# PLAN — ماژولارسازی و تقسیم فایل‌های حجیم (WiFiClient / WiFiUdp / Test Async Client)

> مخاطب این سند: «من» (Claude) در چت‌های آینده. وقتی کاربر گفت «چیزهایی که داخل این فایل نوشتی رو پیاده‌سازی کن»،
> دقیقاً همین مراحل را بدون حدس‌زدن و بدون تغییر API اجرا کن.

---

## ۰. هدف (Goal) و قیود طلایی

**هدف:** فایل‌های زیر را به چند Translation Unit (فایل `.cpp`)ِ کوچک، ایزوله و از نظر معنایی تک‌مسئولیتی بشکنیم تا:
- هر فایل ≤ ۳۰۰ خط شود (قانون پروژه).
- هر فایل فقط یک «دغدغه» داشته باشد (Lifecycle / Connect / Options / Read / Write / Endpoints / RX / TX).
- کار کردن Agent روی هر بخش، خواندن و تست‌زدنِ مجزا راحت‌تر شود.

**قیود (اجباری — تخلف = ارور پروژه‌ی کاربر):**
1. **هیچ امضای عمومی، نام کلاس، نام متد، آرگومان پیش‌فرض یا مقدار بازگشتی تغییر نمی‌کند.** فایل‌های هدر
   `WiFiClient.h`، `WiFiUdp.h`، `test_async_client.h`، `test_config.h` **دست نمی‌خورند** (مگر دقیقاً همان‌طور که
   در همین سند «اجازه» داده شده، که در عمل هیچ تغییری در هدر کتابخانه لازم نیست).
2. **رفتار observable و خروجی تست‌ها صفر-تغییر** می‌ماند؛ فقط «مکان» تعریفِ توابع عوض می‌شود.
3. **هیچ افت سرعتی نباید به‌وجود بیاید.** چون همه‌ی متدهای جابه‌جا‌شده قبلاً هم out-of-line بودند،
   تقسیم‌شان صرفاً سازماندهی مجدد TU است و هیچ تغییر codegen یا hot-path ای ایجاد نمی‌کند.
4. PlatformIO همه‌ی `*.cpp`های داخل `lib/WiFi/src` و `src/tests` را خودکار کامپایل می‌کند؛ فایل جدید = کامپایل خودکار،
   بدون نیاز به تغییر `platformio.ini`.

**دستور بیلد:**
```bash
C:\Users\KAVEH\.platformio\penv\Scripts\platformio.exe run
```

---

## ۱. فهرست فایل‌های هدف

| فایل | خط فعلی | خروجی (تعداد فایل جدید) |
|------|---------|--------------------------|
| `lib/WiFi/src/WiFiClient.cpp` | ۶۷۴ | ۶ فایل |
| `lib/WiFi/src/WiFiUdp.cpp` | ۳۵۵ | ۲ فایل |
| `src/tests/test_async_client.cpp` | ۵۶۴ | ۴ فایل `.cpp` + ۱ هدر کمکی |

`WiFiClient.h`، `WiFiUdp.h`، `WiFiClientInternal.h`، `WiFiClientAsync.cpp` (async از قبل جدا شده) **بدون تغییر**.

---

## ۲. تقسیم `lib/WiFi/src/WiFiClient.cpp` (۶۷۴ خط → ۶ فایل)

### نقشه‌ی انتقال (فقط اسم توابع — مرجع خطوط فعلی داخل پرانتز)
توابع فعلی به‌ترتیب:

- `_configureSocket` (49–74)
- `EndpointCache::refresh` (76–102)
- `_rx()` (104–107)
- Ctor پیش‌فرض (110–115) / `(int fd)` (117–121) / Move ctor (124–134) / `operator=(WiFiClient&&)` (136–157) / dtor (159–162) / `operator=(const&)` (164–177)
- `stop()` (179–186)
- `connect(IP,uint16_t)` (188–191) / `connect(IP,uint16_t,int32_t)` (192–274) / `connect(host,uint16_t)` (276–279) / `connect(host,uint16_t,int32_t)` (281–289)
- `setSocketOption(option,char*,size_t)` (291–294) / `setSocketOption(level,option,const void*,size_t)` (296–304)
- `setTimeout` (306–325)
- `setOption` (327–330) / `getOption` (332–341)
- `setNoDelay` (343–347) / `getNoDelay` (349–354)
- `write(uint8_t)` (356–359)
- `read()` (361–374)
- `write(const uint8_t*,size_t)` (376–451)
- `write_P` (453–456) / `write(Stream&)` (458–475)
- `_handleBufferFailure` (477–481)
- `read(uint8_t*,size_t)` (483–496) / `peek()` (498–511) / `available()` (513–526) / `flush()` (530–537)
- `connected()` (539–593)
- `cacheEndpoints()` (595–598)
- `remoteIP(fd)` (600–607) / `remotePort(fd)` (609–616) / `remoteIP()` (618–622) / `remotePort()` (624–628)
- `localIP(fd)` (630–637) / `localPort(fd)` (639–646) / `localIP()` (648–652) / `localPort()` (654–658)
- `operator==` (660–663) / `fd()` (665–675)

### فایل‌های خروجی و تابع‌های هرکدام

#### 2.1 `WiFiClient.cpp` (بازمانده: Lifecycle / State core) — «فایل اصلی هم‌نام می‌ماند»
توابع: `_rx()`، تمام Ctorها/Dtorها/Operatorها، `stop()`، `_handleBufferFailure`، `operator==`، `fd()`.
شامل‌ها: `"WiFiClient.h"`, `"WiFiClientInternal.h"`, `"WiFi.h"`.
ثابت محلی: `static constexpr int WIFI_CLIENT_DEF_CONN_TIMEOUT_MS = 3000;` (فقط اینجا استفاده می‌شود؛ بالای فایل).

#### 2.2 `WiFiClientConnect.cpp` — اتصال + تنظیم سوکت هنگام هندشیک
توابع: `_configureSocket`، هر ۴ اورلود `connect(...)`.
ماکرو: `ROE_CFG` (تعریف فعلی خط ۳۶–۴۴) به این فایل منتقل شود و از `WiFiClient.cpp` حذف شود.
شامل‌ها: `"WiFiClient.h"`, `"WiFiClientInternal.h"`, `"WiFi.h"`, `<lwip/sockets.h>`, `<errno.h>`.
(نیازی به `<lwip/netdb.h>` نیست؛ `hostByName` از `WiFiGenericClass` در `WiFi.h` می‌آید.)

#### 2.3 `WiFiClientSocketOps.cpp` — گزینه‌های سوکت
توابع: `setSocketOption` (هر دو اورلود)، `setTimeout`، `setOption`، `getOption`، `setNoDelay`، `getNoDelay`.
شامل‌ها: `"WiFiClient.h"`, `"WiFiClientInternal.h"`, `<lwip/sockets.h>`, `<errno.h>`.

#### 2.4 `WiFiClientRead.cpp` — مسیر RX و connected
توابع: `read()`، `read(uint8_t*,size_t)`، `peek()`، `available()`، `flush()`، `connected()`.
ثابت محلی: `static constexpr uint32_t WIFI_CLIENT_CONN_CHECK_MS = 50;` (فقط در `connected()` استفاده می‌شود).
شامل‌ها: `"WiFiClient.h"`, `"WiFiClientInternal.h"`, `<lwip/sockets.h>`, `<errno.h>`.

#### 2.5 `WiFiClientWrite.cpp` — مسیر TX
توابع: `write(uint8_t)`، `write(const uint8_t*,size_t)`، `write_P`، `write(Stream&)`.
شامل‌ها: `"WiFiClient.h"`, `"WiFiClientInternal.h"`, `"WiFi.h"`, `<lwip/sockets.h>`, `<errno.h>`.

#### 2.6 `WiFiClientEndpoints.cpp` — introspection (آدرس‌ها/پورت‌ها)
توابع: `EndpointCache::refresh`، `cacheEndpoints`، `remoteIP()/remoteIP(fd)`، `remotePort()/remotePort(fd)`،
`localIP()/localIP(fd)`، `localPort()/localPort(fd)`.
شامل‌ها: `"WiFiClient.h"`, `"WiFiClientInternal.h"`, `<lwip/sockets.h>`.

### حذفِ کد مرده (گزارش‌شده، نه سکوت):
- `#define WIFI_CLIENT_MAX_WRITE_RETRY (10)` و `#define WIFI_CLIENT_SELECT_TIMEOUT_US (1000000)` فعلاً **هیچ استفاده‌ای
  در کد ندارند** (grep تأیید کرد فقط تعریف‌اند). در حین تقسیم **حذف شوند** و در گزارش به کاربر اعلام شوند.

### نکات ریسک:
- `_configureSocket` در هدر به‌صورت `static` اعلان شده؛ تعریف آن در `WiFiClientConnect.cpp` باید
  `int WiFiClient::_configureSocket(int fd, int timeout_ms)` باشد (بدون کلمه‌ی `static` در تعریف، مثل هر static member).
- `ROE_CFG` باید دقیقاً همراهش `_configureSocket` منتقل شود؛ جای دیگری استفاده نمی‌شود.
- کامنت بالای `WiFiClient.cpp` درباره‌ی «loop-to-full write» و «bulk RX bypass» یا هر کامنت tied به توابع منتقل‌شده
  باید همراه تابع مربوطه برود (نه رها شود).

---

## ۳. تقسیم `lib/WiFi/src/WiFiUdp.cpp` (۳۵۵ خط → ۲ فایل)

### 3.1 `WiFiUdp.cpp` (بازمانده: Lifecycle + TX)
توابع: Ctor، Dtor، `begin(IPAddress,uint16_t)`، `begin(uint16_t)`، `beginMulticast`، `stop()`،
`beginMulticastPacket()`، `beginPacket()`، `beginPacket(IPAddress,uint16_t)`، `beginPacket(const char*,uint16_t)`،
`endPacket()`، `write(uint8_t)`، `write(const uint8_t*,size_t)`، `remoteIP()`، `remotePort()`.
بالای فایل: `#undef write` (تنها در این فایل لازم است).

### 3.2 `WiFiUdpRx.cpp` — مسیر RX
توابع: `parsePacket()`، `available()`، `read()`، `read(unsigned char*,size_t)`، `read(char*,size_t)`،
`peek()`، `flush()`.
بالای فایل: `#undef read` (به‌دلیل وجود متدهایی به نام `read`).

### نکات ریسک:
- `#undef write` / `#undef read` موجود در فایل اصلی باید به فایل مناسب (TX→`write`، RX→`read`) منتقل شود،
  وگرنه ماکروهای lwip/POSIX ممکن است باعث خطای کامپایل شوند.
- همه‌ی عضوهای خصوصی (`udp_server`, `tx_buffer`, `rx_buffer`, `remote_ip`, ...) در یک کلاس مشترک‌اند؛
  تعریف متدها در دو TU هیچ مشکلی ایجاد نمی‌کند؛ هدر `WiFiUdp.h` دست نمی‌خورد.
- `tx_buffer`/`rx_buffer` بین دو فایل مشترک‌اند ولی فقط از طریق accessorهای داخل کلاس دیده می‌شوند؛ نیازی به هدر جدید نیست.

---

## ۴. تقسیم `src/tests/test_async_client.cpp` (۵۶۴ خط → ۴ `.cpp` + ۱ هدر)

### 4.0 هدر کمکی `src/tests/test_async_client_helpers.h` (جدید، داخل پشه‌ی tests)
محتوای مشترک که بین چند فایل تست استفاده می‌شود (به‌صورت `static inline`/`static constexpr` برای internal linkage):
- `static constexpr size_t kPayloadSize = 16384;`
- `static inline uint8_t pattern_byte(uint32_t i)` — بدنه‌ی فعلی (خط ۶–۹).
- `static inline int pump_connect(WiFiClient &c, uint32_t budgetMs, uint32_t &iterations, uint32_t &worstStallMs)` — بدنه‌ی فعلی (خط ۱۴–۳۲).
شامل‌ها: `#include "test_config.h"` (که خودش `Arduino.h` و `WiFi.h` را می‌آورد) + `#include "test_async_client.h"` در صورت لزوم.

### 4.1 هدر اعلان‌های تست `src/tests/test_async_client_cases.h` (جدید)
اعلان‌های extern برای ۶ تابع تست (هرکدام `void functionName();`) تا `run_async_client_tests` بتواند آن‌ها را
از TUهای مختلف صدا بزند.

### 4.2 `test_async_client_connect.cpp`
توابع: `test_async_connect_loopback` (خط ۳۴–۱۰۶) + `test_async_vs_blocking_stall` (خط ۲۳۱–۲۸۴).
شامل‌ها: `"test_config.h"`, `"test_async_client.h"`, `"test_async_client_helpers.h"`, `"test_async_client_cases.h"`.

### 4.3 `test_async_client_data.cpp`
توابع: `test_async_bulk_write_drain` (۱۰۸–۲۲۹) + `test_hot_path_accessors` (۲۸۶–۳۸۹).
شامل‌ها: `"test_config.h"`, `"test_async_client.h"`, `"test_async_client_helpers.h"`.

### 4.4 `test_async_client_write.cpp`
توابع: `test_write_slice_latency` (۳۹۱–۴۶۲) + `test_move_semantics` (۴۶۴–۵۵۳).
شامل‌ها: `"test_config.h"`, `"test_async_client.h"`, `"test_async_client_helpers.h"`.

### 4.5 `test_async_client.cpp` (بازمانده: فقط runner)
محتوای نهایی:
```cpp
#include "test_config.h"
#include "test_async_client.h"
#include "test_async_client_cases.h"

void run_async_client_tests()
{
    TEST_SECTION_START("Asynchronous WiFiClient Tests");
    test_async_connect_loopback();
    test_async_bulk_write_drain();
    test_async_vs_blocking_stall();
    test_hot_path_accessors();
    test_write_slice_latency();
    test_move_semantics();
}
```

### نکات ریسک:
- توابع تست دیگر `static` **نباشند** (چون از TU دیگر صدا زده می‌شوند)؛ اعلان‌ها در `test_async_client_cases.h`
  باید با تعریف‌ها دقیقاً مطابق باشند (همان نام، بدون `static`).
- `pattern_byte` و `pump_connect` چون فقط بین چند فایل مشترک‌اند، `static inline` در هدر کفایت می‌کند و
  باعث duplicate symbol نمی‌شود.
- امضای `pump_connect` (مرجع `WiFiClient&` + سه مرجع `uint32_t&`) بدون تغییر کپی شود تا همه‌ی call-siteها سازگار بمانند.

---

## ۵. چک‌لیست اجرا (بعد از هر تقسیم، به ترتیب)

1. **ساخت فایل‌ها:** فایل‌های جدید را با header گارد/license مناسب (برای `lib`، همان LGPL کنونی؛ برای `src/tests` ساده) بساز.
2. **جابه‌جایی دقیق:** تابع‌ها را با بدنِ byte-for-byte یکسان جابه‌جا کن؛ کامنت‌های tied را هم همراه ببر؛
   هیچ «بهبود» جانبی یا refactor غیرمرتبط انجام نده (قانون Surgical Changes).
3. **حذف مرجع قدیمی:** تابع جابه‌جا‌شده را از فایل مبدأ کاملاً حذف کن تا duplicate symbol نشود.
4. **شامل‌های لازم:** فقط هدر/شامل‌هایی که واقعاً استفاده می‌شوند در هر فایل جدید قرار بگیرند.
5. **بیلد:** `C:\Users\KAVEH\.platformio\penv\Scripts\platformio.exe run` → باید بدون خطا و بدون warning جدید تمام شود.
6. **بررسی تعداد خط:** مطمئن شو هر فایل خروجی ≤ ۳۰۰ خط است.

### دستور بررسی تعداد خط (برای تأیید، پس از اجرا):
```bash
find lib/WiFi/src src/tests -name "*.cpp" -o -name "*.h" | xargs wc -l | sort -rn | head -40
```

---

## ۶. راستی‌آزمایی نهایی (Verification = رفتار preserve شده)

- بیلد سبز (بدون خطا/هشدار جدید).
- تعداد خط همه‌ی فایل‌های هدف زیر ۳۰۰.
- تست‌های سریال: خروجی `run_async_client_tests` باید **دقیقاً همان ۶ تست** را با همان PASS/FAIL اجرا کند
  (تعداد تست و ترتیب ثابت). چون فقط مکان کد عوض شده، هیچ تغییری در خروجی انتظار نمی‌رود.
- هیچ هدر عمومی کتابخانه (`WiFiClient.h`, `WiFiUdp.h`) تغییر نکرده ← پروژه‌ی کاربر بدون ارور کامپایل می‌شود.

### یادآوری از قول کاربر (برای گزارش):
- سرعت و کاهش تاخیر مهم‌تر از حافظه است؛ اما این کارِ خاص، صرفاً «تقسیم» است نه «بهینه‌سازی»؛ لذا
  **انتظار بهبود/افت سرعت = صفر** را صادقانه گزارش کن (نه ادعای درصد). درصد بهبود مربوط به کارهای بهینه‌سازی بعدی است، نه این مرحله.