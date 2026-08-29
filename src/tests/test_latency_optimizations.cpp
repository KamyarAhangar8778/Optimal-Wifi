#include "test_config.h"
#include "test_latency_optimizations.h"

#include <Arduino.h>
#include <WiFi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

static inline uint8_t pat_byte(size_t i)
{
    return (uint8_t)(i * 31u + 7u);
}

static WiFiClient wait_server_client(WiFiServer &srv, uint32_t budgetMs)
{
    WiFiClient c = srv.available();
    uint32_t t0 = millis();
    while (!c && (millis() - t0) < budgetMs)
    {
        c = srv.available();
        delay(1);
    }
    return c;
}

static void print_section(const char *name)
{
    Serial.println();
    Serial.println("--------------------------------------------------");
    Serial.println(name);
    Serial.println("--------------------------------------------------");
}

// Wait for `client` to have at least `need` bytes available, using yield()
// (not delay()) for microsecond-precision wait that doesn't add 1ms quantization.
static bool wait_avail_yield(WiFiClient &client, size_t need, uint32_t budgetUs)
{
    uint32_t s = micros();
    while ((uint32_t)client.available() < need && (micros() - s) < budgetUs)
        yield();
    return (size_t)client.available() >= need;
}

// Wait for `client` to have at least `need` bytes available, using taskYIELD()
// for the tightest possible polling loop — no millis() quantization.
static bool wait_avail_tight(WiFiClient &client, size_t need, uint32_t budgetUs)
{
    uint32_t s = micros();
    while ((size_t)client.available() < need && (micros() - s) < budgetUs)
    {
        // Tight yield: lets lwIP poll task run without 1ms task-switch quantization
        taskYIELD();
    }
    return (size_t)client.available() >= need;
}

} // namespace

void run_latency_optimization_tests()
{
    TEST_SECTION_START("Latency Optimization Verification");

    WiFi.mode(WIFI_AP);
    delay(100);
    WiFi.softAP("ESP32_Latency_Opt", "12345678");
    WiFi.setSleep(false);
    delay(500);

    IPAddress hostIP = WiFi.softAPIP();
    const uint16_t port = 9291;
    WiFiServer server(port);
    server.begin();
    delay(100);

    WiFiClient client;
    if (!client.connect(hostIP, port, 3000))
    {
        Serial.println("  -> [FAIL] client connect failed");
        server.end();
        WiFi.mode(WIFI_OFF);
        return;
    }

    WiFiClient serverClient = wait_server_client(server, 2000);
    if (!serverClient)
    {
        Serial.println("  -> [FAIL] server accept failed");
        client.stop();
        server.end();
        WiFi.mode(WIFI_OFF);
        return;
    }

    client.setNoDelay(true);
    serverClient.setNoDelay(true);

    // ---- Test 1: Micro-packet roundtrip latency (22B MQTT-like) ----
    // Also reports ONE-WAY radio latency (client->air->server) by splitting the
    // RTT at the server-side echo: this isolates the ESP32 radio floor from any
    // server-side delay. On loopback both halves are the self->air->self path.
    print_section("[OPT L1] Micro-packet roundtrip (22B x 200, tight poll)");
    uint8_t frame[22];
    for (size_t i = 0; i < 22; i++)
        frame[i] = pat_byte(i);

    uint8_t rxbuf[22];
    uint32_t minUs = 0xFFFFFFFF, maxUs = 0, totalUs = 0;
    uint32_t oneWayMin = 0xFFFFFFFF, oneWayMax = 0, oneWayTotal = 0;
    uint32_t successCount = 0;

    for (int i = 0; i < 200; ++i)
    {
        uint32_t s = micros();
        client.write(frame, 22);
        if (!wait_avail_tight(serverClient, 22, 50000))
        {
            continue;
        }
        uint32_t t1 = micros(); // server RX arrived = one-way TX over radio done
        serverClient.read(rxbuf, 22);
        serverClient.write(rxbuf, 22);
        if (!wait_avail_tight(client, 22, 50000))
        {
            continue;
        }
        uint32_t t2 = micros(); // client RX arrived = echo over radio done
        client.read(rxbuf, 22);
        uint32_t d = micros() - s;
        if (d < minUs) minUs = d;
        if (d > maxUs) maxUs = d;
        totalUs += d;
        // One-way = server delivery latency (client->air->server).
        uint32_t ow = t1 - s;
        if (ow < oneWayMin) oneWayMin = ow;
        if (ow > oneWayMax) oneWayMax = ow;
        oneWayTotal += ow;
        successCount++;
    }

    if (successCount > 0)
    {
        Serial.printf("  -> avg : %.0f us\n", (float)totalUs / successCount);
        Serial.printf("  -> min : %u us\n", minUs);
        Serial.printf("  -> max : %u us\n", maxUs);
        Serial.printf("  -> sent: %u / 200\n", (unsigned)successCount);
        Serial.printf("  -> one-way (client->server): avg %.0f / min %u / max %u us\n",
                      (float)oneWayTotal / successCount, oneWayMin, oneWayMax);
        Serial.println("     NOTE: loopback = self->air->self; this is the ESP32 radio floor.");
    }

    // ---- Test 2: connected() hot-path syscall cost ----
    print_section("[OPT L2] connected() syscall cost (100k calls, no data)");
    client.write("X", 1);
    wait_avail_tight(serverClient, 1, 50000);
    serverClient.read(rxbuf, 1);
    // Don't forward — leave the 1 byte buffered in client's RX buffer
    client.write("Y", 1);
    wait_avail_tight(serverClient, 1, 50000);
    serverClient.read(rxbuf, 1);
    // Now client RX buffer is empty — connected() will do a poll() syscall

    uint32_t cStart = micros();
    for (int i = 0; i < 100000; ++i)
    {
        client.connected();
    }
    uint32_t cDuration = micros() - cStart;
    Serial.printf("  -> 100k connected() calls: %u us (%.1f ns/op)\n",
                  (unsigned)cDuration, (float)cDuration / 100000.0f);

    // ---- Test 3: Burst write throughput (dual-task, real loopback) ----
    // A dedicated drain task reads & echoes from serverClient so the TX path
    // stays unclogged — mirrors the L3 benchmark's structure. Without a reader
    // the TX buffer would fill and write() would spin to its timeout.
    print_section("[OPT L3] Burst write throughput (50x 22B, async drain)");
    static uint8_t burstBuf[22];
    for (size_t i = 0; i < 22; i++)
        burstBuf[i] = pat_byte(i);

    // Drain task: echo everything received back to the client.
    struct OptDrainCtx
    {
        WiFiClient *c;
        volatile bool *stop;
        volatile uint32_t *echoed;
    };
    static volatile bool drainStop = false;
    static volatile uint32_t drainEchoed = 0;
    drainStop = false;
    drainEchoed = 0;
    static OptDrainCtx drainCtx;
    drainCtx.c = &serverClient;
    drainCtx.stop = &drainStop;
    drainCtx.echoed = &drainEchoed;

    auto drain_fn = +[](void *arg) {
        OptDrainCtx *ctx = (OptDrainCtx *)arg;
        WiFiClient *c = ctx->c;
        uint8_t dbuf[1460];
        while (!*ctx->stop)
        {
            int av = c->available();
            if (av > 0)
            {
                size_t n = (size_t)av < sizeof(dbuf) ? (size_t)av : sizeof(dbuf);
                int r = c->read(dbuf, n);
                if (r > 0)
                {
                    size_t sent = 0;
                    while (sent < (size_t)r)
                    {
                        int w = c->write(dbuf + sent, (size_t)r - sent);
                        if (w <= 0)
                            break;
                        sent += (size_t)w;
                    }
                    *ctx->echoed += (uint32_t)r;
                    continue;
                }
            }
            vTaskDelay(1); // block: feeds the TWDT and lets lower-priority tasks run
        }
        vTaskDelete(NULL);
    };
    TaskHandle_t drainTask = nullptr;
    xTaskCreate(drain_fn, "optdrain", 4096, &drainCtx, configMAX_PRIORITIES - 2, &drainTask);

    uint32_t bwStart = micros();
    size_t bwTotal = 0;
    int bwSent = 0;
    for (int i = 0; i < 50; ++i)
    {
        size_t w = client.write(burstBuf, 22);
        if (w > 0)
        {
            bwTotal += w;
            bwSent++;
        }
    }
    uint32_t bwUs = micros() - bwStart;
    Serial.printf("  -> wrote %u frames (%u B) in %u us = %.1f KB/s\n",
                  bwSent, (unsigned)bwTotal, (unsigned)bwUs,
                  (float)(bwTotal / 1024.0f) / ((float)bwUs / 1000000.0f));

    // Let the drain task finish echoing, then stop it cooperatively.
    uint32_t ds = millis();
    while (drainEchoed < bwTotal && (millis() - ds) < 2000)
        vTaskDelay(1);
    drainStop = true;
    for (int i = 0; i < 200; ++i)
        vTaskDelay(1);

    client.stop();
    serverClient.stop();
    server.end();
    WiFi.mode(WIFI_OFF);

    Serial.println();
    Serial.println("[OPT] Done.");
}
