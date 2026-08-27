#include "test_config.h"
#include "test_async_client.h"
#include "test_async_client_helpers.h"
#include "test_async_client_cases.h"

void test_async_connect_loopback()
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

void test_async_vs_blocking_stall()
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