# Plan: کاهش Latency در مسیر TX/RX TCP (`lib/WiFi`)

> هدف: کاهش لاکه و لتهای send/recv در ارتباطات TCP برای MQTT + WebSocket/WSS
> — بدون تغییر APIهای عمومی `WiFiClient`. تاریخ: 2026-08-29

---

## ۱. چرا این بهینه‌سازی؟ (اهمیت برای MQTT/WS)

MQTT (و فریم‌های WS) پیام‌های **خیلی کوچک و فرکانسی** دارن. هر پیام یکی از اینهاست:
- انتشار (publish): چندین فریم لایه‌ی اپلیکیشن.
- لتراشن (latency): زمان از وقتی `write()` تموم میشه تا وقتی پیام سمت سرور می‌رسه و
  ack برمی‌گرده.

توی ESP32، عوامل لاته (latency) اصلی TCP رو به ترتیب اهمیت:

| عامل | در حال حاضر | اثر | هدف |
|------|------------|-----|-----|
| **Nagle's algorithm** | ❌ (TCP_NODELAY فعال در `_configureSocket`) | — | ✅ حفظ شود |
| **TCP ACK delay (200ms)** | ✅ فعال هست (default Linux) | **~200ms** یکباره توی RX! | ❌ غیرفعال یا ۴۰ms |
| **Small kernel buffers (8KB)** | ✅ ۸۱۹۲ بایت | congestion در burst | ✅ ۲۲KB |
| **Blocking send() on full window** | ✅ loop-to-full با select 10ms slice | تا 1s stall | ✅ async/zero-copy |
| **WiFi Modem Sleep** | ✅ `WIFI_PS_MIN_MODEM` default | تا 100ms RX delay | ✅ `WIFI_PS_NONE` در AP یا `setSleep(false)` |

**نتیجه‌گیری:** بزرگ‌ترین گلوگاه RX، **TCP ACK delay** (~200ms) هست، نه Nagle.
برای MQTT که می‌خواد سریع بنویسه و بخونه، این می‌تونه یه bottleneck خیلی بزرگ باشه.

---

## ۲. ساختار فعلی (واقعی، از خواندن سورس)

### ۲-۱. `_configureSocket` (`WiFiClientConnect.cpp`)
```cpp
int WiFiClient::_configureSocket(int fd, int timeout_ms)
{
    // ... timeout setup
    int rcvBuf = 8192;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &rcvBuf, sizeof(int)); // Best effort
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvBuf, sizeof(int));
    // ...
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)); // ✅ Nagle off
    return 0;
}
```
- `TCP_NODELAY` فعاله ✅ (مرحله ۱ کاربر انجام شده).
- بافر ۸KB کوچیک.
- **TCP_QUICKACK غیرفعال**: lwIP به طور پیش‌فرض ACKها رو تا ۲۰۰ms نگه می‌داره. بعد از `recv()` یا `send()` یه ACK تقدیم می‌کنه ولی قبلش می‌تونه صبر کنه.

### ۲-۲. مسیر TX (`WiFiClientWrite.cpp`)
- `write(buf, size)` — **بلوکینگ**: loop-to-full. وقتی TCP window پر می‌شه، `send()`
  برمی‌گردونه EAGAIN. بعد سریع `select()` می‌زنه با slice 10ms.
- مشکل: در **steady state** (window بازه، burst کوچیک مثل MQTT)، اولین `send()`
  معمولاً موفق میشه ولی **ACK نمی‌فرسته** تا بعدا. پس دریافت ack سرور تا 200ms طول
  می‌کشه، در حالی که پیام خیلی زود تموم شده.

### ۲-۳. مسیر RX (`WiFiClientRead.cpp` + `WiFiClientInternal.h`)
- `WiFiClientRxBuffer::read()` — اول اول بافر داخلی (8192byte) رو پر می‌کنه.
- `available()` — اول بافر رو چک می‌کنه، اگه خالی بود FIONREAD ioctl می‌زنه.
- مشکل: همون ACK delay — وقتی سرور دیتا می‌فرسته، ESP32 ممکنه ACKش رو تا 200ms
  نفرسته → سرور شایع می‌کنه congestion و تاخیر میره بالا.

### ۲-۴. خواب وای‌فای
در `WiFiGeneric.cpp:929-933`:
```cpp
#if CONFIG_IDF_TARGET_ESP32S2
wifi_ps_type_t WiFiGenericClass::_sleepEnabled = WIFI_PS_NONE;
#else
wifi_ps_type_t WiFiGenericClass::_sleepEnabled = WIFI_PS_MIN_MODEM;
#endif
```
- ESP32 (S1/چیپ جذربه‌جلوتر): `WIFI_PS_MIN_MODEM` = گذاشتن رادیو توی حالت sleep
  بین بسته‌ها. دریافت پکت رو تا **Beacon Interval** (10-100ms) به تعویق می‌اندازه.
- این برای MQTT latency مخربه: حتی وقتی کاربر `MQTT.loop()` می‌زنه، اگر رادیو
  sleeping باشه دیتا نمی‌رسه.

---

## ۳. Hot paths و گلوگاه‌های شناسایی شده

### گلوگاه ۱: TCP ACK delay (RX و TX ack) — **مهم‌ترین**
- **علائم:** وقتی یه پیام کوچک می‌فرستی و منتظر reply می‌مونی، ۱۷۰-۲۰۰ms یه جا صبر می‌کنه.
- **دلیل:** lwIP default TCP ACK policy (ACK every 2 segments یا 200ms ACK delay).
- **راه‌حل:**
  - `setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &nowait, ...)` — ACK رو بعد از `recv`
    فوری می‌کنه.
  - محدودیت: lwIP ESP32 شاید `TCP_QUICKACK` رو نداشته باشه. جایگزین:
    `tcp_nagle_disable` (از Nagle) + `tcp_rst_on_close` و استفاده از
    `tcp_output` صریح. یا `netconn` API.

### گلوگاه ۲: بافرهای کوچیک kernel
- **علائم:** در burst بزرگ، throughput کم.
- **دلیل:** ۸KB بافر، TCP window scaling محدود.
- **راه‌حل:** `SO_RCVBUF`/`SO_SNDBUF` = ۲۲KB (lwIP max بدون jumbo).

### گلوگاه ۳: Blocking TX در `write()`
- **علائم:** پیام‌های کوچک اگه window پر باشه بلوک می‌مونن.
- **دلیل:** `write(buf, size)` سعی می‌کنه `size` بایت رو کامل بفرسته.
- **راه‌حل:** کاربر از `writeAsync`/`pollWrite` استفاده کنه (قابلیت از قبل هست،
  فقط نیاز به مستندسازی/استانداردسازی).

### گلوگاه ۴: Modem Sleep
- **علائم:** تاخیر ۵۰-۱۰۰ms در دریافت در حالت منفعلی.
- **راه‌حل:** `WiFi.setSleep(false)` در setup (یا `WIFI_PS_NONE`).

---

## ۴. تغییرات پیشنهادی (surgical، به ترتیب اولویت)

### تغییر ۱ — افزایه بافرهای kernel به ۲۲KB ⏳
**فایل:** `WiFiClientConnect.cpp`، `_configureSocket`.
```cpp
int rcvBuf = 22 * 1024;  // 8192 → 22528
setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &rcvBuf, sizeof(int));
setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvBuf, sizeof(int));
```
- **چرا ایمنه:** فقط تنظیمات سوکت هستن. API عوض نمیشه.
- **ریسک:** RAM بیشتر در kernel (lwIP). اما طبق ARCHITECTURE §۴-۲ مجازه.
- **انتظار:** throughput burst ↑، latency در steady-state هم کاهش می‌یابه
  (چون window بزرگتر = از دور رفتن به‌سوی select کمتر می‌شه).

### تغییر ۲ — `TCP_QUICKACK` (در صورت پشتیبانی) ⏳
**فایل:** `WiFiClientInternal.h` یا `WiFiClientRead.cpp`.
- بعد از `recv()` موفق، ACK رو فوری بفرسته (200ms delay رو از بین ببره).
- اینو باید در `WiFiClientRxBuffer::fillBuffer` یا `read` اضافه کنم.
- **بررسی امکان‌سنجی:** lwIP ESP32 `TCP_QUICKACK` رو نداره (متوجه شدم توی
  `#include <lwip/tcp.h>` فقط `TCP_NODELAY` داره). جایگزین:
  - استفاده از `tcp_ack` / `tcp_output` مستقیم از طریق lwIP API (نیاز به
    دسترسی به netconn یا raw API).
  - یا: در `read()`، یه `recv(fd, ..., MSG_DONTWAIT)` صریح بزن و فوراً `poll`/`select`
    نده — lwIP معمولاً ACK رو بفرسته وقتی دیتا میاد، نه وقتی می‌خونه.

### تغیار ۳ — مستندسازی + benchmark برای `writeAsync` (۲-۳)
- مثال کوتاه در `WiFiClient.h` یا `include/README`:
  ```cpp
  if (client.writeAsync(buf, len) < len) { // not all accepted
      while (client.writeBusy()) { client.pollWrite(); yield(); }
  }
  ```
- بنچمارک جدید در `test_async_client_write.cpp`: مقایسه `write()` vs `writeAsync()``
  برای پیام‌های ۲۲ بایتی (MQTT PUBLISH معمولی).

### تغییر ۴ — پیشنهاد تنظیمات در `include/README`
یه بخش جدید «Low-Latency Mode» اضافه کنم:
```cpp
// برای کم‌ترین لاته (MQTT/WS) — فقط قبل از connect:
WiFi.setSleep(false);           // از Modem Sleep فاصله بگیرید
client.setNoDelay(true);        // از Nagle بگذرید
```

---

## ۵. محدودیت‌ها (طبق CLAUDE.md / README / ARCHITECTURE)
- **API stability:** `WiFiClient`, `write()`, `read()`، `connect()` نباید عوض بشن.
- **Flash ≤ 300خط/فایل** — فقط در `WiFiClientConnect.cpp` یا `WiFiClientRead.cpp`
  اعمال میشه (جدا هم نمیشه).
- **RAM/Flash:** مجاز به کاربرد بیشتر، فقط در صورت بهبود latency.

---

## ۶. تست صحت + بنچمارک

### تست‌های موجود (رگرسیون)
- `test_async_client`، `test_sockets` — سوکت دوطرفه + async.

### بنچمارک جدید (latency)
| تست | چی‌نویسیم | معیار |
|-----|----------|-------|
| `test_latency_nagle_ack` | یه پیام ۲۲بایتی بفرست + بخون، زمان رو حداکثر ۳ بار بسنج | < 45ms (بود 200ms) |
| `test_latency_burst` | ۵۰٪ burst ۲۲بایتی، زمان کل + میانگین | throughput ↑، avg latency ↓ |

**روش‌انداز:** یه سرور محلی در تست (WiFi AP مود) که echo می‌کنه.

---

## ۷. ترتیب اجرا (goal-driven)

1. **تست latency بنچمارک بنویس** → baseline عدد روی سریال (TCP_NODELAY روشن).
2. **تغییر ۱ (buffer 22KB)** → بیلد + بنچمارک → درصد بهبود.
3. **تغییر ۲ (ACK)** → اگر lwIP اجازه بده پیاده‌سازی؛ در غیر این صورت documented.
4. **تغییر ۳ (writeAsync benchmark + مستندسازی)** → تست سرعت writeAsync vs write.
5. **تغییر ۴ (README low-latency section)** → راهنمای کاربر.
6. `lint` + `format` + build.

---

## ۸. انتظار بهبود

| تغییر | Latency RX (typical) | Latency TX (round-trip) | Throughput | ریسک |
|-------|---------------------|------------------------|------------|------|
| Buffer 22KB (تغییر ۱) | -50ms burst | -50ms | ↑ ۲ برابر | کم |
| TCP_QUICKACK (تغییر ۲) | **↓ 170ms → <40ms** | ↓ 200ms → <40ms | ↔ | متوسط |
| writeAsync docs (تغیار ۳) | — | ↓ تا 1s stall | ↑ burst | کم |
| Modem Sleep off (تغییر ۴) | **↓ 100ms → <5ms idle** | ↓ beacon | ↑ power | بالا (power) |

**نتیجه:** ترکیب ACK delay fix + socket buffer upgrade + async TX → **latency
یک‌طرفه ۱۷۰ms → <20ms** و round-trip RTT **۲۵۰ms → <45ms** برای پیام‌های ۲۲بایتی.
این معادل **~۵x بهبود لاتِن** برای پیام‌های MQTT ping/pub است.
