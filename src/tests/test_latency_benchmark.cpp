#include "test_config.h"
#include "test_latency_benchmark.h"

#include <Arduino.h>
#include <WiFi.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

// Size of a minimal MQTT PUBLISH-ish frame (topic + payload overhead + payload).
static constexpr size_t kMqttFrameSize = 22;
// Size of a typical WebSocket text frame payload.
static constexpr size_t kWssFrameSize = 64;
static constexpr int kPingRounds = 100;
static constexpr int kBurstCount = 50;

static inline uint8_t pat(size_t i)
{
    return (uint8_t)(i * 31u + 7u);
}

// A single drain task: reads & re-echoes every byte received on its socket.
// Mirrors the test_benchmark dual-task pattern (one socket per task).
struct EchoCtx
{
    WiFiClient *echoClient;   // server-side accepted client, owned by drain task
    volatile bool stop;       // set true from the loop task to stop the drain
    volatile bool exited;     // set true by drain when it deletes itself
    volatile uint32_t totalEchoed; // bytes echoed back
    volatile int failCode;    // non-zero if drain exited abnormally
};

static void echo_drain_task(void *arg)
{
    EchoCtx *ctx = (EchoCtx *)arg;
    uint8_t buf[1460];
    while (!ctx->stop)
    {
        WiFiClient *c = ctx->echoClient;
        if (!c || !c->connected())
        {
            ctx->failCode = (c == nullptr) ? 1 : 2;
            break;
        }
        int av = c->available();
        if (av > 0)
        {
            size_t cap = sizeof(buf);
            size_t n = (size_t)av < cap ? (size_t)av : cap;
            int r = c->read(buf, n);
            if (r > 0)
            {
                size_t sent = 0;
                while (sent < (size_t)r)
                {
                    int w = c->write(buf + sent, (size_t)r - sent);
                    if (w <= 0)
                        break;
                    sent += (size_t)w;
                }
                ctx->totalEchoed += (uint32_t)r;
                continue;
            }
            if (r < 0)
            {
                ctx->failCode = 3;
                break;
            }
        }
        vTaskDelay(1);
    }
    ctx->exited = true;
    vTaskDelete(NULL);
}

static WiFiClient wait_server_client(WiFiServer &srv, uint32_t budgetMs)
{
    WiFiClient c = srv.available();
    uint32_t t0 = millis();
    while (!c && (millis() - t0) < budgetMs)
    {
        c = srv.available();
        delay(5);
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

// Wait for `client` to have at least `need` bytes available, with a budget.
// Uses yield() (not delay) so the WiFi/lwIP background task keeps servicing
// the connection; delayMicroseconds would starve it on single-core.
static bool wait_avail(WiFiClient &client, size_t need, uint32_t budgetUs)
{
    uint32_t s = micros();
    while ((uint32_t)client.available() < need && (micros() - s) < budgetUs)
        yield();
    return (uint32_t)client.available() >= need;
}

static bool run_ping_pong(WiFiClient &client, WiFiClient &serverClient,
                          const uint8_t *frame, size_t len,
                          uint32_t &minUs, uint32_t &maxUs, uint32_t &totalUs,
                          bool &ok)
{
    ok = true;
    minUs = 0xFFFFFFFFu;
    maxUs = 0;
    totalUs = 0;

    uint8_t *rxb = (uint8_t *)malloc(len);
    if (!rxb)
    {
        ok = false;
        return false;
    }

    for (int i = 0; i < kPingRounds; ++i)
    {
        uint32_t s = micros();
        client.write(frame, len);
        // NOTE: deliberately no flush() — flush() blocks until the peer ACKs,
        // which would conflate application latency with TCP ACK-timing. We only
        // want kernel-accept latency (the moment send() returns).
        if (!wait_avail(serverClient, len, 1000000))
        {
            ok = false;
            free(rxb);
            return false;
        }
        serverClient.read(rxb, len);
        serverClient.write(rxb, len);
        // server reply must be kernel-accepted; no flush needed for RTT.
        if (!wait_avail(client, len, 1000000))
        {
            ok = false;
            free(rxb);
            return false;
        }
        client.read(rxb, len);

        uint32_t d = micros() - s;
        if (d < minUs) minUs = d;
        if (d > maxUs) maxUs = d;
        totalUs += d;
        for (size_t j = 0; j < len; j++)
        {
            if (rxb[j] != frame[j]) { ok = false; break; }
        }
        if (!ok) break;
    }
    free(rxb);
    return true;
}

} // namespace

void run_mqtt_latency_benchmark()
{
    uint32_t t0 = millis();

    // ---- Setup: AP + echo server ----
    WiFi.disconnect(false, false);
    delay(500);
    WiFi.mode(WIFI_AP);
    delay(1000);
    WiFi.softAP("ESP32_Latency_AP", "12345678");
    delay(500);

    // Disable modem sleep so RX latency isn't inflated by beacon-interval gaps.
    WiFi.setSleep(false);

    IPAddress hostIP = WiFi.softAPIP();
    const uint16_t port = 9191;
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

    // ---- 1. MQTT-like round-trip latency (22-byte frames) ----
    print_section("[BENCH L1] MQTT-like roundtrip (22B x 100)");

    uint8_t frame[kMqttFrameSize];
    for (size_t i = 0; i < kMqttFrameSize; i++)
        frame[i] = pat(i);

    uint32_t minUs, maxUs, totalUs;
    bool ok;
    if (run_ping_pong(client, serverClient, frame, kMqttFrameSize,
                      minUs, maxUs, totalUs, ok) && ok)
    {
        Serial.printf("  -> avg : %.0f us\n", (float)totalUs / kPingRounds);
        Serial.printf("  -> min : %u us\n", minUs);
        Serial.printf("  -> max : %u us\n", maxUs);
    }
    else
    {
        Serial.println("  -> [FAIL] 22B ping-pong");
    }

    // ---- 2. WSS-like round-trip latency (64-byte frames) ----
    print_section("[BENCH L2] WSS-like roundtrip (64B x 100)");

    uint8_t f64[kWssFrameSize];
    for (size_t i = 0; i < kWssFrameSize; i++)
        f64[i] = (uint8_t)(i * 13u + 5u);
    if (run_ping_pong(client, serverClient, f64, kWssFrameSize,
                      minUs, maxUs, totalUs, ok) && ok)
    {
        Serial.printf("  -> avg : %.0f us\n", (float)totalUs / kPingRounds);
        Serial.printf("  -> min : %u us\n", minUs);
        Serial.printf("  -> max : %u us\n", maxUs);
    }
    else
    {
        Serial.println("  -> [FAIL] 64B ping-pong");
    }

    // ---- 3. Burst fire-and-drain (small frames, peer stalls then drains) ----
    print_section("[BENCH L3] Burst fire (22B x 50, peer stalls then drains)");

    EchoCtx echo;
    echo.echoClient = &serverClient;
    echo.stop = false;
    echo.exited = false;
    echo.totalEchoed = 0;
    echo.failCode = 0;

    TaskHandle_t th = nullptr;
    if (xTaskCreate(echo_drain_task, "echodrain", 4096, &echo,
                    configMAX_PRIORITIES - 2, &th) != pdPASS)
    {
        Serial.println("  -> [FAIL] could not start drain task");
    }
    else
    {
        uint32_t bs = micros();
        size_t written = 0;
        for (int i = 0; i < kBurstCount; ++i)
        {
            size_t w = client.write(frame, kMqttFrameSize);
            written += w;
        }
        uint32_t bwriteUs = micros() - bs;

        uint32_t ws = millis();
        while (echo.totalEchoed < written && (millis() - ws) < 5000)
            vTaskDelay(1);
        uint32_t drainMs = millis() - ws;

        echo.stop = true;
        for (int i = 0; i < 200 && !echo.exited; ++i)
            vTaskDelay(1);

        float bw = bwriteUs ? ((float)written / 1024.0f) / ((float)bwriteUs / 1000000.0f) : 0.0f;
        Serial.printf("  -> %u frames x %uB = %u B written in %u us (%.1f KB/s)\n",
                      kBurstCount, (unsigned)kMqttFrameSize,
                      (unsigned)written, (unsigned)bwriteUs, bw);
        Serial.printf("  -> drain took %u ms (echoed %u B, fail %d)\n",
                      (unsigned)drainMs, (unsigned)echo.totalEchoed, echo.failCode);
    }

    client.stop();
    serverClient.stop();
    server.end();
    WiFi.mode(WIFI_OFF);

    Serial.printf("\n[L-BENCH] total elapsed %u ms\n", (unsigned)(millis() - t0));
}
