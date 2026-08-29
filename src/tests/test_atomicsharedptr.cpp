#include "test_config.h"
#include "test_atomicsharedptr.h"

#include <Optimization/AtomicSharedPtr.h>

namespace {

static int g_asp_alive = 0; // shared leak detector across local Counter types

// ---------------------------------------------------------------------------
// Correctness: make + operator*
// ---------------------------------------------------------------------------
void test_asp_make_deref()
{
    TEST_CASE_START("AtomicSharedPtr make + operator*");

    uniuno::AtomicSharedPtr<int> p = uniuno::AtomicSharedPtr<int>::make(42);
    TEST_ASSERT(static_cast<bool>(p), "make left ptr null");
    TEST_ASSERT(*p == 42, "operator* returned wrong value");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: copy-ctor shares value, independent reads
// ---------------------------------------------------------------------------
void test_asp_copy_ctor()
{
    TEST_CASE_START("AtomicSharedPtr copy-ctor (shared value)");

    uniuno::AtomicSharedPtr<int> orig = uniuno::AtomicSharedPtr<int>::make(7);
    uniuno::AtomicSharedPtr<int> cpy(orig);

    TEST_ASSERT(static_cast<bool>(orig) && static_cast<bool>(cpy), "copy invalidated a side");
    TEST_ASSERT(*orig == 7 && *cpy == 7, "copies disagree on value");
    // mutate via original, copy must see it (shared state)
    *orig = 99;
    TEST_ASSERT(*cpy == 99, "copy did not observe shared mutation");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: copy-assign
// ---------------------------------------------------------------------------
void test_asp_copy_assign()
{
    TEST_CASE_START("AtomicSharedPtr copy-assign");

    uniuno::AtomicSharedPtr<int> a = uniuno::AtomicSharedPtr<int>::make(1);
    uniuno::AtomicSharedPtr<int> b = uniuno::AtomicSharedPtr<int>::make(2);
    b = a; // a kept alive, b's old state freed

    TEST_ASSERT(*b == 1, "copy-assign did not take source value");
    TEST_ASSERT(*a == 1, "copy-assign destroyed source");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: move-ctor/assign leaves source empty, dest usable
// ---------------------------------------------------------------------------
void test_asp_move()
{
    TEST_CASE_START("AtomicSharedPtr move (ctor + assign)");

    uniuno::AtomicSharedPtr<int> src = uniuno::AtomicSharedPtr<int>::make(55);
    uniuno::AtomicSharedPtr<int> dst(std::move(src));
    TEST_ASSERT(!static_cast<bool>(src), "move source not emptied");
    TEST_ASSERT(*dst == 55, "move dest wrong value");

    uniuno::AtomicSharedPtr<int> src2 = uniuno::AtomicSharedPtr<int>::make(66);
    uniuno::AtomicSharedPtr<int> dst2 = uniuno::AtomicSharedPtr<int>::make(0);
    dst2 = std::move(src2);
    TEST_ASSERT(!static_cast<bool>(src2), "move-assign source not emptied");
    TEST_ASSERT(*dst2 == 66, "move-assign dest wrong value");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: reset / dtor frees when last reference drops
// ---------------------------------------------------------------------------
void test_asp_reset_last()
{
    TEST_CASE_START("AtomicSharedPtr reset frees on last ref");

    struct Counter {
        int id;
        Counter(int i) : id(i) { g_asp_alive++; }
        ~Counter() { g_asp_alive--; }
    };
    g_asp_alive = 0;

    {
        uniuno::AtomicSharedPtr<Counter> p = uniuno::AtomicSharedPtr<Counter>::make(1);
        TEST_ASSERT(g_asp_alive == 1, "ctor did not create object");
        p.reset();
        TEST_ASSERT(g_asp_alive == 0, "reset did not free object");
        TEST_ASSERT(!static_cast<bool>(p), "reset did not null the ptr");
    }
    TEST_ASSERT(g_asp_alive == 0, "dtor leaked object");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: 1000 make+reset cycles must not leak (heap hygiene)
// ---------------------------------------------------------------------------
void test_asp_leak_stress()
{
    TEST_CASE_START("AtomicSharedPtr 1000 make+reset (no leak)");

    struct Counter {
        Counter() { g_asp_alive++; }
        ~Counter() { g_asp_alive--; }
    };
    g_asp_alive = 0;

    for (int i = 0; i < 1000; ++i) {
        uniuno::AtomicSharedPtr<Counter> p = uniuno::AtomicSharedPtr<Counter>::make();
        *p; // touch
    }
    TEST_ASSERT(g_asp_alive == 0, "leak detected after 1000 cycles");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: shared refcount across multiple copies, last one frees
// ---------------------------------------------------------------------------
void test_asp_shared_refcount()
{
    TEST_CASE_START("AtomicSharedPtr shared refcount lifecycle");

    struct Counter {
        Counter() { g_asp_alive++; }
        ~Counter() { g_asp_alive--; }
    };
    g_asp_alive = 0;

    uniuno::AtomicSharedPtr<Counter> a = uniuno::AtomicSharedPtr<Counter>::make();
    {
        uniuno::AtomicSharedPtr<Counter> b(a);
        uniuno::AtomicSharedPtr<Counter> c(a);
        uniuno::AtomicSharedPtr<Counter> d = b;
        TEST_ASSERT(g_asp_alive == 1, "extra allocations on copy");
    } // b, c, d destroyed -> only 'a' left
    TEST_ASSERT(g_asp_alive == 1, "over-free on copy dtor");
    a.reset();
    TEST_ASSERT(g_asp_alive == 0, "did not free on last reset");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: owner dies first, copy outlives it (lifetime safety)
// This is the REAL usage in Deferred.h (AtomicSharedPtr<AsyncResult> member
// that gets copied/moved). A per-instance inline buffer would dangle here.
// ---------------------------------------------------------------------------
void test_asp_owner_dies_first()
{
    TEST_CASE_START("AtomicSharedPtr owner-dies-before-copy");

    struct Rec {
        int id;
        Rec(int i) : id(i) { g_asp_alive++; }
        Rec(const Rec& o) : id(o.id) { g_asp_alive++; }
        ~Rec() { g_asp_alive--; }
    };
    g_asp_alive = 0;

    uniuno::AtomicSharedPtr<Rec> copy;
    {
        uniuno::AtomicSharedPtr<Rec> owner = uniuno::AtomicSharedPtr<Rec>::make(42);
        copy = owner;                              // share state
        TEST_ASSERT(g_asp_alive == 1, "extra alloc on share");
        // owner destroyed here
    }
    TEST_ASSERT(g_asp_alive == 1, "object freed while copy alive (use-after-free!)");
    TEST_ASSERT((*copy).id == 42, "copy lost value after owner died");
    copy.reset();
    TEST_ASSERT(g_asp_alive == 0, "leak: object not freed on last reset");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Correctness: multi-copy across scopes, interleaved destruction order
// ---------------------------------------------------------------------------
void test_asp_interleaved_lifetimes()
{
    TEST_CASE_START("AtomicSharedPtr interleaved lifetimes");

    struct Rec {
        Rec() { g_asp_alive++; }
        ~Rec() { g_asp_alive--; }
    };
    g_asp_alive = 0;

    uniuno::AtomicSharedPtr<Rec> a = uniuno::AtomicSharedPtr<Rec>::make();
    {
        uniuno::AtomicSharedPtr<Rec> b(a);
        uniuno::AtomicSharedPtr<Rec> c;
        c = a;                                    // c shares with a,b
        TEST_ASSERT(g_asp_alive == 1, "extra alloc on copy/share");
        // b and c destroyed here; a still alive
    }
    TEST_ASSERT(g_asp_alive == 1, "over-free on interleaved dtor");
    a.reset();
    TEST_ASSERT(g_asp_alive == 0, "leak on last reset after interleaved");

    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Benchmark: measure make / copy / deref / reset cost on serial
// ---------------------------------------------------------------------------
void bench_atomicsharedptr()
{
    Serial.println("\n[BENCH ASP] AtomicSharedPtr micro-benchmark:");

    const int ITERS = 20000;

    // --- make (heap alloc) ---
    uint32_t t0 = micros();
    for (int i = 0; i < ITERS; ++i) {
        uniuno::AtomicSharedPtr<int> p = uniuno::AtomicSharedPtr<int>::make(i);
        asm volatile("" : : "r"(&p) : "memory");
    }
    uint32_t makeUs = micros() - t0;

    // --- copy (refcount++) ---
    uniuno::AtomicSharedPtr<int> base = uniuno::AtomicSharedPtr<int>::make(1);
    t0 = micros();
    for (int i = 0; i < ITERS; ++i) {
        uniuno::AtomicSharedPtr<int> c(base);
        asm volatile("" : : "r"(&c) : "memory");
    }
    uint32_t copyUs = micros() - t0;

    // --- deref (operator*) ---
    volatile int sink = 0;
    t0 = micros();
    for (int i = 0; i < ITERS; ++i) {
        sink = *base;
    }
    uint32_t derefUs = micros() - t0;

    // --- reset (refcount-- + maybe free) ---
    t0 = micros();
    for (int i = 0; i < ITERS; ++i) {
        uniuno::AtomicSharedPtr<int> p = uniuno::AtomicSharedPtr<int>::make(i);
        p.reset();
    }
    uint32_t resetUs = micros() - t0;

    Serial.printf("  -> make  : %4u us / %d = %u ns/op\n", makeUs, ITERS,
                  (unsigned)((uint64_t)makeUs * 1000 / ITERS));
    Serial.printf("  -> copy  : %4u us / %d = %u ns/op\n", copyUs, ITERS,
                  (unsigned)((uint64_t)copyUs * 1000 / ITERS));
    Serial.printf("  -> deref : %4u us / %d = %u ns/op\n", derefUs, ITERS,
                  (unsigned)((uint64_t)derefUs * 1000 / ITERS));
    Serial.printf("  -> reset : %4u us / %d = %u ns/op\n", resetUs, ITERS,
                  (unsigned)((uint64_t)resetUs * 1000 / ITERS));
}

} // namespace

void run_atomicsharedptr_tests()
{
    TEST_SECTION_START("AtomicSharedPtr (standalone correctness + benchmark)");
    test_asp_make_deref();
    test_asp_copy_ctor();
    test_asp_copy_assign();
    test_asp_move();
    test_asp_reset_last();
    test_asp_leak_stress();
    test_asp_shared_refcount();
    test_asp_owner_dies_first();
    test_asp_interleaved_lifetimes();
    bench_atomicsharedptr();
}
