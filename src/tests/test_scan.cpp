#include "test_config.h"
#include "test_scan.h"

static void test_sync_scan()
{
    TEST_CASE_START("Synchronous WiFi Scan");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int16_t n = WiFi.scanNetworks(false, true); // sync scan, show hidden
    if (n >= 0)
    {
        Serial.printf("\n    -> Found %d networks:\n", n);
        for (int i = 0; i < n && i < 5; ++i)
        {
            Serial.printf("       [%02d] SSID: %-20s | RSSI: %3d dBm | Ch: %2d | BSSID: %s\n",
                          i,
                          WiFi.SSID(i).c_str(),
                          WiFi.RSSI(i),
                          WiFi.channel(i),
                          WiFi.BSSIDstr(i).c_str());
        }
        if (n > 5)
        {
            Serial.printf("       ... and %d more networks\n", n - 5);
        }
        WiFi.scanDelete();
        TEST_PASS();
    }
    else
    {
        TEST_FAIL("Scan failed with negative return code");
    }
}

static void test_async_scan()
{
    TEST_CASE_START("Asynchronous WiFi Scan");
    WiFi.mode(WIFI_STA);
    delay(50);

    int16_t scanStatus = WiFi.scanNetworks(true); // async scan
    if (scanStatus != WIFI_SCAN_RUNNING && scanStatus != 0)
    {
        TEST_FAIL("Async scan did not start properly");
        return;
    }

    uint32_t startMs = millis();
    int16_t result = WIFI_SCAN_RUNNING;
    while (result == WIFI_SCAN_RUNNING && (millis() - startMs < 10000))
    {
        result = WiFi.scanComplete();
        delay(100);
    }

    if (result >= 0)
    {
        Serial.printf("\n    -> Async scan completed in %u ms, found %d networks\n",
                      (unsigned int)(millis() - startMs), result);
        WiFi.scanDelete();
        TEST_PASS();
    }
    else if (result == WIFI_SCAN_FAILED)
    {
        TEST_FAIL("Async scan reported failure");
    }
    else
    {
        TEST_FAIL("Async scan timed out");
    }
}

void run_scan_tests()
{
    TEST_SECTION_START("WiFi Scan Tests");
    test_sync_scan();
    test_async_scan();
}
