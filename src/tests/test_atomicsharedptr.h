#ifndef TEST_ATOMICSHAREDPTR_H
#define TEST_ATOMICSHAREDPTR_H

// Standalone correctness + benchmark harness for AtomicSharedPtr.
// NOTE: AtomicSharedPtr lives in include/ which is its OWN separate project.
// This test must NOT depend on Optimal-Wifi (WiFi/Events/Timer) — only on
// the AtomicSharedPtr header itself + the serial test macros in test_config.h.
void run_atomicsharedptr_tests();

#endif // TEST_ATOMICSHAREDPTR_H
