#include "test_config.h"
#include "test_sta.h"

static volatile uint8_t s_lastDisconnectReason = 0;

static void onStaWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED)
    {
        Serial.printf("\n    [Event] STA Connected to '%s' (Ch: %u)",
                      info.wifi_sta_connected.ssid, info.wifi_sta_connected.channel);
    }
    else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)
    {
        Serial.printf("\n    [Event] STA Got IP: %s",
                      IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
    }
    else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
    {
        uint8_t reason = info.wifi_sta_disconnected.reason;
        s_lastDisconnectReason = reason;
        Serial.printf("\n    [Event] STA Disconnected (Reason: %u -> %s)",
                      reason,
                      WiFi.disconnectReasonName((wifi_err_reason_t)reason));
    }
}

static void test_sta_connection()
{
    TEST_CASE_START("Station Connection & IP Acquisition");

    if (strcmp(TEST_WIFI_SSID, "YOUR_WIFI_SSID") == 0)
    {
        TEST_SKIP("Set TEST_WIFI_SSID in test_config.h to test router connection");
        return;
    }

    s_lastDisconnectReason = 0;

    // Register event listener for detailed diagnostic output
    wifi_event_id_t eventId = WiFi.onEvent(onStaWiFiEvent);

    // Reset IP configuration and ensure clean STA mode
    WiFi.config(IPAddress(), IPAddress(), IPAddress());
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false); // Disable aggressive auto-reconnect flood
    WiFi.setMinSecurity(WIFI_AUTH_OPEN);
    delay(300);

    Serial.printf("\n    -> Connecting to SSID: '%s' (ESP32 MAC: %s) ...",
                  TEST_WIFI_SSID, WiFi.macAddress().c_str());
    WiFi.begin(TEST_WIFI_SSID, TEST_WIFI_PASS);

    // Wait for connection with progress indicator
    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs < TEST_WIFI_TIMEOUT))
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    wl_status_t status = WiFi.status();
    WiFi.removeEvent(eventId);

    if (status == WL_CONNECTED && WiFi.isConnected())
    {
        Serial.printf("    -> Connected! IP: %s | Gateway: %s | Subnet: %s | DNS: %s | RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str(),
                      WiFi.subnetMask().toString().c_str(),
                      WiFi.dnsIP().toString().c_str(),
                      WiFi.RSSI());
        TEST_PASS();
    }
    else
    {
        Serial.printf("    -> Connection failed with status: %u (%s)\n",
                      status,
                      status == WL_NO_SSID_AVAIL ? "NO_SSID_AVAIL" : status == WL_CONNECT_FAILED ? "CONNECT_FAILED"
                                                                 : status == WL_CONNECTION_LOST  ? "CONNECTION_LOST"
                                                                 : status == WL_DISCONNECTED     ? "DISCONNECTED"
                                                                 : status == WL_IDLE_STATUS      ? "IDLE_STATUS"
                                                                                                 : "UNKNOWN");

        if (s_lastDisconnectReason == 5)
        { // WIFI_REASON_ASSOC_TOOMANY
            Serial.println("\n    [!] HINT for ASSOC_TOOMANY (Reason 5):");
            Serial.println("        Hotspot on phone rejected connection. Please check:");
            Serial.println("        1. 'Connected devices' limit in phone Hotspot settings (set to Unlimited).");
            Serial.println("        2. Check 'Blocklist' in phone Hotspot settings.");
            Serial.println("        3. Turn Hotspot OFF and back ON on the phone.");
        }
        TEST_FAIL("Station failed to obtain IP address within timeout");
    }
}

static void test_sta_disconnect_reconnect()
{
    TEST_CASE_START("Station Disconnect & Reconnect");

    if (strcmp(TEST_WIFI_SSID, "YOUR_WIFI_SSID") == 0)
    {
        TEST_SKIP("Skipped: requires active WiFi connection credentials");
        return;
    }

    if (!WiFi.isConnected())
    {
        TEST_SKIP("Skipped: not currently connected");
        return;
    }

    bool discOk = WiFi.disconnect(false, false);
    delay(300);

    if (!discOk || WiFi.isConnected())
    {
        TEST_FAIL("WiFi.disconnect() failed");
        return;
    }

    bool reconOk = WiFi.reconnect();
    uint8_t res = WiFi.waitForConnectResult(TEST_WIFI_TIMEOUT);

    if (reconOk && res == WL_CONNECTED)
    {
        TEST_PASS();
    }
    else
    {
        TEST_FAIL("WiFi.reconnect() failed");
    }
}

static void test_sta_static_ip_config()
{
    TEST_CASE_START("Station Static IP Configuration");
    IPAddress local_ip(192, 168, 1, 222);
    IPAddress gateway(192, 168, 1, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress dns(192, 168, 1, 1);

    bool cfgOk = WiFi.config(local_ip, gateway, subnet, dns);

    // Revert back to DHCP so subsequent operations are not affected
    WiFi.config(IPAddress(), IPAddress(), IPAddress());

    if (cfgOk)
    {
        TEST_PASS();
    }
    else
    {
        TEST_FAIL("WiFi.config() returned false");
    }
}

void run_sta_tests()
{
    TEST_SECTION_START("WiFi Station (STA) Tests");
    test_sta_connection();
    test_sta_disconnect_reconnect();
    test_sta_static_ip_config();
}
