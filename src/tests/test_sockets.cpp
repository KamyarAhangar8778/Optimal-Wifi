#include "test_config.h"
#include "test_sockets.h"

static void test_tcp_server_client_loopback() {
    TEST_CASE_START("TCP Server & Client Loopback");

    // Setup AP for local communication
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32_Socket_Test", "12345678");
    delay(100);

    IPAddress hostIP = WiFi.softAPIP();
    uint16_t port = 8088;

    WiFiServer server(port);
    server.begin();

    WiFiClient client;
    int conn = client.connect(hostIP, port, 3000);
    if (!conn) {
        server.end();
        TEST_FAIL("WiFiClient could not connect to local WiFiServer");
        return;
    }

    WiFiClient serverClient = server.available();
    uint32_t waitStart = millis();
    while (!serverClient && (millis() - waitStart < 1000)) {
        serverClient = server.available();
        delay(10);
    }

    if (!serverClient) {
        client.stop();
        server.end();
        TEST_FAIL("Server did not accept incoming connection");
        return;
    }

    // Client -> Server
    const char* clientMsg = "HELLO_FROM_CLIENT";
    client.print(clientMsg);
    client.flush();

    waitStart = millis();
    while (serverClient.available() < (int)strlen(clientMsg) && (millis() - waitStart < 1000)) {
        delay(10);
    }

    String receivedOnServer = serverClient.readStringUntil('\0');
    if (receivedOnServer.indexOf(clientMsg) == -1) {
        client.stop();
        serverClient.stop();
        server.end();
        TEST_FAIL("Server received incorrect data from client");
        return;
    }

    // Server -> Client Echo
    const char* serverMsg = "REPLY_FROM_SERVER";
    serverClient.print(serverMsg);
    serverClient.flush();

    waitStart = millis();
    while (client.available() < (int)strlen(serverMsg) && (millis() - waitStart < 1000)) {
        delay(10);
    }

    String receivedOnClient = client.readStringUntil('\0');

    client.stop();
    serverClient.stop();
    server.end();

    if (receivedOnClient.indexOf(serverMsg) != -1) {
        TEST_PASS();
    } else {
        TEST_FAIL("Client received incorrect data from server");
    }
}

static void test_udp_send_receive_loopback() {
    TEST_CASE_START("UDP Packet Send & Receive");

    WiFi.mode(WIFI_AP);
    delay(50);
    IPAddress hostIP = WiFi.softAPIP();
    uint16_t udpPort = 9099;

    WiFiUDP udp;
    if (!udp.begin(udpPort)) {
        TEST_FAIL("WiFiUDP begin failed");
        return;
    }

    const char* udpPayload = "OPTIMAL_WIFI_UDP_TEST_PAYLOAD";
    udp.beginPacket(hostIP, udpPort);
    udp.write((const uint8_t*)udpPayload, strlen(udpPayload));
    int endRes = udp.endPacket();

    if (endRes <= 0) {
        udp.stop();
        TEST_FAIL("WiFiUDP endPacket failed");
        return;
    }

    uint32_t startMs = millis();
    int packetSize = 0;
    while ((packetSize = udp.parsePacket()) == 0 && (millis() - startMs < 1000)) {
        delay(10);
    }

    if (packetSize <= 0) {
        udp.stop();
        TEST_FAIL("WiFiUDP parsePacket timed out");
        return;
    }

    char rxBuffer[64] = {0};
    int len = udp.read(rxBuffer, sizeof(rxBuffer) - 1);
    udp.stop();

    if (len > 0 && strcmp(rxBuffer, udpPayload) == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("UDP payload mismatch");
    }
}

void run_socket_tests() {
    TEST_SECTION_START("TCP & UDP Socket Tests");
    test_tcp_server_client_loopback();
    test_udp_send_receive_loopback();
}
