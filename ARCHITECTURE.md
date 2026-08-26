# ARCHITECTURE.md — Optimal-Wifi

> نقشه‌ی راه و مرجع معماری پروژه. قبل از شروع هر تغییر یا بهینه‌سازی، این سند را مطالعه کنید.

---

## ۱. نمای کلی سیستم (System Overview)

این پروژه با هدف **بهینه‌سازی حداکثری سرعت، کاهش تاخیر (Latency) و حذف سربارهای غیرضروری (Zero-Allocation / Lock-Free)** در کتابخانه‌ی رسمی WiFi برای میکروکنترلر ESP32 در پلتفرم Arduino طراحی شده است.

### لایه‌های اصلی:

| لایه | مسیر | نقش و مسئولیت |
|------|------|---------------|
| **کتابخانه‌ی اصلی (Target)** | `lib/WiFi/src/` | پیاده‌سازی رسمی و بهینه‌شده‌ی کلاس‌های وایفای (تنها بخشی که کتابخانه را می‌سازد) |
| **ابزارهای بهینه‌سازی (Custom Tools)** | `include/` | ساختارهای داده بدون تخصیص پویا، تایمرهای سخت‌افزاری/نرم‌افزاری، Async Future و Events |
| **مجموعه تست و بنچمارک (Test Harness)** | `src/` و `src/tests/` | تست‌های خودکار پورت سریال برای صحت‌سنجی ۱۰۰٪ قابلیت‌ها و سنجش زمان اجرا |

---

## ۲. ساختار دقیق پوشه‌ها و فایل‌ها

```
Optimal-Wifi/
├── lib/
│   └── WiFi/                          # کتابخانه‌ی هدف برای بهینه‌سازی
│       ├── src/
│       │   ├── WiFi.h / WiFi.cpp      # Facade اصلی (WiFiClass) مشتق از تمام لایه‌ها
│       │   ├── WiFiGeneric.h/.cpp     # لایه‌ی ریشه (مدیریت Events، حالت‌ها، Sleep، توان TX)
│       │   ├── WiFiSTA.h/.cpp         # لایه کلاینت (اتصال به مودم، دریافت IP، مدیریت DHCP)
│       │   ├── WiFiAP.h/.cpp          # لایه اکسس‌پوینت (سافت‌AP، کانفیگ Subnet و IP محلی)
│       │   ├── WiFiScan.h/.cpp        # اسکن همگام/ناهمگام شبکه‌های اطراف و پارس BSSID/RSSI
│       │   ├── WiFiClient.h/.cpp      # سوکت TCP کلاینت و بافرینگ RX/TX
│       │   ├── WiFiClientInternal.h   # کلاس‌های داخلی مشترک کلاینت (RX-buffer، Socket-handle)
│       │   ├── WiFiClientAsync.cpp    # اتصال/ارسال آسنکرون غیربلوکینگ (Poll State-Machine)
│       │   ├── WiFiServer.h/.cpp      # سرور TCP با قابلیت پذیرش کلاینت‌ها
│       │   ├── WiFiUdp.h/.cpp         # سوکت ارسال/دریافت بسته‌های بدون اتصال UDP
│       │   ├── WiFiMulti.h/.cpp       # مدیریت اتصال هوشمند و خودکار به لیست APها
│       │   └── WiFiType.h             # تایپ‌ها، انوم‌های وضعیت (wl_status_t) و ماکروها
│       └── library.properties
│
├── include/                           # ابزارهای کاستوم برای شتاب‌دهی (اختیاری)
│   ├── README                         # راهنمای نحوه استفاده از ابزارهای کاستوم
│   ├── Core/
│   │   ├── Async/                     # سیستم ناهمگام (Future, Deferred, Executor)
│   │   └── Error/                     # ساختار مدیریت خطا و سیاست‌های ثبت ارور
│   ├── Events/                        # دیسپچر سریع رویدادها (EventDispatcher)
│   ├── Optimization/                  # ساختارهای داده بهینه (StaticArray, StaticHashMap, SmallVector, FastFunction)
│   ├── Timer/                         # موتور زمان‌بندی دقیق غیر-بلوکینگ (Hardware/Software Timers)
│   └── Utilities/                     # ابزارهای کمکی (Logging, Traits, Clock Sync)
│
├── src/
│   ├── main.cpp                       # نقطه ورود تست (Runner، نمایش بنر و گزارش وضعیت)
│   ├── Optimization/                  # پیاده‌سازی سورس‌های کانتینرها (ArrayBase, HashMapBase)
│   └── tests/                         # ماژول‌های مجزای تست سریال
│       ├── test_config.h              # متغیرهای تست (SSID, Password, Timeout) و ماکروهای آماری
│       ├── test_generic.cpp/.h        # تست حالت‌ها، مک‌آدرس، توان، اسلیپ و رویدادها
│       ├── test_scan.cpp/.h           # تست اسکن سنکرون و آسنکرون
│       ├── test_ap.cpp/.h             # تست چرخه حیات SoftAP و تنظیم IP سفارشی
│       ├── test_sta.cpp/.h            # تست اتصال به روتر، DHCP، ریکانکت و استاتیک IP
│       ├── test_sockets.cpp/.h        # تست ارتباط دوطرفه TCP Client/Server و پکت UDP
│       └── test_async_client.cpp/.h   # تست connect/write آسنکرون + بنچمارک stall در برابر حالت بلوکینگ
│
└── platformio.ini                     # تنظیمات کامپایلر، بردهای ESP32 و مسیرهای Include
```

---

## ۳. قوانین طلایی توسعه و بهینه‌سازی

1. **حفظ کامل امضای توابع عمومی (API Stability):** نام توابع، آرگومان‌های پیش‌فرض و مقادیر بازگشتی عمومی در `lib/WiFi/src/` نباید دستخوش تغییر شوند تا پروژه‌های کاربر بدون خطا کامپایل شوند.
2. **اولویت مطلق با سرعت (Performance-First):** مجاز به استفاده نامحدود از حافظه RAM و Flash هستیم؛ هدف بیشینه‌سازی سرعت و کمینه‌سازی تاخیر است.
3. **معیار واقعی درصد بهبود:** پس از هر مرحله ریفکتور، درصد واقعی بهبود سرعت نسبت به نسخه پایه اندازه‌گیری و گزارش می‌شود.
4. **استاندارد حجم فایل:** هر فایل باید حداکثر بین ۲۰۰ تا ۳۰۰ خط نگه‌داشته شود (به جز فایل‌های هدر و کانفیگ خاص).

---

## ۴. دستورات بیلد و اجرا

```bash
# بیلد پروژه:
C:\Users\KAVEH\.platformio\penv\Scripts\platformio.exe run

# مانیتور خروجی سریال (Baud: 115200):
C:\Users\KAVEH\.platformio\penv\Scripts\platformio.exe device monitor
```
