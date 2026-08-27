#ifndef TEST_ASYNC_CLIENT_CASES_H
#define TEST_ASYNC_CLIENT_CASES_H

void test_async_connect_loopback();
void test_async_bulk_write_drain();
void test_async_vs_blocking_stall();
void test_hot_path_accessors();
void test_write_slice_latency();
void test_move_semantics();

#endif // TEST_ASYNC_CLIENT_CASES_H