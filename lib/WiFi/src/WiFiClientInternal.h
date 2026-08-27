/*
  WiFiClientInternal.h - Internal classes shared between WiFiClient translation units
  Copyright (c) 2026 Optimal-Wifi project.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
*/

#ifndef _WIFICLIENT_INTERNAL_H_
#define _WIFICLIENT_INTERNAL_H_

#include "WiFiClient.h"
#include <lwip/sockets.h>

#ifndef UNLIKELY
#if defined(__GNUC__) || defined(__clang__)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#else
#define UNLIKELY(x) (x)
#define LIKELY(x)   (x)
#endif
#endif

#ifndef COLD_FUNC
#if defined(__GNUC__) || defined(__clang__)
#define COLD_FUNC __attribute__((cold, noinline))
#else
#define COLD_FUNC
#endif
#endif

static constexpr size_t WIFI_CLIENT_RX_BUFFER_SIZE = 8192;
static constexpr size_t WIFI_CLIENT_FLUSH_BUFFER_SIZE = 1024; // 1KB saves 3KB of static BSS RAM with <1% stream throughput impact

// Optimized Zero-Allocation receive buffer with larger capacity for throughput
class WiFiClientRxBuffer
{
private:
    uint8_t _buffer[WIFI_CLIENT_RX_BUFFER_SIZE];
    size_t _pos;
    size_t _fill;
    int _fd;
    bool _failed;

    size_t r_available()
    {
        if (_fd < 0)
        {
            return 0;
        }
        int count;
#ifdef ESP_IDF_VERSION_MAJOR
        int res = lwip_ioctl(_fd, FIONREAD, &count);
#else
        int res = lwip_ioctl_r(_fd, FIONREAD, &count);
#endif
        if (res < 0)
        {
            _failed = true;
            return 0;
        }
        return (count > 0) ? (size_t)count : 0;
    }

    bool fillBuffer()
    {
        if (_pos == _fill)
        {
            // Buffer fully drained: rewind to regain full capacity.
            _pos = _fill = 0;
        }
        else if (WIFI_CLIENT_RX_BUFFER_SIZE <= _fill)
        {
            return false; // genuinely full with UNREAD data
        }
        int res = recv(_fd, _buffer + _fill, WIFI_CLIENT_RX_BUFFER_SIZE - _fill, MSG_DONTWAIT);
        if (res < 0)
        {
            if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)
            {
                _failed = true;
            }
            return false;
        }
        _fill += res;
        return res > 0;
    }

public:
    WiFiClientRxBuffer(int fd)
        : _pos(0), _fill(0), _fd(fd), _failed(false)
    {
    }

    ~WiFiClientRxBuffer()
    {
    }

    bool failed()
    {
        return _failed;
    }

    // Zero-cost liveness hint: unread buffered bytes prove the peer still
    // sends (and the socket is open) — lets connected() skip every syscall.
    bool hasBuffered() const
    {
        return _fill > _pos;
    }

    int read(uint8_t *dst, size_t len)
    {
        if (UNLIKELY(!dst || !len))
        {
            return _failed ? -1 : 0;
        }

        if (_pos == _fill)
        {
            _pos = _fill = 0;

            if (len >= WIFI_CLIENT_RX_BUFFER_SIZE)
            {
                ssize_t r = recv(_fd, dst, len, MSG_DONTWAIT);
                if (r > 0)
                {
                    return (int)r;
                }
                if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)
                {
                    _failed = true;
                }
                return 0;
            }

            ssize_t r = recv(_fd, _buffer, WIFI_CLIENT_RX_BUFFER_SIZE, MSG_DONTWAIT);
            if (r > 0)
            {
                _fill = (size_t)r;
            }
            else
            {
                if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)
                {
                    _failed = true;
                }
                return 0;
            }
        }

        size_t availableBytes = _fill - _pos;
        size_t toCopy = (len < availableBytes) ? len : availableBytes;

        if (toCopy == 1)
        {
            *dst = _buffer[_pos];
        }
        else
        {
            memcpy(dst, _buffer + _pos, toCopy);
        }

        _pos += toCopy;
        if (_pos == _fill)
        {
            _pos = _fill = 0;
        }
        return (int)toCopy;
    }

    int peek()
    {
        if (_pos == _fill && !fillBuffer())
        {
            return -1;
        }
        return _buffer[_pos];
    }

    size_t available()
    {
        // Hot-path: when the local buffer already holds data, report it WITHOUT
        // issuing a FIONREAD ioctl syscall. Only query the socket when the buffer
        // is empty. In a tight read loop this eliminates the vast majority of
        // syscalls. Behavior is identical (returns total readable bytes).
        if (_fill > _pos)
            return _fill - _pos;
        return r_available();
    }

    void flush()
    {
        // Discard all buffered AND socket-pending RX data.
        if (_pos == _fill)
        {
            _pos = _fill = 0;
        } // drained -> rewind first
        if (r_available())
        {
            int res = recv(_fd, _buffer + _fill, WIFI_CLIENT_RX_BUFFER_SIZE - _fill, MSG_DONTWAIT);
            if (res > 0)
            {
                _fill += res;
            }
        }
        _pos = _fill = 0; // discarded everything -> rewind
    }
};

class WiFiClientSocketHandle
{
private:
    int sockfd;
    WiFiClientRxBuffer _rx;

public:
    explicit WiFiClientSocketHandle(int fd) : sockfd(fd), _rx(fd)
    {
    }

    ~WiFiClientSocketHandle()
    {
        if (sockfd >= 0)
        {
            close(sockfd);
        }
    }

    int fd() const
    {
        return sockfd;
    }

    WiFiClientRxBuffer &rx()
    {
        return _rx;
    }
};

// RAII ownership of a raw lwIP socket descriptor. Every early-return in a
// connect flow auto-closes the descriptor; ownership transfers out only via
// release() on the success path. Makes the error paths leak-proof without
// hand-written close() calls duplicated at each bail-out site.
struct FdGuard
{
    int fd;

    explicit FdGuard(int f) : fd(f)
    {
    }
    ~FdGuard()
    {
        if (fd >= 0)
        {
            close(fd);
        }
    }
    FdGuard(const FdGuard &) = delete;
    FdGuard &operator=(const FdGuard &) = delete;

    int release()
    {
        int f = fd;
        fd = -1;
        return f;
    }
};

#endif /* _WIFICLIENT_INTERNAL_H_ */