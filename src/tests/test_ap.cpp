#include "test_config.h"
#include "test_ap.h"

static void test_softap_lifecycle() {
    TEST_CASE_START("SoftAP Lifecycle & Verification");
    WiFi.mode(WIFI_AP);
    delay(50);

    const char* apSSID = "ESP32_Test_AccessPoint";
    const char* apPass = "12345678";
    bool apStarted = WiFi.softAP(apSSID, apPass, 1, 0, 4);

    if (!apStarted) {
        TEST_FAIL("softAP() returned false");
        return;
    }

    IPAddress apIP = WiFi.softAPIP();
    String apMac = WiFi.softAPmacAddress();
    String currentSSID = WiFi.softAPSSID();
    uint8_t stationNum = WiFi.softAPgetStationNum();

    Serial.printf("\n    -> AP SSID: %s | IP: %s | MAC: %s | Stations: %u\n",
                  currentSSID.c_str(), apIP.toString().c_str(), apMac.c_str(), stationNum);

    if (apIP != IPAddress(0, 0, 0, 0) && apMac.length() == 17 && stationNum == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("SoftAP state parameters invalid");
    }
}

static void test_softap_custom_config() {
    TEST_CASE_START("SoftAP Custom IP Configuration");
    IPAddress local_ip(192, 168, 44, 1);
    IPAddress gateway(192, 168, 44, 1);
    IPAddress subnet(255, 255, 255, 0);

    bool cfgSuccess = WiFi.softAPConfig(local_ip, gateway, subnet);
    IPAddress currentIP = WiFi.softAPIP();

    if (cfgSuccess && currentIP == local_ip) {
        TEST_PASS();
    } else {
        TEST_FAIL("softAPConfig failed to apply custom subnet/IP");
    }

    // Teardown SoftAP
    WiFi.softAPdisconnect(true);
    delay(50);
}

void run_ap_tests() {
    TEST_SECTION_START("Access Point (SoftAP) Tests");
    test_softap_lifecycle();
    test_softap_custom_config();
}
