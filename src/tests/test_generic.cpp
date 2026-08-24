#include "test_config.h"
#include "test_generic.h"

static volatile bool s_eventTriggered = false;
static void testWiFiEventCallback(arduino_event_id_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_START || event == ARDUINO_EVENT_WIFI_READY) {
        s_eventTriggered = true;
    }
}

static void test_wifi_modes() {
    TEST_CASE_START("WiFi Mode Switching");
    bool modeOff = WiFi.mode(WIFI_OFF);
    bool modeSta = WiFi.mode(WIFI_STA);
    wifi_mode_t curMode = WiFi.getMode();
    if (modeSta && curMode == WIFI_STA) {
        TEST_PASS();
    } else {
        TEST_FAIL("Failed to switch to WIFI_STA mode");
    }
}

static void test_mac_and_hostname() {
    TEST_CASE_START("MAC Address & Hostname");
    String mac = WiFi.macAddress();
    if (mac.length() != 17) {
        TEST_FAIL("Invalid MAC string format");
        return;
    }

    const char* testHost = "Optimal-ESP32";
    WiFi.setHostname(testHost);
    const char* curHost = WiFi.getHostname();
    if (curHost && strcmp(curHost, testHost) == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("Hostname mismatch");
    }
}

static void test_tx_power_and_sleep() {
    TEST_CASE_START("TX Power & Sleep Configuration");
    WiFi.setTxPower(WIFI_POWER_17dBm);
    wifi_power_t power = WiFi.getTxPower();

    WiFi.setSleep(WIFI_PS_MIN_MODEM);
    wifi_ps_type_t sleepMode = WiFi.getSleep();

    if (power > 0 && sleepMode == WIFI_PS_MIN_MODEM) {
        TEST_PASS();
    } else {
        TEST_FAIL("Power or Sleep configuration failed");
    }
}

static void test_network_calculations() {
    TEST_CASE_START("Network Calculations");
    IPAddress ip(192, 168, 1, 50);
    IPAddress subnet(255, 255, 255, 0);

    IPAddress netId = WiFiGenericClass::calculateNetworkID(ip, subnet);
    IPAddress broadcast = WiFiGenericClass::calculateBroadcast(ip, subnet);
    uint8_t cidr = WiFiGenericClass::calculateSubnetCIDR(subnet);

    if (netId == IPAddress(192, 168, 1, 0) &&
        broadcast == IPAddress(192, 168, 1, 255) &&
        cidr == 24) {
        TEST_PASS();
    } else {
        TEST_FAIL("Subnet/Broadcast calculation error");
    }
}

static void test_events_registration() {
    TEST_CASE_START("Event Listener Registration");
    s_eventTriggered = false;
    wifi_event_id_t eventId = WiFi.onEvent(testWiFiEventCallback);

    // Trigger STA start
    WiFi.mode(WIFI_OFF);
    delay(50);
    WiFi.mode(WIFI_STA);
    delay(100);

    WiFi.removeEvent(eventId);

    if (s_eventTriggered) {
        TEST_PASS();
    } else {
        TEST_FAIL("WiFi event callback was not invoked");
    }
}

void run_generic_tests() {
    TEST_SECTION_START("Generic & System Configuration Tests");
    test_wifi_modes();
    test_mac_and_hostname();
    test_tx_power_and_sleep();
    test_network_calculations();
    test_events_registration();
}
