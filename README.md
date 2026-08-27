# Optimal-Wifi

An optimized, drop-in replacement for the standard Arduino **WiFi** library targeting the
ESP32. Built to maximize throughput and minimize latency while remaining fully
API-compatible with the original library.

## Highlights

- **Drop-in compatible** — same public class names, methods, and signatures as the stock
  `WiFi` / `WiFiClient` / `WiFiUDP` / `WiFiServer` / `WiFiMulti` / `WiFiScan` APIs.
- **Non-blocking async connect/write** — `connectAsync()`, `pollConnect()`, `writeAsync()`,
  `pollWrite()` let `loop()` keep running while a TCP handshake or a bulk transfer drains.
- **Zero-copy TX views** — pending async writes reference the caller's buffer directly
  (no heap copy).
- **Batch-fill RX buffering** — a large static receive buffer bypasses the internal buffer
  on bulk reads, removing a memcpy per chunk.
- **Hot-path endpoint caching** — `remoteIP()`/`localIP()`/port accessors resolve once and
  then serve from RAM, not a syscall.
- **RAII socket management** — leak-proof descriptors on every error path.
- **API stability** — the project's golden rule: public signatures never change, so your
  existing sketch compiles unchanged.

## Project layout

```
Optimal-Wifi/
├── lib/WiFi/src/        # The optimized WiFi library (the optimization target)
├── include/             # Optional custom acceleration tools (Timers, Events,
│                        #   Async Future, StaticArray/StaticHashMap/SmallVector, ...)
├── src/tests/           # Serial test harness + benchmarks
├── src/main.cpp         # Test entry point (runs all suites, then benchmarks)
├── plans/               # Optimization & refactor plans (implementation notes)
└── platformio.ini       # Build configuration
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for a full map of the system and the development
golden rules.

## Requirements

- [PlatformIO](https://platformio.org/) (with an ESP32 board profile, e.g. `esp32dev`)
- An ESP32 development board

## Build & flash

```bash
# From the project root:
C:\Users\KAVEH\.platformio\penv\Scripts\platformio.exe run
```

Upload to a board and open the serial monitor (115200 baud) to watch the test suite:

```bash
C:\Users\KAVEH\.platformio\penv\Scripts\platformio.exe run --target upload
C:\Users\KAVEH\.platformio\penv\Scripts\platformio.exe device monitor
```

The firmware prints a self-verification harness on boot: a banner, then the modular test
suites (generic, scan, AP, STA, sockets, async client), a pass/fail summary, and finally a
set of performance benchmarks.

## Tests

`src/main.cpp` runs six verification suites backed by a lightweight serial-reporter macro
set (`src/tests/test_config.h`):

- **Generic** — mode switching, MAC/hostname, TX power, sleep, event registration
- **Scan** — synchronous and asynchronous network scan
- **AP** — SoftAP lifecycle and custom IP configuration
- **STA** — station connect, DHCP, reconnect, static IP
- **Sockets** — TCP client/server loopback and UDP send/receive
- **Async client** — non-blocking connect/write, endpoint caching, move semantics, and
  async-vs-blocking stall benchmark

`test_benchmark.cpp` reports baseline throughput/latency numbers used to quantify each
optimization.

> **Wi-Fi credentials:** the STA tests read `TEST_WIFI_SSID` / `TEST_WIFI_PASS` from
> `src/tests/test_config.h`. Set them to your own router before flashing.

## Custom acceleration tools (`include/`)

The library also ships a set of optional, header-only tools under `include/` — zero-heap
containers, hardware/software timers, an async Future/Deferred system, and fast event
dispatch. They live under the `uniuno` namespace and are documented in
[`include/README`](include/README). They are optional: the core WiFi library does not
depend on them.

## License

This project builds on the ESP32 Arduino WiFi library (LGPL-2.1). New work inherits the
same terms; see the per-file headers for details.