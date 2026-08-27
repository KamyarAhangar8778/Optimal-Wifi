#include "test_config.h"
#include "test_async_client.h"
#include "test_async_client_helpers.h"

void test_async_bulk_write_drain()
{
    TEST_CASE_START("Async bulk write (zero-copy drain)");

    WiFi.mode(WIFI_AP);
    delay(50);

    WiFiServer server(8091);
    server.begin();

    WiFiClient client;
    if (!client.connectAsync(WiFi.softAPIP(), 8091))
    {
        server.end();
        TEST_FAIL("connectAsync refused to start");
        return;
    }
    uint32_t iters, worst;
    if (pump_connect(client, 3000, iters, worst) != 1)
    {
        client.stop();
        server.end();
        TEST_FAIL("pollConnect failed");
        return;
    }

    WiFiClient serverClient = server.available();
    uint32_t waitStart = millis();
    while (!serverClient && (millis() - waitStart < 1000))
    {
        serverClient = server.available();
        delay(5);
    }
    if (!serverClient)
    {
        client.stop();
        server.end();
        TEST_FAIL("server never accepted");
        return;
    }

    // Static storage: the buffer MUST stay valid & unmodified while the async
    // drain holds a zero-copy view into it.
    static uint8_t payload[kPayloadSize];
    for (size_t i = 0; i < kPayloadSize; i++)
        payload[i] = pattern_byte(i);

    size_t firstAccepted = client.writeAsync(payload, kPayloadSize);
    bool queuedPath = client.pendingWrite() > 0;

    // Drain TX and consume RX CONCURRENTLY: both endpoints sit on the same
    // ESP32, so once the peer's kernel RX buffer fills its TCP window closes
    // and the remainder can only flush while someone reads. Interleaving the
    // two mirrors real duplex traffic instead of a fill-then-read sequence.
    static uint8_t scratch[1024];
    size_t got = 0;
    bool match = true;
    bool hardError = false;
    uint32_t drainIters = 0;
    uint32_t tStart = millis();
    while ((millis() - tStart < 5000) &&
           (client.writeBusy() || got < kPayloadSize))
    {
        if (client.writeBusy())
        {
            if (client.pollWrite() < 0)
            {
                hardError = true;
                break;
            }
            drainIters++;
            if (drainIters > 500000)
                break; // watchdog, should not trigger
        }
        int n = serverClient.read(scratch, sizeof(scratch));
        if (n > 0)
        {
            for (int i = 0; i < n; i++)
            {
                if (scratch[i] != pattern_byte(got + (size_t)i))
                {
                    match = false;
                    break;
                }
            }
            if (!match)
                break;
            got += (size_t)n;
        }
    }

    client.stop();
    serverClient.stop();
    server.end();

    if (hardError)
    {
        TEST_FAIL("pollWrite hit a hard error");
        return;
    }
    if (match && got != kPayloadSize)
    {
        TEST_FAIL("transfer timed out incomplete");
        return;
    }
    if (!match)
    {
        TEST_FAIL("payload corrupted in transit");
        return;
    }
    if (firstAccepted == 0)
    {
        TEST_FAIL("writeAsync accepted nothing");
        return;
    }

    Serial.printf("\n      -> first send %u/%u bytes (%s), drain iterations: %u",
                  (unsigned)firstAccepted, (unsigned)kPayloadSize,
                  queuedPath ? "queued remainder" : "kernel took all",
                  (unsigned)drainIters);
    TEST_PASS();
}

void test_hot_path_accessors()
{
    TEST_CASE_START("Hot-path endpoint cache & zero-syscall connected()");

    WiFi.mode(WIFI_AP);
    delay(50);

    WiFiServer server(8093);
    server.begin();

    WiFiClient client;
    if (!client.connectAsync(WiFi.softAPIP(), 8093))
    {
        server.end();
        TEST_FAIL("connectAsync refused to start");
        return;
    }
    uint32_t iters, worst;
    if (pump_connect(client, 3000, iters, worst) != 1)
    {
        client.stop();
        server.end();
        TEST_FAIL("pollConnect failed");
        return;
    }

    WiFiClient serverClient = server.available();
    uint32_t waitStart = millis();
    while (!serverClient && (millis() - waitStart < 1000))
    {
        serverClient = server.available();
        delay(5);
    }
    if (!serverClient)
    {
        client.stop();
        server.end();
        TEST_FAIL("server never accepted");
        return;
    }

    // --- endpoint cache: first call resolves, subsequent calls must return
    // identical values at RAM speed ---
    IPAddress ip1 = client.remoteIP();
    uint16_t p1 = client.remotePort();
    IPAddress l1 = client.localIP();
    uint16_t lp1 = client.localPort();
    bool consistent = true;
    for (int i = 0; i < 10000; i++)
    { // 10k calls: only the FIRST may touch a syscall
        if (client.remoteIP() != ip1 || client.remotePort() != p1 ||
            client.localIP() != l1 || client.localPort() != lp1)
        {
            consistent = false;
            break;
        }
    }
    if (!consistent || !(ip1 == WiFi.softAPIP()))
    {
        client.stop();
        serverClient.stop();
        server.end();
        TEST_FAIL("endpoint cache inconsistent or wrong peer IP");
        return;
    }

    // --- zero-syscall connected(): push data, then verify connected() stays
    // true across the buffered window WITHOUT ever probing the socket ---
    const char *msg = "CACHE_PROBE";
    serverClient.print(msg);
    waitStart = millis();
    while (client.available() < (int)strlen(msg) && (millis() - waitStart < 1000))
    {
        delay(5);
    }
    bool aliveWhileBuffered = true;
    for (int i = 0; i < 100000; i++)
    { // 100k calls, all served from RAM
        if (!client.connected())
        {
            aliveWhileBuffered = false;
            break;
        }
    }
    bool dataIntact = client.available() >= (int)strlen(msg);

    client.stop();
    serverClient.stop();
    server.end();

    if (!aliveWhileBuffered)
    {
        TEST_FAIL("connected() dropped while RX data was buffered");
        return;
    }
    if (!dataIntact)
    {
        TEST_FAIL("buffered RX data lost during connected() polling");
        return;
    }

    Serial.printf("\n      -> 10k cached endpoint reads + 100k RAM-only connected() calls OK");
    TEST_PASS();
}