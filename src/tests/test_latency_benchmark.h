#ifndef TEST_LATENCY_BENCHMARK_H
#define TEST_LATENCY_BENCHMARK_H

// Serial benchmark harness measuring small-message (MQTT/WS-like) round-trip
// latency and burst throughput. Independent of Optimal-Wifi WiFi specifics
// beyond the standard WiFiClient/WiFiServer API.

// Runs the latency + burst measurement suite and prints results over Serial.
// Must be called AFTER WiFi is connected/AP is up.
void run_mqtt_latency_benchmark();

#endif // TEST_LATENCY_BENCHMARK_H
