#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

#include <Arduino.h>
#include <WiFi.h>

// ==============================================================================
// WiFi Test Configuration
// ==============================================================================
// If you want to test connecting to your local WiFi router, enter credentials here:
#define TEST_WIFI_SSID     "redminote11"
#define TEST_WIFI_PASS     "kavehlololo"
#define TEST_WIFI_TIMEOUT  15000  // 15 seconds

// ==============================================================================
// Test Statistics & Helper Macros
// ==============================================================================
struct TestStats {
    uint32_t totalTests;
    uint32_t passedTests;
    uint32_t failedTests;
    uint32_t skippedTests;

    void reset() {
        totalTests = 0;
        passedTests = 0;
        failedTests = 0;
        skippedTests = 0;
    }
};

extern TestStats g_stats;

#define TEST_SECTION_START(name) \
    Serial.println(); \
    Serial.println("=================================================="); \
    Serial.printf("[TEST SUITE] %s\n", name); \
    Serial.println("==================================================")

#define TEST_CASE_START(name) \
    Serial.printf("  [*] Running: %-35s ... ", name); \
    g_stats.totalTests++

#define TEST_PASS() \
    do { \
        Serial.println("[ PASS ]"); \
        g_stats.passedTests++; \
    } while(0)

#define TEST_FAIL(msg) \
    do { \
        Serial.printf("[ FAIL ] -> %s\n", msg); \
        g_stats.failedTests++; \
    } while(0)

#define TEST_SKIP(reason) \
    do { \
        Serial.printf("[ SKIP ] -> %s\n", reason); \
        g_stats.skippedTests++; \
    } while(0)

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            TEST_FAIL(msg); \
            return; \
        } \
    } while(0)

#endif // TEST_CONFIG_H
