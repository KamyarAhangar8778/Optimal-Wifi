#ifndef TEST_FASTFUNCTION_H
#define TEST_FASTFUNCTION_H

// Standalone correctness + benchmark harness for FastFunction.
// NOTE: FastFunction lives in include/ which is its OWN separate project.
// This test must NOT depend on Optimal-Wifi (WiFi/Events/Timer) — only on
// the FastFunction header itself + the serial test macros in test_config.h.
void run_fastfunction_tests();

#endif // TEST_FASTFUNCTION_H
