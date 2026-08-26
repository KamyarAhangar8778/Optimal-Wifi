# PLAN — بهینه‌سازی و ارتقای کارایی WiFiScan (WiFiScan.h / WiFiScan.cpp)

> سند گام‌به‌گام بهینه‌سازی ماژول اسکن وای‌فای (`WiFiScan`) برای دستیابی به حداکثر سرعت، حذف تخصیص دینامیک حافظه و بهبود زمان واکنش.

---

## ۱. وضعیت فعلی و خط مبنا (Baseline Metrics)

نتایج بنچمارک فعلی برای `WiFiScan`:
- **زمان اسکن کامل (Full Scan, Channels 1-13):** ۶٬۸۱۹ ms
- **زمان اسکن یک‌کاناله (Ch 11):** ۲۰۳ ms
- **تخصیص Heap در طول اسکن:** `new wifi_ap_record_t[N]` — چندین صد تا چند صدهزار بایت بسته به تعداد APها

---

## ۲. گلوگاه‌های شناسایی‌شده (Bottlenecks & Root Causes)

1. **تخصیص پویه در `_scanDone()`:**
   - در خط ۱۱۳ `WiFiScanScan.cpp`، برای ذخیره نتایج اسکن از `new wifi_ap_record_t[N]` استفاده می‌شود.
   - این یک بلوک بزرگ از رم به اندازه `N * sizeof(wifi_ap_record_t)` اختصاص می‌دهد.
2. **عدم استفاده از لایه abstraction استاتیک:**
   - در حال حاضر `_scanResult` یک `void*` ساده است و نیاز به `reinterpret_cast` دارد.
   - با استفاده از `StaticArray` از `include/Optimization/` می‌توانیم این لایه را جایگزین کنیم.
3. **استفاده از `String` در `BSSIDstr()` و `SSID()`:**
   - این عملیات می‌تواند منجر به تخصیص دینامیک شود.
   - با استفاده از یک آرایه‌ی `char` استاتیک یا استفاده مستقیم از `wifi_ap_record_t`، این هزینه قابل حذف است.
4. **عدم محدود کردن حداکثر نتایج اسکن:**
   - در حال حاضر `_scanCount` می‌تواند بزرگ باشد.
   - افزودن یک `MAX_SCAN_RESULTS` به‌عنوان حداکثر ثابت، می‌تواند انعطاف‌پذیری بیشتری بدهد.

---

## ۳. اهداف عملکردی (Target Goals)

| شاخص | مقدار فعلی (Baseline) | مقدار هدف پس از بهینه‌سازی | درصد بهبود هدف |
|------|----------------------|---------------------------|----------------|
| **زمان اسکن یک‌کاناله** | ۲۰۳ ms | **زیر ۱۵۰ ms** | **+۲۵٪ تا +۳۵٪** |
| **تخصیص Heap در طول اسکن** | `new wifi_ap_record_t[N]` | **صفر (Zero Heap Allocation)** | **۱۰۰٪ حذف تکه‌تکه شدن رم** |
| **زمان BSSIDstr()** | ~۲۰ μs | **زیر ۵ μs** | **+۷۵٪** |

---

## ۴. مراحل اجرایی گام‌به‌گام (Step-by-Step Execution)

### گام ۱: جایگزینی تخصیص پویه با آرایه استاتیک

- در `WiFiScan.h` یک ثابت به نام `MAX_SCAN_RESULTS` تعریف کنید (مثلاً ۲۰).
- یک آرایه استاتیک از نوع `wifi_ap_record_t` به نام `_scanResultStatic[MAX_SCAN_RESULTS]` به‌عنوان عضو استاتیک کلاس اضافه کنید.
- در `WiFiScan.cpp`، `void* WiFiScanClass::_scanResult` را حذف کنید.
- در `_scanDone()`، به جای `new wifi_ap_record_t[N]`، مستقیماً `esp_wifi_scan_get_ap_records` را در `_scanResultStatic` صدا بزنید.
- مقدار `_scanCount` را با `min(WiFiScanClass::_scanCount, MAX_SCAN_RESULTS)` محدود کنید.

### گام ۲: بهینه‌سازی `BSSIDstr()` و `SSID()`

- `BSSIDstr()` را به‌گونه‌ای بازنویسی کنید که از یک آرایه‌ی `char` استاتیک استفاده کند.
- `SSID()` را می‌توانید بدون تغییر بگذارید، زیرا Arduino `String` در پس‌زمینه تخصیص می‌دهد، اما در اینجا یک کپی معمولی است و می‌توان آن را در محل استفاده بهینه کرد.

### گام ۳: بهینه‌سازی `_getScanInfoByIndex`

- این تابع بازگرداننده یک `void*` است.
- در عوض، یک رابط (Interface) واضح برای دسترسی به `wifi_ap_record_t` می‌سازیم.
- از `StaticArray.data()` یا `StaticArray[idx]` برای دسترسی مستقیم استفاده کنید.

### گام ۴: بهینه‌سازی `scanDelete()`

- در عوض از `delete[]`، فقط یک فلگ بزنید که بلوک استاتیک هنوز استفاده نمی‌شود.
- یا می‌توانید `_scanCount = 0` بزنید و از `clear()` در `StaticArray` استفاده کنید.

### گام ۵: افزودن یک بنچمارک برای `WiFiScan`

- یک تابع جدید به نام `bench_scan_speed()` در `src/tests/test_benchmark.cpp` اضافه کنید.
- این تابع زمان اسکن یک‌کاناله و زمان صدای `BSSIDstr()` را برای چند AP اول اندازه‌گیری می‌کند.
- خروجی سریال آن شبیه به این باشد:
  ```
  [BENCH 1] WiFi Scan Performance:
    -> Single-Channel Scan (Ch 11): 145 ms (Found 7 APs)
    -> BSSIDstr() x 10 calls: 12 us total (1.2 us avg)
  ```

### گام ۶: تست، بیلد و ثبت بنچمارک جدید

- اجرای بیلد با `platformio run`.
- اجرای همه تست‌ها برای اطمینان از صحت ۱۰۰٪ APIها.
- مقایسه نتایج بنچمارک با خط مبنا و محاسبه درصد واقعی افزایش سرعت.

---

## ۵. تضمین عدم شکستن APIها (Strict API Compatibility)

- تمام امضاها (`signatures`) متدهای عمومی (`SSID()`, `BSSIDstr()`, `RSSI()`, `encryptionType()`, `channel()`) بدون هیچ‌گونه تغییر باقی می‌مانند.
- `scanNetworks()`، `scanComplete()` و `scanDelete()` رفتارشان یکسان باقی می‌ماند.
- کلاس والد (`WiFiScanClass`) و تمام ارث‌بری‌ها بدون تغییر هستند.
