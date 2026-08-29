/*
  WiFiClientWrite.cpp - TX data path for WiFiClient
  Copyright (c) 2026 Optimal-Wifi project.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "WiFiClient.h"
#include "WiFiClientInternal.h"
#include "WiFi.h"
#include <lwip/sockets.h>
#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

size_t WiFiClient::write(uint8_t data)
{
    return write(&data, 1);
}

size_t WiFiClient::write(const uint8_t *buf, size_t size)
{
    if (!_connected || (fd() < 0) || !buf || size == 0)
    {
        return 0;
    }

    // Loop-to-full: a single send() cannot guarantee delivering `size` bytes
    // (lwIP may accept only part). Advance the cursor and retry the tail so
    // no byte is silently dropped — matching the standard Arduino contract.
    size_t totalSent = 0;
    while (totalSent < size)
    {
        // Fast path: non-blocking send. Full success returns immediately; a
        // partial success advances the cursor and retries the tail WITHOUT
        // stalling on select().
        int res = send(fd(), (void *)(buf + totalSent), size - totalSent, MSG_DONTWAIT);
        if (res > 0)
        {
            totalSent += (size_t)res;
            continue;
        }
        if (res == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
        {
            // send()==0 for a non-zero length means the peer closed with no
            // data accepted; any other non-retryable errno is a hard error.
            stop();
            return totalSent;
        }

        // Buffer-full path: bounded spin-then-block. We use a zero-timeout
        // taskYIELD() spin for the first ~500us — at loopback latency the peer
        // (or kernel TX completion) frees buffer space within a few loop
        // iterations, so a 1ms quantized delay() or a 10ms select slice
        // would dwarf the actual RTT by 3-20x. Only if the spin budget is
        // exhausted do we fall back to select() for the remainder of the
        // timeout, which re-checks the deadline each iteration to avoid
        // overshooting.
        uint32_t startMs = millis();
        const uint32_t spinBudgetUs = 500; // sub-millisecond spin: covers typical ACK turnaround
        uint32_t spinStartUs = micros();
        while (true)
        {
            uint32_t waitedMs = millis() - startMs;
            if (waitedMs >= (uint32_t)_timeout)
            {
                return totalSent; // budget exhausted
            }
            // Spin phase: tight taskYIELD loop, no select syscall overhead
            if ((micros() - spinStartUs) < spinBudgetUs)
            {
                taskYIELD(); // cooperative: lets lwIP poll task run, no 1ms quantization
                res = send(fd(), (void *)(buf + totalSent), size - totalSent, MSG_DONTWAIT);
                if (res > 0)
                {
                    totalSent += (size_t)res;
                    break; // back to outer loop for the (possibly empty) tail
                }
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                {
                    stop();
                    return totalSent;
                }
                continue;
            }

            // Fallback: select with a short slice bounded by remaining budget
            uint32_t remainMs = (uint32_t)_timeout - waitedMs;
            suseconds_t sliceUs =
                (remainMs >= 10) ? 10000 : (suseconds_t)(remainMs * 1000);

            fd_set wrset;
            struct timeval slice;
            FD_ZERO(&wrset);
            FD_SET(fd(), &wrset);
            slice.tv_sec = sliceUs / 1000000;
            slice.tv_usec = sliceUs % 1000000;

            if (select(fd() + 1, NULL, &wrset, NULL, &slice) <= 0)
            {
                continue; // no room yet — deadline re-checked above
            }

            res = send(fd(), (void *)(buf + totalSent), size - totalSent, MSG_DONTWAIT);
            if (res > 0)
            {
                totalSent += (size_t)res;
                break; // back to outer loop for the (possibly empty) tail
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            {
                stop();
                return totalSent;
            }
        }
    }
    return totalSent;
}

size_t WiFiClient::write_P(PGM_P buf, size_t size)
{
    return write(buf, size);
}

size_t WiFiClient::write(Stream &stream)
{
    // Reentrant stack buffer (1460B = MSS-aligned, 4-byte aligned): maximizes
    // per-call send() efficiency for streaming writes (MQTT/WS chunked bodies).
    uint8_t buf[1460] __attribute__((aligned(4)));
    size_t written = 0;
    while (true)
    {
        size_t avail = stream.available();
        if (avail == 0)
            break;
        size_t toRead = (avail > sizeof(buf)) ? sizeof(buf) : avail;
        size_t toWrite = stream.readBytes(buf, toRead);
        if (toWrite == 0)
            break;
        written += write(buf, toWrite);
    }
    return written;
}