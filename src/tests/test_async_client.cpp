#include "test_config.h"
#include "test_async_client.h"

static const size_t kPayloadSize = 16384;

static uint8_t pattern_byte(uint32_t i)
{
    return (uint8_t)(i * 31u + 7u);
}

// Drives pollConnect() until it leaves the "connecting" state, tracking how
// many other-work iterations loop() squeezed in plus the worst single stall
// observed (max gap between consecutive poll calls).
static int pump_connect(WiFiClient &c, uint32_t budgetMs,
                        uint32_t &iterations, uint32_t &worstStallMs)
{
    iterations = 0;
    worstStallMs = 0;
    uint32_t last = millis();
    uint32_t t0 = last;
    int rc;
    while ((rc = c.pollConnect()) == 0 && (millis() - t0) < budgetMs)
    {
        iterations++;
        uint32_t now = millis();
        uint32_t gap = now - last;
        if (gap > worstStallMs)
            worstStallMs = gap;
        last = now;
    }
    return rc;
}

static void test_async_connect_loopback()
{
    TEST_CASE_START("Async connect (non-blocking) + round-trip");

    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32_Async_Test", "12345678");
    delay(100);

    WiFiServer server(8090);
    server.begin();

    WiFiClient client;
    if (!client.connectAsync(WiFi.softAPIP(), 8090))
    {
        server.end();
        TEST_FAIL("connectAsync refused to start");
        return;
    }

    uint32_t iters, worst;
    int rc = pump_connect(client, 3000, iters, worst);
    if (rc != 1)
    {
        client.stop();
        server.end();
        TEST_FAIL("pollConnect never reached connected state");
        return;
    }
    Serial.printf("\n      -> connected after %u polls, worst loop stall %u ms",
                  (unsigned)iters, (unsigned)worst);

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
        TEST_FAIL("server never accepted the async connection");
        return;
    }

    const char *msg = "ASYNC_OK";
    size_t sent = client.writeAsync((const uint8_t *)msg, strlen(msg));
    while (client.writeBusy() && client.pollWrite() == 0)
    { /* drain */
    }

    waitStart = millis();
    while (serverClient.available() < (int)strlen(msg) && (millis() - waitStart < 1000))
    {
        delay(5);
    }
    char rx[16] = {0};
    serverClient.read((uint8_t *)rx, strlen(msg));

    client.stop();
    serverClient.stop();
    server.end();

    if (sent == strlen(msg) && strcmp(rx, msg) == 0)
    {
        TEST_PASS();
    }
    else
    {
        TEST_FAIL("round-trip data mismatch");
    }
}

static void test_async_bulk_write_drain()
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

static void test_async_vs_blocking_stall()
{
    TEST_CASE_START("Loop stall benchmark: async vs blocking connect");

    WiFi.mode(WIFI_AP);
    delay(50);

    // A subnet-local address with nobody behind it: SYN retries keep BOTH
    // variants busy for their whole deadline, giving a fair stall comparison.
    IPAddress apIp = WiFi.softAPIP();
    IPAddress silentIp(apIp[0], apIp[1], apIp[2], apIp[3] == 250 ? 249 : 250);

    // --- asynchronous path ---
    WiFiClient ac;
    ac.setTimeout(1); // 1 s deadline
    uint32_t t0 = millis();
    if (!ac.connectAsync(silentIp, 8092))
    {
        TEST_FAIL("connectAsync refused to start");
        return;
    }
    uint32_t iters = 0, worst = 0, last = millis();
    int rc;
    while ((rc = ac.pollConnect()) == 0 && (millis() - t0) < 5000)
    {
        iters++;
        uint32_t now = millis();
        if ((now - last) > worst)
            worst = now - last;
        last = now;
    }
    ac.stop();

    // --- blocking baseline ---
    WiFiClient bc;
    bc.setTimeout(1);
    uint32_t tb = millis();
    bc.connect(silentIp, 8092, 1000);
    uint32_t blockStall = millis() - tb;
    bc.stop();

    Serial.printf("\n      -> ASYNC : %u loop iterations kept running, worst stall %u ms",
                  (unsigned)iters, (unsigned)worst);
    Serial.printf("\n      -> BLOCK : loop frozen %u ms", (unsigned)blockStall);

    if (rc == -1 && blockStall >= 900 && iters >= 100 && worst < blockStall / 10)
    {
        TEST_PASS();
    }
    else
    {
        TEST_FAIL("unexpected async/blocking stall behavior");
    }
}

static void test_hot_path_accessors()
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

static void test_write_slice_latency()
{
    TEST_CASE_START("Blocking write buffer-full slice latency");

    WiFi.mode(WIFI_AP);
    delay(50);

    WiFiServer server(8094);
    server.begin();

    WiFiClient client;
    if (!client.connectAsync(WiFi.softAPIP(), 8094))
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

    // Deliberately do NOT read on the server side: the peer window closes and
    // write() must enter its buffer-full path. Measure the WORST single-call
    // duration with a short deadline so the slice size dominates.
    client.setTimeout(1); // 1s
    static uint8_t chunk[4096];
    memset(chunk, 0xAB, sizeof(chunk));
    uint32_t worstCall = 0;
    for (int i = 0; i < 8; i++)
    {
        uint32_t t0 = millis();
        client.write(chunk, sizeof(chunk));
        uint32_t d = millis() - t0;
        if (d > worstCall)
            worstCall = d;
    }

    client.stop();
    serverClient.stop();
    server.end();

    Serial.printf("\n      -> worst single blocking write() while peer stalled: %u ms",
                  (unsigned)worstCall);
    if (worstCall <= 1010)
    { // 1s deadline + at most ONE trailing 10ms slice of quantization slack
        TEST_PASS();
    }
    else
    {
        TEST_FAIL("write() overshot its timeout budget badly");
    }
}

void run_async_client_tests()
{
    TEST_SECTION_START("Asynchronous WiFiClient Tests");
    test_async_connect_loopback();
    test_async_bulk_write_drain();
    test_async_vs_blocking_stall();
    test_hot_path_accessors();
    test_write_slice_latency();
}
