#include <Arduino.h>
#include "test_config.h"
#include "test_generic.h"
#include "test_scan.h"
#include "test_ap.h"
#include "test_sta.h"
#include "test_sockets.h"

TestStats g_stats;

static void print_banner() {
    Serial.println();
    Serial.println("**************************************************");
    Serial.println("*           OPTIMAL-WIFI TEST SUITE              *");
    Serial.println("*      ESP32 WiFi Core Verification Harness      *");
    Serial.println("**************************************************");
    Serial.printf("ESP32 Chip Model: %s | Revision: %d | Cores: %d\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
    Serial.printf("Free Heap: %u bytes | Flash Size: %u MB\n",
                  ESP.getFreeHeap(), ESP.getFlashChipSize() / (1024 * 1024));
    Serial.println("**************************************************");
}

static void print_summary() {
    Serial.println();
    Serial.println("==================================================");
    Serial.println("                 TEST SUMMARY                     ");
    Serial.println("==================================================");
    Serial.printf("  Total Tests  : %u\n", g_stats.totalTests);
    Serial.printf("  Passed Tests : %u\n", g_stats.passedTests);
    Serial.printf("  Failed Tests : %u\n", g_stats.failedTests);
    Serial.printf("  Skipped Tests: %u\n", g_stats.skippedTests);
    Serial.println("--------------------------------------------------");

    if (g_stats.failedTests == 0) {
        Serial.println("  >>> RESULT: ALL EXECUTED TESTS PASSED! <<<");
    } else {
        Serial.println("  >>> RESULT: SOME TESTS FAILED! <<<");
    }
    Serial.println("==================================================");

    Serial.println("\n[WiFi Diagnostics Info]");
    WiFi.printDiag(Serial);
    Serial.println("==================================================");
}

void setup() {
    Serial.begin(115200);
    delay(2000); // Allow Serial monitor to attach

    g_stats.reset();
    print_banner();

    // Run modular test suites
    run_generic_tests();
    run_scan_tests();
    run_ap_tests();
    run_sta_tests();
    run_socket_tests();

    print_summary();
}

void loop() {
    // Keep idle after tests complete
    delay(1000);
}
