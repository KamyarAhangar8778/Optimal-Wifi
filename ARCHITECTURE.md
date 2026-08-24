# ARCHITECTURE.md — Optimal-Wifi

> نقشه‌ی سریع پروژه. هر بار که گیج شدی کدوم فایل چیکار می‌کنه، اول اینو بخون،


---

## 1. نمای کل (Big Picture)

این یه **کتابخونه‌ی WiFi بهینه‌شده برای ESP32/Arduino** هست. سه لایه داره:

| لایه | پوشه | نقش |
|------|------|-----|
| **کتابخونه‌ی اصلی** | `lib/WiFi/src/` | پیاده‌سازی کلاس‌های WiFi (STA/AP/Scan/Client/Server/UDP) — تنها کتابخونه‌ای که کامپایل میشه |
| **کتابخونه‌های کاستوم** | `include/` | ابزارهای بهینه‌سازی (Timer, Error, Events, Optimization, Core/Async) |
| **تست‌ها** | `src/tests/` | تست سریال که خروجی‌ش رو کاربر از Serial Monitor می‌خونه |

---

## 2. ساختار پوشه‌ها

```
Optimal-Wifi/
├── lib/
│   └── WiFi/        # کتابخونه‌ی اصلی و فعال (اینجا دست می‌زنیم)
│       ├── src/               # کلاس‌های WiFi + پیاده‌سازی .cpp
│       │   ├── WiFi.h / WiFi.cpp          # WiFiClass — facade کلی
│       │   ├── WiFiGeneric.h/.cpp         # ریشه‌ی همه (event, mode, sleep, power)
│       │   ├── WiFiSTA.h/.cpp             # Station (کلاینت، اتصال به AP)
│       │   ├── WiFiAP.h/.cpp              # Access Point (سافت‌AP)
│       │   ├── WiFiScan.h/.cpp            # اسکن شبکه‌ها
│       │   ├── WiFiClient.h/.cpp          # سوکت TCP کلاینت
│       │   ├── WiFiServer.h/.cpp          # سوکت TCP سرور
│       │   ├── WiFiUdp.h/.cpp             # سوکت UDP
│       │   ├── WiFiMulti.h/.cpp           # اتصال خودکار به لیست APها
│       │   └── WiFiType.h                 # انوم‌ها و تایپ‌های مشترک
│       ├── examples/          # مثال‌ها (FTM, WPS, WiFiScan, ...)
│       ├── library.properties # name=WiFi_Optimized, version=2.0.0
│       └── keywords.txt
├── include/                   # کتابخونه‌های کاستوم
│   ├── Core/
│   │   ├── Error/             # سیستم مدیریت ارور (فقط توی تست‌ها)
│   │   └── Async/             # Future / Deferred / AsyncResult
│   ├── Events/                # EventDispatcher (سیستم رویداد)
│   ├── Optimization/          # کانتینرهای بهینه (Static*, SmallVector, FastFunction, AtomicSharedPtr)
│   ├── Timer/                 # کتابخونه‌ی تایمر غیر-بلوکینگ
│   └── Utilities/             # function_traits, logging, sync_clock
├── src/
│   ├── tests/              # تست‌های سریال (test_main + test_*.cpp)
│   └── Optimization/          # .cpp کانتینرها (ArrayBase, HashMapBase)
└── platformio.ini             # کانفیگ build (env: esp32dev) → lib_deps = WiFi_Optimized
```

## 3. نحوه‌ی کامپایل گرفتن

```bash
# دستور بیلد:
:\Users\KAVEH\.platformio\penv\Scripts\platformio.exe run

```