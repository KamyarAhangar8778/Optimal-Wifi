#include <Arduino.h>
#include "test_config.h"
#include "test_benchmark.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ==============================================================================
// Bulk-transfer harness: dedicated drain task with STRICT socket ownership.
// On single-chip loopback a single task CANNOT both push (blocking write)
// and drain the echo: a full send buffer blocks write() while the echo sits
// unread, which closes the TCP receive window => guaranteed deadlock.
// Two tasks, each owning exactly ONE socket, is the correct sustained
// throughput measurement (and mirrors real-world client/server usage).
// Shutdown is cooperative (flag + self-delete) — never force-delete a task
// that may sit inside lwip.
// ==============================================================================
static WiFiClient* g_bulkSrvCli = nullptr;   // owned ONLY by drain task
static volatile uint32_t g_bulkEchoBytes = 0;
static volatile bool g_bulkStop = false;
static volatile bool g_bulkDrainExited = false;
static volatile int  g_bulkDrainExit = 0;    // 0=stop flag, 1=null client, 2=disconnected, 3=read<0

static void bulk_drain_task(void*)
{
    uint8_t tmp[1460];
    while (!g_bulkStop) {
        WiFiClient* c = g_bulkSrvCli;
        if (!c) { g_bulkDrainExit = 1; break; }
        if (!c->connected()) { g_bulkDrainExit = 2; break; }
        int av = c->available();
        if (av > 0) {
            int r = c->read(tmp, (size_t)av > sizeof(tmp) ? sizeof(tmp) : (size_t)av);
            if (r > 0) { g_bulkEchoBytes += (uint32_t)r; continue; }
            if (r < 0) { g_bulkDrainExit = 3; break; }
        }
        vTaskDelay(1);
    }
    g_bulkDrainExited = true;
    vTaskDelete(NULL);
}

// ==============================================================================
// Benchmark 3: TCP Throughput & Roundtrip Latency
// ==============================================================================
static void bench_tcp_performance() {
    Serial.println("\n[BENCH 3] TCP Socket Throughput & Latency:");

    // Fully settle the wireless stack: stop STA, then wait long enough for the
    // LWIP/event queue to drain before switching modes. Without this, a pending
    // STA reconnect event races the AP-mode switch and triggers xQueueGenericSend
    // asserts (pvItemToQueue == NULL).
    WiFi.disconnect(false, false);
    delay(500);
    WiFi.mode(WIFI_OFF);
    delay(1000);
    WiFi.mode(WIFI_AP);
    delay(500);
    WiFi.softAP("ESP32_Bench_AP", "12345678");
    delay(500);

    const uint16_t port = 9191;
    IPAddress hostIP = WiFi.softAPIP();

    WiFiServer server(port);
    server.begin();
    delay(100);

    // 1. Connection Handshake Latency
    uint32_t tStart = micros();
    WiFiClient client;
    if (!client.connect(hostIP, port, 2000)) {
        Serial.println("  -> [FAIL] TCP Connect failed.");
        server.end();
        return;
    }
    uint32_t connLatency = micros() - tStart;

    WiFiClient serverClient = server.available();
    uint32_t wStart = millis();
    while (!serverClient && (millis() - wStart < 2000)) {
        serverClient = server.available();
        delay(5);
    }
    if (!serverClient) {
        Serial.println("  -> [FAIL] Server accept failed.");
        client.stop();
        server.end();
        return;
    }

    client.setNoDelay(true);
    serverClient.setNoDelay(true);

    Serial.printf("  -> Handshake Latency     : %4u us\n", connLatency);

    // 2. Ping-Pong Latency (100 rounds of 64 bytes)
    const int PING_COUNT = 100;
    uint8_t pingBuf[64] = {0xAA};
    uint8_t pongBuf[64] = {0};

    uint32_t totalPingUs = 0;
    for (int i = 0; i < PING_COUNT; ++i) {
        uint32_t pStart = micros();
        client.write(pingBuf, sizeof(pingBuf));
        client.flush();

        uint32_t pw = millis();
        while (serverClient.available() < (int)sizeof(pingBuf) && (millis() - pw < 1000)) {
            delayMicroseconds(10);
        }
        serverClient.read(pongBuf, sizeof(pongBuf));
        serverClient.write(pongBuf, sizeof(pongBuf));
        serverClient.flush();

        pw = millis();
        while (client.available() < (int)sizeof(pongBuf) && (millis() - pw < 1000)) {
            delayMicroseconds(10);
        }
        client.read(pongBuf, sizeof(pongBuf));
        totalPingUs += (micros() - pStart);
    }
    Serial.printf("  -> Roundtrip Latency (Avg): %4.1f us / ping-pong (100 rounds)\n",
                  (float)totalPingUs / PING_COUNT);

    // 3. Bulk Transfer Throughput — DUAL-TASK: sender task pushes into the
    //    client socket while a dedicated drain task reads the server's echo.
    //    Strict one-socket-per-task ownership avoids the lwip cross-task
    //    deadlock and measures true sustained loopback throughput.
    const size_t CHUNK_SIZE = 1460;          // ~MSS-sized writes
    const size_t PAYLOAD_BYTES = 64 * 1024;  // 64 KB per round
    const int BULK_ROUNDS = 3;

    uint8_t *bulkBuf = (uint8_t *)malloc(CHUNK_SIZE);
    memset(bulkBuf, 0x55, CHUNK_SIZE);

    Serial.println("  -> Bulk Transfer (dual-task TX + dedicated RX drain, x3):");

    g_bulkSrvCli = &serverClient;

    for (int round = 1; round <= BULK_ROUNDS; ++round) {
        g_bulkEchoBytes = 0;
        g_bulkStop = false;
        g_bulkDrainExited = false;

        TaskHandle_t drainHandle = nullptr;
        if (xTaskCreate(bulk_drain_task, "bulkdrain", 4096, nullptr,
                        configMAX_PRIORITIES - 2, &drainHandle) != pdPASS) {
            Serial.println("     [FAIL] could not spawn drain task");
            break;
        }

        tStart = micros();
        size_t sentTotal = 0;
        uint32_t startMs = millis();
        uint32_t lastProgressMs = startMs;
        const char* endNote = "";

        while (sentTotal < PAYLOAD_BYTES && (millis() - startMs < 10000)) {
            int w = client.write(bulkBuf, CHUNK_SIZE);
            if (w > 0) { sentTotal += (size_t)w; lastProgressMs = millis(); }
            else if (millis() - lastProgressMs > 1500) {
                endNote = "  [STALL - aborted]";
                break;
            }
        }
        uint32_t totalUs = micros() - tStart;

        // Graceful stop: flag first, give the drain task time to exit its
        // lwip call on its own before we touch anything.
        g_bulkStop = true;
        for (int i = 0; i < 200 && !g_bulkDrainExited; ++i) vTaskDelay(1);

        uint32_t echoed = g_bulkEchoBytes;
        if (!g_bulkDrainExited) endNote = "  [DRAIN TASK HUNG]";
        else if (sentTotal < PAYLOAD_BYTES) endNote = "  [TIMEOUT]";

        float sec = (float)totalUs / 1000000.0f;
        float kbPerSec = ((float)sentTotal / 1024.0f) / sec;
        float mbps = (kbPerSec * 8.0f) / 1024.0f;

        Serial.printf("     Round %d: sent %u B (echo %u B) | %u us | %.2f KB/s (%.2f Mbps)%s\n",
                      round, (unsigned)sentTotal, (unsigned)echoed,
                      (unsigned)totalUs, kbPerSec, mbps, endNote);

        vTaskDelay(50);   // let lwip settle between rounds
    }

    free(bulkBuf);

    client.stop();
    serverClient.stop();
    server.end();
}

// ==============================================================================
// Benchmark 4: UDP Throughput & Latency
// ==============================================================================
static void bench_udp_performance() {
    Serial.println("\n[BENCH 4] UDP Packet Throughput & Latency:");

    WiFi.mode(WIFI_AP);
    delay(50);
    IPAddress hostIP = WiFi.softAPIP();
    uint16_t udpPort = 9292;

    WiFiUDP udp;
    if (!udp.begin(udpPort)) {
        Serial.println("  -> [FAIL] UDP begin failed.");
        return;
    }

    const int PACKET_COUNT = 200;
    const size_t PACKET_SIZE = 256;
    uint8_t packetData[PACKET_SIZE];
    memset(packetData, 0xEE, PACKET_SIZE);

    uint32_t tStart = micros();
    int successfulPackets = 0;

    for (int i = 0; i < PACKET_COUNT; ++i) {
        udp.beginPacket(hostIP, udpPort);
        udp.write(packetData, PACKET_SIZE);
        udp.endPacket();

        uint32_t wStart = micros();
        while (udp.parsePacket() == 0 && (micros() - wStart < 50000)) {
            delayMicroseconds(20);
        }
        if (udp.available()) {
            udp.read(packetData, PACKET_SIZE);
            successfulPackets++;
        }
    }
    uint32_t durUs = micros() - tStart;
    udp.stop();

    float sec = (float)durUs / 1000000.0f;
    float pps = (float)successfulPackets / sec;
    float kbPerSec = ((float)(successfulPackets * PACKET_SIZE) / 1024.0f) / sec;

    Serial.printf("  -> 200 UDP Packets (256B) : %4u us | %u/%u Delivered\n",
                  durUs, successfulPackets, PACKET_COUNT);
    Serial.printf("  -> UDP Throughput         : %.1f Packets/sec | %.2f KB/s\n",
                  pps, kbPerSec);
}

void run_all_benchmarks() {
    Serial.println();
    Serial.println("##################################################");
    Serial.println("#         BASELINE PERFORMANCE BENCHMARKS        #");
    Serial.println("##################################################");

    bench_tcp_performance();
    bench_udp_performance();

    Serial.println("##################################################");
}
