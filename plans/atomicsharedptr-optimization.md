# Plan: بهینه‌سازی AtomicSharedPtr (`include/Optimization/AtomicSharedPtr.h`)

> هدف: کاهش سربار پردازش و (در صورت ارزشمند بودن) RAM/Flash در `AtomicSharedPtr`
> — زیربنای Events/Timer/Async/Error و بخشی از `lib/WiFi`. بدون تغییر امضای عمومی
> (API stability). تاریخ: 2026-08-28

---

## ۱. چرا AtomicSharedPtr؟ (اولویت از تحلیل call-graph)

از روی گراف فراخوانی (codebase-memory-mcp) پس از reindex تازه:

| نماد | calls_in | کجا مصرف میشه |
|------|---------|---------------|
| `AtomicSharedPtr::operator*` | **32** | Events/Timer/Async/Error + `WiFiClient.write/connect/setTimeout` |
| `AtomicSharedPtr::operator bool` | **15** | همان‌ها + `WiFiClient._configureSocket/scanNetworks` |
| `AtomicSharedPtr::reset` | ۱ | clear/dtor (هر بار refcount--) |
| `AtomicSharedPtr::State` (ctor) | ۱ | `make()` / ctor — هر بار `new` |

`AtomicSharedPtr` **زیربنای تمام کاستوم‌ها** است (Events/Timer/Async/Error) + ۵ تماس
مستقیم در `lib/WiFi` (`_configureSocket`, `connect`, `scanNetworks`, `setTimeout`,
`write`). بهینه‌سازی‌اش مستقیم سرعت کاستوم و (غیرمستقیم) `lib/WiFi` را می‌زند بدون
دست زدن به APIهای دیفالت.

---

## ۲. ساختار فعلی (واقعی، از خواندن سورس)

```cpp
template <typename T>
class AtomicSharedPtr {
  struct State {
    T value;
    std::atomic<int> m_ref_count;   // thread-safe refcount
    State(Args&&...) : value(...), m_ref_count(1) {}
  };
  State* state;                     // 4-byte pointer (heap-allocated State)
public:
  ctor / make / copy / move / dtor / reset
  operator* / operator-> / operator bool
};
```

- هر `make()` / ctor از `T` → یک `new State` (heap alloc) + یک `delete` در reset.
- `std::atomic<int>` روی Xtensa عملیات اتمیک دارد (کندتر از int معمولی، ولی برای
  thread-safety ضروری — مخصوصاً چون Timer/Events ممکنه از ISR/Task دیگه صدا زده شن).
- RAM به ازای هر نمونه: `sizeof(State*)` (۴) + `sizeof(State)` در heap
  (`sizeof(T)` + ۴ بایت atomic + padding).

---

## ۳. Hot paths شناسایی‌شده و گلوگاه‌ها

### ۳-۱. Heap allocation در هر `make()` (بحرانی‌ترین)
هر بار `AtomicSharedPtr<T>` ساخته میشه → یک `new` (heap). در hot path (هر بار
یه callback/socket ساخته میشه) این یه syscall-style overhead داره. طبق CLAUDE.md
(RAM نامحدود) میتونیم **inline storage** اضافه کنیم (مثل FastFunction) تا برای
Tهای کوچک heap alloc نشه — ولی این تغییر معماریه و ریسک داره (شبیه data_ در
ArrayBase). **طبق تجربه FastFunction: با احتیاط، فقط اگر تست standalone تایید کرد.**

### ۳-۲. `std::atomic<int>` refcount (ناگزیر)
عملیات اتمیک (`fetch_add`/`fetch_sub`) روی Xtensa گرون‌تر از int معمولیه. اما:
- اگه فقط single-threaded استفاده میشه (که تو اکثر موارد ESP32 اینجوریه چون Timer
  در `loop()` صدا میشه نه ISR)، میتونیم `std::memory_order_relaxed` رو نگه داریم
  (همین الان هست) یا حتی `std::atomic` رو با `volatile int` + دستی عوض کنیم — ولی
  **این ناایمنه اگه multithread باشه** → رد میشه مگر اینکه اثبات شه single-threaded.

### ۳-۳. `operator*` / `operator bool` (تمیز، جای بهینه‌سازی نداره)
فقط یک deref (`state->value` / `state != nullptr`). کامپایلر از قبل بهینه‌شون کرده.
مانند `copy` در FastFunction: با ASM/LUT قابل بهتر شدن نیستن.

---

## ۴. تغییرات پیشنهادی (surgical، به تفکیک)

### تغییر ۱ — حذف `std::atomic` اگر single-threaded ثابت شد ❌ فعلاً رد
**دلیل:** ریسک corruption در Timer/Events (اگه از task دیگه صدا زده شن). نیاز به
اثبات single-threaded بودن داره. طبق «کتابخونه خراب نشه» فعلاً نمی‌زنیم.

### تغییر ۲ — کاهش اندازه `State` (`uint16_t` refcount) ❌ رد شد (ضد انتظار)
**تحلیل:** `std::atomic<int>` = ۴ بایت. تست شد `std::atomic<uint16_t>` (۲ بایت کمتر
در هر State). نتیجه بیلد:
- **Flash: ۷۹۲۰۲۱ → ۷۹۲۴۳۳ (+۴۱۲ بایت)** — بدتر شد! علت: روی Xtensa LX6،
  `std::atomic<uint16_t>` کد verboseتری تولید می‌کند (احتمالاً alignment/different
  atomic sequence) پس Flash بیشتر شد.
- **RAM کل برنامه:** تغییر نکرد (State در heap تخصیص می‌شود → در گزارش static RAM
  دیده نمی‌شود؛ فقط ۲ بایت در هر نمونه heap که قابل اندازه‌گیری مستقیم نیست).

**تصمیم:** طبق CLAUDE.md جدید («در صورتی که واقعاً ارزشمند باشه») این تغییر **ارزشمند
نبود** (Flash بدتر، RAM تو گزارش کل نامرئی). برگشت داده شد (revert). طبق Simplicity
First اعمال نشد.

### تغییر ۳ — DeadCode / کد اضافی
بررسی: آیا متدی هست که صدا زده نمیشه؟
- `State` ctor template → همیشه استفاده میشه (در make/ctor).
- `make()` vs ctor از `T` → هر دو استفاده میشن (grep تایید کرد).
- **یافته احتمالی:** `operator->` شاید کمتر از `operator*` صدا زده شه — ولی حذفش
  API رو میشکنه (طبق API stability نباید). پس DeadCode واقعی نداریم مگر اینکه
  توی تحلیل دقیق پیدا شه.

### تغییر ۴ (اختیاری) — inline storage برای T کوچک
مثل FastFunction: اگه `sizeof(T) <= N` باشه، `State` رو در استک (یا عضو کلاس)
نگه داریم نه heap. **ریسک بالا** (تغییر معماری + ممکنه کتابخونه خراب شه) → فعلاً
فقط تحلیل میشه، اعمال نمیشه مگر با تست standalone کامل.

---

## ۵. محدودیت‌ها (طبق CLAUDE.md / README)

- **API stability:** هیچ امضای عمومی (`operator*`, `operator->`, `make`, ctorها،
  `reset`, `operator bool`) تغییر نمیکنه.
- **کتابخونه include/ خراب نشه:** تغییرات نباید Events/Timer/Async/Error یا
  `lib/WiFi` را بشکنن (تست standalone + تست‌های موجود این رو تضمین می‌کنن).
- فایل‌ها ≤ ۲۰۰–۳۰۰ خط (ARCHITECTURE.md).
- RAM/Flash: طبق CLAUDE.md جدید، فقط در صورت ارزشمند بودن کاهش بدهیم.

---

## ۶. تست صحت (Standalone — مستقل از این پروژه)

> `AtomicSharedPtr` توی `include/` هست = پروژه‌ی جداگونه. تست نباید به محیط
> Optimal-Wifi (WiFi/Events/Timer) وابسته باشه — فقط `#include
> <Optimization/AtomicSharedPtr.h>` + زیرساخت سریال (`test_config.h` ماکروهای
> `TEST_*` / `Serial`).

فایل جدید: `src/tests/test_atomicsharedptr.cpp` + `test_atomicsharedptr.h`
- تابع ورودی: `run_atomicsharedptr_tests()` — از `main.cpp` فراخوانی میشه.

### ۶-۱. موارد صحت (correctness) — baseline امنیته
| سناریو | verify |
|--------|--------|
| make + operator* | مقدار درست برمی‌گرده |
| copy-ctor | refcount++، دو کپی یک value می‌بینن |
| copy-assign | refcount درست، منبع زنده می‌مونه |
| move-ctor/assign | مقصد value رو می‌بینه، منبع empty (بدون double-free) |
| reset/dtor | آخرین refcount حذف میکنه (بدون leak) |
| **تست نشت:** ۱۰۰۰ بار make+reset در حلقه | heap leak نشه (توی بنچمارک بررسی) |
| operator bool | null-check درست |
| multithread شبیه‌سازی: ۲ کپی همزمان | refcount درست بمونه (بدون race) |

### ۶-۲. بنچمارک سریال (قبل از هر تغییر)
با `micros()` زمان می‌گیریم و عدد پایه (baseline) روی سریال چاپ می‌کنیم:
```
[AtomicSharedPtr] baseline: make=X ns | copy=Y ns | deref=Z ns | reset=W ns
```

### ۶-۳. بعد از بهینه‌سازی
همان تست صحت (۶-۱) باید دوباره `PASS` بده + بنچمارک عدد کمتری بده.
درصد واقعی بهبود روی سریال گزارش میشه.

### ۶-۴. تست‌های موجود (رگرسیون کلی)
`test_async_client`, `test_generic`, `test_benchmark` + FastFunction standalone
هم اجرا میشن تا رگرسیون سطح سیستم گیر بیفته.

---

## ۷. ترتیب اجرا (goal-driven — بی‌گدار به آب نمی‌زنیم)

1. **تست standalone صحت** بنویس و اجرا کن → باید `PASS` بده (AtomicSharedPtr سالمه).
2. **بنچمارک پایه** → عدد baseline روی سریال.
3. تغییر ۲ (uint16_t refcount) → بیلد + تست + بنچمارک → درصد بهبود واقعی.
4. تغییر ۳ (DeadCode) → اگر پیدا شد.
5. تغییر ۴ (inline storage) → فقط با تست standalone کامل (ریسک بالا).
6. تست‌های موجود WiFi/کاستوم pass باشن.
7. `lint` + `format` + build.

---

## ۸. چه چیزهایی بهتر میشه؟ (تحلیل CPU/RAM/Flash)

| تغییر | CPU | RAM | Flash | ریسک |
|-------|-----|-----|-------|------|
| uint16_t refcount (تغییر ۲) | صفر/ناچیز | **−۲ بایت/نمونه** | کمی کمتر | کم |
| inline storage (تغییر ۴) | **−new/delete در hot path** | +N بایت استک | کمتر کد | **بالا** |
| حذف atomic (تغییر ۱) | سریع‌تر | صفر | کمتر | **بحرانی** (رد شد) |

**نتیجه:** بهینه‌سازی AtomicSharedPtr بیشتر روی **حذف heap alloc (تغییر ۴)** و
**کاهش اندازه State (تغییر ۲)** تمرکز داره. CPU از نظر deref تمیزه؛ گلوگاه اصلی
`new`/`delete` هست نه عملیات اتمیک.
