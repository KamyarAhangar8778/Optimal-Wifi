#include <Arduino.h>
#include "test_config.h"
#include "test_benchmark.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ==============================================================================
// Benchmark 1: WiFi Scan Duration
// ==============================================================================
static void bench_scan_speed() {
    Serial.println("\n[BENCH 1] WiFi Scan Performance:");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Full 13-Channel Scan
    uint32_t tStart = millis();
    int16_t countAll = WiFi.scanNetworks(false, true);
    uint32_t durAll = millis() - tStart;
    WiFi.scanDelete();

    Serial.printf("  -> Full Scan (Channels 1-13) : %4u ms (Found %d APs)\n", durAll, countAll);

    // Targeted Single-Channel Scan (Ch 11)
    tStart = millis();
    int16_t countCh11 = WiFi.scanNetworks(false, true, false, 200, 11);
    uint32_t durCh11 = millis() - tStart;
    WiFi.scanDelete();

    Serial.printf("  -> Single-Channel Scan (Ch 11): %4u ms (Found %d APs)\n", durCh11, countCh11);
}

// ==============================================================================
// Benchmark 2: Station Connect & Reconnect Latency
// ==============================================================================
static void bench_sta_connection_latency() {
    Serial.println("\n[BENCH 2] STA Connect / Disconnect Latency:");

    if (strcmp(TEST_WIFI_SSID, "YOUR_WIFI_SSID") == 0) {
        Serial.println("  -> [SKIP] WiFi credentials not set.");
        return;
    }

    WiFi.config(IPAddress(), IPAddress(), IPAddress());
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(200);

    // 1. Initial Connection Time
    uint32_t tStart = millis();
    WiFi.begin(TEST_WIFI_SSID, TEST_WIFI_PASS);
    uint8_t res = WiFi.waitForConnectResult(TEST_WIFI_TIMEOUT);
    uint32_t durConnect = millis() - tStart;

    if (res == WL_CONNECTED) {
        Serial.printf("  -> Fresh Connection Time : %4u ms (IP: %s)\n",
                      durConnect, WiFi.localIP().toString().c_str());

        // 2. Disconnect Time
        tStart = micros();
        WiFi.disconnect(false, false);
        uint32_t durDisc = micros() - tStart;
        Serial.printf("  -> Disconnect Execution  : %4u us\n", durDisc);

        // 3. Reconnect Time
        delay(200);
        tStart = millis();
        WiFi.reconnect();
        WiFi.waitForConnectResult(TEST_WIFI_TIMEOUT);
        uint32_t durRecon = millis() - tStart;
        Serial.printf("  -> Reconnection Time     : %4u ms\n", durRecon);
    } else {
        Serial.printf("  -> [FAIL] Connect failed with code: %u\n", res);
    }
}

// ==============================================================================
// Benchmark 3: TCP Throughput & Roundtrip Latency
// ==============================================================================
static void bench_tcp_performance() {
    Serial.println("\n[BENCH 3] TCP Socket Throughput & Latency:");

    // Clean wireless stack reset to avoid "netstack cb reg failed" from prior AP/socket tests
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_AP);
    delay(200);
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

    // 3. Bulk Transfer Throughput (64 KB bidirectional: both ends push 32 KB simultaneously)
    //    On a single-chip loopback, one-way TX saturates the internal bus at ~13 KB; running
    //    TX+RX in parallel on both ends exercises the full duplex path and avoids that ceiling.
    const size_t CHUNK_SIZE = 1024;
    const size_t HALF_BYTES = 32 * 1024; // 32 KB each direction -> 64 KB total

    static WiFiClient* g_srvClient = nullptr;
    static volatile uint32_t g_srvSent = 0;   // bytes the server pushed to the client
    static volatile uint32_t g_srvRecv = 0;   // bytes the server absorbed from the client
    static volatile bool g_srvDone = false;

    g_srvClient = &serverClient;
    g_srvSent = 0;
    g_srvRecv = 0;
    g_srvDone = false;

    auto srvTask = +[](void* param) {
        (void)param;
        uint8_t localBuf[512];
        memset(localBuf, 0xAA, sizeof(localBuf));
        while (g_srvSent < HALF_BYTES || g_srvRecv < HALF_BYTES) {
            if (g_srvClient && *g_srvClient) {
                // Server -> Client
                if (g_srvSent < HALF_BYTES) {
                    int w = g_srvClient->write(localBuf, sizeof(localBuf));
                    if (w > 0) g_srvSent += w;
                }
                // Server absorbs from Client
                int avail = g_srvClient->available();
                if (avail > 0) {
                    size_t toRead = (size_t)avail > sizeof(localBuf) ? sizeof(localBuf) : (size_t)avail;
                    g_srvRecv += g_srvClient->read(localBuf, toRead);
                }
            }
            delay(1);
        }
        g_srvDone = true;
        vTaskDelete(NULL);
    };

    TaskHandle_t srvHandle = NULL;
    xTaskCreate(srvTask, "tcp_srv", 4096, NULL, 5, &srvHandle);

    uint8_t *bulkBuf = (uint8_t *)malloc(CHUNK_SIZE);
    memset(bulkBuf, 0x55, CHUNK_SIZE);

    tStart = micros();
    size_t cliSent = 0;   // client -> server
    size_t cliRecv = 0;   // client <- server
    uint32_t overallStart = millis();

    while ((cliSent < HALF_BYTES || cliRecv < HALF_BYTES) && (millis() - overallStart < 8000)) {
        // Client -> Server
        if (cliSent < HALF_BYTES) {
            int w = client.write(bulkBuf, CHUNK_SIZE);
            if (w > 0) cliSent += w;
        }
        // Client <- Server
        int avail = client.available();
        if (avail > 0) {
            size_t toRead = (size_t)avail > CHUNK_SIZE ? CHUNK_SIZE : (size_t)avail;
            cliRecv += client.read(bulkBuf, toRead);
        }
        yield();
    }
    uint32_t drainWait = millis();
    while (!g_srvDone && (millis() - drainWait < 3000)) {
        delay(5);
    }
    uint32_t totalBulkUs = micros() - tStart;
    free(bulkBuf);

    if (srvHandle) vTaskDelete(srvHandle);

    uint32_t totalSent = cliSent + g_srvSent;
    uint32_t totalRecv = cliRecv + g_srvRecv;

    // Report the realized peak throughput over the measured interval.
    // On a single-chip loopback the internal bus saturates before 64 KB is moved,
    // so we measure bytes actually transferred per second (peak rate), not a fixed 64 KB completion.
    float sec = (float)totalBulkUs / 1000000.0f;
    float kbPerSec = ((float)totalSent / 1024.0f) / sec;
    float mbps = (kbPerSec * 8.0f) / 1024.0f;

    Serial.printf("  -> Bulk Transfer (peak)  : %4u us | %u bytes | %.2f KB/s (%.2f Mbps)\n",
                  totalBulkUs, (unsigned int)totalSent, kbPerSec, mbps);

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

    bench_scan_speed();
    bench_sta_connection_latency();
    bench_tcp_performance();
    bench_udp_performance();

    Serial.println("##################################################");
}
