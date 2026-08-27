#include "test_config.h"
#include "test_async_client.h"
#include "test_async_client_helpers.h"

void test_write_slice_latency()
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

void test_move_semantics()
{
    TEST_CASE_START("Move constructor/assignment preserve live connection");

    WiFi.mode(WIFI_AP);
    delay(50);

    WiFiServer server(8095);
    server.begin();

    WiFiClient *client = new WiFiClient();
    if (!client->connectAsync(WiFi.softAPIP(), 8095))
    {
        delete client;
        server.end();
        TEST_FAIL("connectAsync refused to start");
        return;
    }
    uint32_t iters, worst;
    if (pump_connect(*client, 3000, iters, worst) != 1)
    {
        client->stop();
        delete client;
        server.end();
        TEST_FAIL("pollConnect failed");
        return;
    }

    // Move-construct from the heap object, then free the (now empty) shell.
    WiFiClient moved(std::move(*client));
    bool sourceEmptied = (client->fd() == -1 && !client->connected());
    delete client;

    if (!moved.connected() || moved.fd() < 0)
    {
        moved.stop();
        server.end();
        TEST_FAIL("moved-to client lost its connection");
        return;
    }

    // Move-assign into a second fresh object as well.
    WiFiClient assigned;
    assigned = std::move(moved);
    if (!assigned.connected() || assigned.fd() < 0)
    {
        assigned.stop();
        server.end();
        TEST_FAIL("move-assigned client lost its connection");
        return;
    }

    // The chain must still carry data end-to-end after two moves.
    const char *msg = "MOVE_OK";
    WiFiClient serverClient = server.available();
    uint32_t waitStart = millis();
    while (!serverClient && (millis() - waitStart < 1000))
    {
        serverClient = server.available();
        delay(5);
    }
    size_t sent = assigned.write((const uint8_t *)msg, strlen(msg));
    waitStart = millis();
    while (serverClient.available() < (int)strlen(msg) && (millis() - waitStart < 1000))
    {
        delay(5);
    }
    char rx[16] = {0};
    serverClient.read((uint8_t *)rx, strlen(msg));

    assigned.stop();
    serverClient.stop();
    server.end();

    if (sent == strlen(msg) && strcmp(rx, msg) == 0)
    {
        Serial.printf("\n      -> source emptied: %s | data survived two moves",
                      sourceEmptied ? "yes" : "NO");
        if (!sourceEmptied)
        {
            TEST_FAIL("move left the source holding the socket");
            return;
        }
        TEST_PASS();
    }
    else
    {
        TEST_FAIL("data did not survive the moves");
    }
}