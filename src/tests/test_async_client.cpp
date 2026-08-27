#include "test_config.h"
#include "test_async_client.h"
#include "test_async_client_cases.h"

void run_async_client_tests()
{
    TEST_SECTION_START("Asynchronous WiFiClient Tests");
    test_async_connect_loopback();
    test_async_bulk_write_drain();
    test_async_vs_blocking_stall();
    test_hot_path_accessors();
    test_write_slice_latency();
    test_move_semantics();
}