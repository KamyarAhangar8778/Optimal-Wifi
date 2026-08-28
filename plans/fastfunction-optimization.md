# Plan: بهینه‌سازی FastFunction (`include/Optimization/FastFunction/`)

> هدف: کاهش سربار پردازش در hot pathهای `FastFunction` — زیربنای تمام کاستوم‌ها
> (Events, Timer, Async) و بخشی از `lib/WiFi`. بدون تغییر امضای عمومی (API stability).
>
> تاریخ: 2026-08-28

---

## ۱. چرا FastFunction؟ (اولویت از تحلیل call-graph)

از روی گراف فراخوانی (codebase-memory-mcp) در کل پروژه (خارج از `lib/WiFi`):

| نماد | calls_in | کجا مصرف میشه |
|------|---------|---------------|
| `FastFunctionVTableImpl.move` | **34** | هر move-construct/assign در AsyncResult, AtomicSharedPtr, BaseEventDispatcher, Deferred, ErrorHandler, Executor |
| `AtomicSharedPtr` | 44 | زیربنای EventDispatcher/Timer (و توسط `WiFiClient.connect` و غیره) |
| `Timer*` (کل زیرسیستم) | ~130 | `TimerManager`, `TimerStorage`, `Timer`, ... |

`FastFunction` **زیربنای مشترک** بقیه‌ست → بهینه‌سازی‌اش مستقیم سرعت کاستوم و (غیرمستقیم) `lib/WiFi` رو می‌زنه بدون دست زدن به APIهای دیفالت.

`lib/WiFi` مستقیماً روی این تماس‌ها نشسته (hot pathها):
- `AtomicSharedPtr`: `WiFiClient.connect`, `pollConnect`, `scanNetworks`, `setTimeout`, `write`
- `FastFunction`: `WiFiClient` ctor
- `Timer`: `WiFiClient::connected`, RX buffer, `setTimeout`, `~WiFiMulti`

---

## ۲. ساختار فعلی (واقعی، از خواندن سورس)

```
include/Optimization/FastFunction/
├── FastFunctionBase.h       // FastFunctionVTable + FastFunctionVTableImpl (destroy/move/copy)
├── FastFunctionDecl.h       // تعریف کلاس: invoker_ + vtable_ + SmallVector storage_
├── FastFunctionConstructors.h // ctor functor + invoke_impl (IRAM) + vtable_impl
├── FastFunctionCopyMove.h   // copy/move ctor + copy/move assign
├── FastFunctionInvoke.h     // operator() + operator bool (FORCE_INLINE)
└── FastFunctionMemory.h     // dtor + clear
```

کلاس: `storage_` از نوع `SmallVector<std::max_align_t, N>` (inline تا Capacity، بعد heap).
`vtable_` = `nullptr` وقتی functor trivially-destructible/movable/copyable باشه (مسیر سریع memcpy).

---

## ۳. Hot paths شناسایی‌شده و گلوگاه‌ها

### ۳-۱. Move double-work (بحرانی — روی ۳۴ تماس)
در `FastFunctionCopyMove.h` move ctor/assign:
```cpp
bool was_heap = other.storage_.is_heap();
storage_ = std::move(other.storage_);          // (A) محتوا قبلاً منتقل شد
if (!was_heap && vtable_) {
  vtable_->move(storage_.data(), other.storage_.data());  // (B) دوباره placement-new!
}
```

**تحلیل دقیق از سورس `SmallVectorMove.h` (واقعی):**
`storage_` از نوع `SmallVector<std::max_align_t, N>` هست. `std::max_align_t` یک نوع
**POD / trivially-copyable** است → طبق `SmallVectorMove.h` (خط ۵۷-۵۸ و ۹۰-۹۱) موقع موو
SmallVector **همیشه** از مسیر `std::memcpy(data_, other.data_, size_ * sizeof(T))` میره،
نه move-ctor عنصری.

پس برای functor **non-trivial** (که `vtable_ != nullptr`، مثلاً lambda با capture):
- **(A)** بایت‌های functor را bitwise کپی می‌کند (برای non-trivial این یک کپی ناقص/فراری است).
- **(B)** روی همون بافر دوباره `new DecayF(std::move(...))` اجرا می‌کند.

یعنی **دو عمل سنگین پشت سر هم**: یک `memcpy` + یک ساخت دوباره (move-ctor) + یک
`MEMORY_BARRIER`. این فقط کند نیست — بالقوه **ناایمن** هم هست: اگه functor منبعی
مثل pointer داشته باشه، کپی bitwise در (A) باعث aliasing میشه (تست ۱۰۰۰ بار موو این
را می‌گیرد).

**اثر روی CPU:** هر move = ۲ عمل سنگین به جای ۱. روی hot path که ۳۴ بار در هر چرخه
(dispatch رویداد / tick timer / poll future) صدا میشه → سربار CPU قابل‌توجه.

### ۳-۲. سازنده: دو مرحله reserve+set_size
`FastFunctionConstructors.h`:
```cpp
storage_.reserve(required_elements);
storage_.force_set_size(required_elements);
new (storage_.data()) DecayF(...);
```
`reserve` + `force_set_size` دو فراخوانی جداگانه روی SmallVector؛ قابل ادغام به یک مسیر مستقیم.

### ۳-۳. `clear()` همیشه storage پاک میکنه
`FastFunctionMemory.h`: `clear()` همیشه `storage_.clear()` رو صدا میزنه حتی وقتی فقط `invoker_` ناله. برای موو-assign که `clear()` صدا میزنه قبل از موو، این هزینه‌ی اضافیه.

---

## ۴. تغییرات پیشنهادی (surgical، به تفکیک فایل)

### تغییر ۱ — `FastFunctionCopyMove.h`: حذف double-move (بحرانی)
**مشکل:** وقتی functor non-trivial و روی inline storage باشه، (A) یک `memcpy` bitwise
از بایت‌های functor انجام می‌دهد و بعد (B) دوباره `new DecayF(std::move(...))` روی همون
بافر صدا می‌زند → کپی ناقص + ساخت تکراری + خطر aliasing.

**راه‌حل (surgical):** functor را **قبل** از جابجایی بافر توسط SmallVector منتقل کن،
یا برعکس — فقط یکی از دو کار را انجام بده:
- برای مسیر **non-trivial + inline** (`!was_heap && vtable_`):
  ابتدا `vtable_->move(storage_.data(), other.storage_.data())` را صدا بزن،
  **سپس** `storage_ = std::move(other.storage_)` فقط metadata (size/capacity/ptr) را
  جابجا کند — نه دوباره بایت‌ها. یا ساده‌تر: اجازه بده SmallVector مسیر memcpy را برود
  و به جای `vtable_->move` فقط destructor سورس را فراخوانی کن (`vtable_->destroy`).
  به این ترتیب بایت‌ها یک بار (در A) کپی می‌شوند و destructor یک بار → **صفر ساخت تکراری**.

**توجه مهم:** این منطق نیاز به بررسی دقیق دارد که `vtable_->destroy` روی بافرِ *سورس*
صدا زده شود (نه مقصد) و سورس در حالت empty معتبر باقی بماند. تست ۱۰۰۰ بار موو correctness
را تضمین می‌کند.

- حفظ رفتار: موو-ctor/assign باید سورس را در حالت معتبر (empty) رها کند.
- معیار: حذف یک `memcpy` + یک move-ctor + یک `MEMORY_BARRIER` در هر move برای non-trivial.

### تغییر ۲ — `FastFunctionConstructors.h`: ادغام reserve+set_size
یک متد کمکی در SmallVector (یا مستقیم اینجا) که inline storage رو در یک گام آماده کنه بدون فراخوانی دوگانه. اگر SmallVector فعلاً راه مستقیم نداره، از `resize`-سبک استفاده کن.

### تغییر ۳ — `FastFunctionMemory.h`: clear سبک‌تر
وقتی `vtable_` ناله (trivial) و فقط `invoker_` ست، `storage_.clear()` رو فقط در صورت نیاز صدا بزن (معمولاً تفاوت ناچیز؛ فقط اگر پروفایل تایید کرد).

### تغییر ۴ (اختیاری) — `FastFunctionInvoke.h`: ثبات کنترل
`operator()` از قبل `FORCE_INLINE`. میتونیم مطمئن شیم `invoker_` مستقیم صدا میشه بدون بررسی اضافی. (در حال حاضر تمیزه — فقط برای اطمینان.)

---

## ۵. محدودیت‌ها (طبق CLAUDE.md / README)

- **API stability:** هیچ امضای عمومی (`operator()`, ctorها، `clear`, `operator bool`) تغییر نمیکنه.
- فایل‌ها باید ≤ ۲۰۰–۳۰۰ خط بمونن (طبق ARCHITECTURE.md).
- حافظه RAM/Flash نامحدوده → فقط سرعت مهمه.
- **`include/` یک پروژه‌ی جداگانه است** (کپی‌شده از جای دیگر، بعداً روی خودش کار
  می‌کنیم). پس:
  - تست‌های FastFunction **نباید** به محیط Optimal-Wifi وابسته باشند — فقط
    `#include <Optimization/FastFunction.h>` + زیرساخت سریال (`test_config.h` ماکروهای
    `TEST_*`/`Serial`). قابل کپی به پروژه‌ی اصلی `include/` باشند.
  - تغییرات FastFunction فقط در فایل‌های `include/Optimization/FastFunction/` اعمال
    می‌شوند؛ به `lib/WiFi` یا سایر بخش‌های Optimal-Wifi دست نمی‌زنیم (مگر اضافه کردن
    تست در `src/tests/`).

---

## ۶. تست صحت (Standalone — مستقل از این پروژه)

> محدودیت حیاتی: `FastFunction` کتابخونه‌ی **مستقل** است که import/copy شده.
> تست‌هایش نباید به محیط این پروژه (WiFi/Events/Timer) وابسته باشند — فقط
> خودِ `FastFunction` را verify کنند تا قابل کپی به پروژه‌ی اصلی‌ش باشند.
> ولی توی این پروژه اجرا می‌شوند و خروجی سریال می‌دهند.

فایل جدید: `src/tests/test_fastfunction.cpp` + `test_fastfunction.h`
- **فقط** `#include <Optimization/FastFunction.h>` (بدون `WiFi.h`، بدون Events/Timer).
- تابع ورودی: `run_fastfunction_tests()` — از `main.cpp` فراخوانی میشه.
- از `test_config.h` (ماکروهای `TEST_*` + `Serial`) استفاده میکنه — این فقط
  زیرساخت تسته، نه منطق WiFi.

### ۶-۱. موارد صحت (correctness) — این baseline امنیته
| سناریو | verify |
|--------|--------|
| invoke lambda ساده `int(int)` | مقدار صحیح برمی‌گرده |
| lambda با capture (non-trivial) | state داخل capture حفظ میشه |
| موو-سازی (`std::move`) | مقصد کار میکنه، منبع empty (بدون double-free) |
| کپی-سازی | دو کپی مستقل؛ تغییر یکی روی دیگری اثر نداره |
| موو-انتساب (`operator=`) | مثل موو-سازی |
| `operator bool` / `clear()` | نال‌چک و پاک‌سازی درست |
| **۱۰۰۰ بار موو در حلقه** | نشت/corrupt نشه (دقیقاً جایی که double-move خراب میکنه) |
| functor trivial (بدون vtable_) | مسیر memcpy سریع درست کار کنه |

### ۶-۲. بنچمارک سریال (قبل از هر تغییر)
با `micros()` زمان می‌گیریم و عدد پایه (baseline) روی سریال چاپ می‌کنیم:
```
[FastFunction] baseline: move = X ns/op | invoke = Y ns/op | copy = Z ns/op
```

### ۶-۳. بعد از بهینه‌سازی
همان تست صحت (۶-۱) باید دوباره `PASS` بده (رفتار عوض نشه) و بنچمارک عدد کمتری بده:
```
[FastFunction] after: move = X' ns/op | ...  → Z% faster
```

### ۶-۴. تست‌های موجود WiFi (رگرسیون کلی)
`test_move_semantics` + `test_hot_path_accessors` (در `run_async_client_tests`) هم اجرا
می‌شوند تا رگرسیون سطح سیستم گیر بیفتد — اما مکمل‌اند نه جایگزین تست standalone.

---

## ۷. ترتیب اجرا (goal-driven — بی‌گدار به آب نمی‌زنیم)

1. **تست standalone صحت** (`run_fastfunction_tests`) رو بنویس و اجرا کن →
   باید `PASS` بده. این فنس ایمنیه: اگه FastFunction الان خراب باشه، قبل از
   دست‌زدن میفهمیم.
2. **بنچمارک پایه** رو اجرا کن → عدد baseline روی سریال ثبت شه
   (`[FastFunction] baseline: ...`).
3. تغییر ۱ (double-move) → بیلد + اجرای دوباره تست standalone (باید باز `PASS`) +
   بنچمارک → درصد بهبود واقعی.
4. تغییر ۲ (reserve+set_size) → بیلد + تست + بنچمارک.
5. تغییر ۳/۴ (اگر پروفایل تایید کرد).
6. تست‌های موجود WiFi (`test_move_semantics`, `test_hot_path_accessors`) pass باشن
   (رفتار سطح سیستم حفظ شه).
7. `lint` + `format` + build.

---

## ۸. Verification

- `C:\Users\KAVEH\.platformio\penv\Scripts\platformio.exe run` → کامپایل بدون خطا.
- مانیتور سریال: بنچمارک عدد بهبود چاپ می‌کنه؛ تست‌ها `OK` میدن.
- رفتار: موو/کپی/اینوک همچنان صحیح (تست async_client_connect + hot_path_accessors).
