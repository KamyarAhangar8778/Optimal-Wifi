#include "test_config.h"
#include "test_fastfunction.h"

#include <Optimization/FastFunction.h>

namespace {

// ---------------------------------------------------------------------------
// Correctness: invoke a simple lambda + return value
// ---------------------------------------------------------------------------
void test_ff_invoke_simple()
{
    TEST_CASE_START("FastFunction invoke (simple int(int))");

    uniuno::FastFunction<int(int)> f = [](int x) { return x * 2; };
    TEST_ASSERT(static_cast<bool>(f), "invoker not set after construct");
    TEST_ASSERT(f(21) == 42, "invoke returned wrong value");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: non-trivial functor with captured state must keep its state
// ---------------------------------------------------------------------------
void test_ff_capture_state()
{
    TEST_CASE_START("FastFunction capture (non-trivial) preserves state");

    int base = 10;
    uniuno::FastFunction<int(int)> f = [base](int x) { return base + x; };
    TEST_ASSERT(f(5) == 15, "captured state lost");
    TEST_ASSERT(f(100) == 110, "captured state lost on second call");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: move-construct leaves source empty, dest usable
// ---------------------------------------------------------------------------
void test_ff_move_construct()
{
    TEST_CASE_START("FastFunction move-construct");

    uniuno::FastFunction<int(int)> src = [](int x) { return x + 1; };
    uniuno::FastFunction<int(int)> dst(std::move(src));

    TEST_ASSERT(!static_cast<bool>(src), "source not emptied after move");
    TEST_ASSERT(static_cast<bool>(dst), "dest not valid after move");
    TEST_ASSERT(dst(41) == 42, "dest invoke wrong after move");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: move-assign leaves source empty, dest usable
// ---------------------------------------------------------------------------
void test_ff_move_assign()
{
    TEST_CASE_START("FastFunction move-assign");

    uniuno::FastFunction<int(int)> src = [](int x) { return x + 1; };
    uniuno::FastFunction<int(int)> dst = [](int x) { return x * 3; };
    dst = std::move(src);

    TEST_ASSERT(!static_cast<bool>(src), "source not emptied after move-assign");
    TEST_ASSERT(dst(41) == 42, "dest invoke wrong after move-assign");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: copy-construct yields independent copies
// ---------------------------------------------------------------------------
void test_ff_copy_construct()
{
    TEST_CASE_START("FastFunction copy-construct (independent)");

    int k = 7;
    uniuno::FastFunction<int(int)> orig = [k](int x) mutable { return k + x; };
    uniuno::FastFunction<int(int)> cpy(orig);

    TEST_ASSERT(static_cast<bool>(orig) && static_cast<bool>(cpy),
                "copy left a side invalid");
    TEST_ASSERT(cpy(3) == 10, "copy invoke wrong");
    TEST_ASSERT(orig(3) == 10, "original invoke wrong after copy");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: operator bool + clear()
// ---------------------------------------------------------------------------
void test_ff_bool_clear()
{
    TEST_CASE_START("FastFunction operator bool + clear()");

    uniuno::FastFunction<int(int)> f;
    TEST_ASSERT(!static_cast<bool>(f), "default-constructed must be empty");

    f = [](int x) { return x; };
    TEST_ASSERT(static_cast<bool>(f), "assigned must be valid");
    f.clear();
    TEST_ASSERT(!static_cast<bool>(f), "clear() did not empty");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: 1000 consecutive moves must NOT leak or corrupt (this is the
// exact path where the double-move bug would surface: aliasing / double-free)
// ---------------------------------------------------------------------------
void test_ff_move_stress()
{
    TEST_CASE_START("FastFunction move stress (1000 moves, no leak/corrupt)");

    struct Holder
    {
        int id;
        int* shared; // heap resource to detect aliasing/double-free
        Holder(int i) : id(i), shared(new int(i)) {}
        Holder(const Holder& o) : id(o.id), shared(new int(*o.shared)) {}
        Holder(Holder&& o) : id(o.id), shared(o.shared)
        {
            o.shared = nullptr;
        }
        ~Holder()
        {
            delete shared; // will crash/double-free if aliased
            shared = nullptr;
        }
        int get() const { return *shared; }
    };

    uniuno::FastFunction<int()> f = [h = Holder(5)]() { return h.get(); };
    TEST_ASSERT(f() == 5, "initial value wrong");

    for (int i = 0; i < 1000; ++i)
    {
        uniuno::FastFunction<int()> moved(std::move(f));
        if (moved() != 5)
        {
            TEST_FAIL("value corrupted during move stress");
            return;
        }
        f = std::move(moved); // ping-pong to exercise both move paths
    }

    TEST_ASSERT(f() == 5, "final value corrupted");
    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: trivial functor takes the fast (vtable_ == null) path
// ---------------------------------------------------------------------------
void test_ff_trivial_fast_path()
{
    TEST_CASE_START("FastFunction trivial functor (memcpy path)");

    // A plain function pointer / stateless lambda is trivially copyable.
    uniuno::FastFunction<int(int)> f = [](int x) { return x - 1; };
    TEST_ASSERT(f(50) == 49, "trivial invoke wrong");

    auto cpy = f; // copy of trivial must work too
    TEST_ASSERT(cpy(50) == 49, "trivial copy invoke wrong");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Benchmark: measure move / invoke / copy cost on serial (baseline + after)
// ---------------------------------------------------------------------------
void bench_fastfunction()
{
    Serial.println("\n[BENCH FF] FastFunction micro-benchmark:");

    const int ITERS = 20000;

    // --- move cost ---
    uint32_t t0 = micros();
    for (int i = 0; i < ITERS; ++i)
    {
        uniuno::FastFunction<int(int)> src = [i](int x) { return x + i; };
        uniuno::FastFunction<int(int)> dst(std::move(src));
        asm volatile("" : : "r"(dst(1)) : "memory"); // keep alive, prevent DCE
    }
    uint32_t moveUs = micros() - t0;

    // --- invoke cost ---
    uniuno::FastFunction<int(int)> inv = [](int x) { return x * 2; };
    t0 = micros();
    volatile int sink = 0;
    for (int i = 0; i < ITERS; ++i)
    {
        sink = inv(i);
    }
    uint32_t invokeUs = micros() - t0;

    // --- copy cost ---
    uniuno::FastFunction<int(int)> base = [](int x) { return x + 1; };
    t0 = micros();
    for (int i = 0; i < ITERS; ++i)
    {
        uniuno::FastFunction<int(int)> c(base);
        asm volatile("" : : "r"(c(1)) : "memory");
    }
    uint32_t copyUs = micros() - t0;

    Serial.printf("  -> move   : %4u us / %d = %u ns/op\n", moveUs, ITERS,
                  (unsigned)((uint64_t)moveUs * 1000 / ITERS));
    Serial.printf("  -> invoke : %4u us / %d = %u ns/op\n", invokeUs, ITERS,
                  (unsigned)((uint64_t)invokeUs * 1000 / ITERS));
    Serial.printf("  -> copy   : %4u us / %d = %u ns/op\n", copyUs, ITERS,
                  (unsigned)((uint64_t)copyUs * 1000 / ITERS));
}

} // namespace

void run_fastfunction_tests()
{
    TEST_SECTION_START("FastFunction (standalone correctness + benchmark)");
    test_ff_invoke_simple();
    test_ff_capture_state();
    test_ff_move_construct();
    test_ff_move_assign();
    test_ff_copy_construct();
    test_ff_bool_clear();
    test_ff_move_stress();
    test_ff_trivial_fast_path();
    bench_fastfunction();
}
