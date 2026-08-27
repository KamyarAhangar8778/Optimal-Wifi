#ifndef TEST_ASYNC_CLIENT_HELPERS_H
#define TEST_ASYNC_CLIENT_HELPERS_H

#include "test_config.h"

static constexpr size_t kPayloadSize = 16384;

static inline uint8_t pattern_byte(uint32_t i)
{
    return (uint8_t)(i * 31u + 7u);
}

// Drives pollConnect() until it leaves the "connecting" state, tracking how
// many other-work iterations loop() squeezed in plus the worst single stall
// observed (max gap between consecutive poll calls).
static inline int pump_connect(WiFiClient &c, uint32_t budgetMs,
                               uint32_t &iterations, uint32_t &worstStallMs)
{
    iterations = 0;
    worstStallMs = 0;
    uint32_t last = millis();
    uint32_t t0 = last;
    int rc;
    while ((rc = c.pollConnect()) == 0 && (millis() - t0) < budgetMs)
    {
        iterations++;
        uint32_t now = millis();
        uint32_t gap = now - last;
        if (gap > worstStallMs)
            worstStallMs = gap;
        last = now;
    }
    return rc;
}

#endif // TEST_ASYNC_CLIENT_HELPERS_H