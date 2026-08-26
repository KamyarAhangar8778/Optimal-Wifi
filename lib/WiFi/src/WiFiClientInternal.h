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

#define WIFI_CLIENT_RX_BUFFER_SIZE (8192)

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
        return count;
    }

    bool fillBuffer()
    {
        if (_pos == _fill)
        {
            // Buffer fully drained: rewind to regains full capacity. Without
            // this the buffer saturates permanently once _fill reaches SIZE
            // and read() returns 0 forever (stall after ~8 KB cumulative).
            _pos = _fill = 0;
        }
        else if (WIFI_CLIENT_RX_BUFFER_SIZE <= _fill)
        {
            return false; // genuinely full with UNREAD data
        }
        if (!r_available())
        {
            return false;
        }
        int res = recv(_fd, _buffer + _fill, WIFI_CLIENT_RX_BUFFER_SIZE - _fill, MSG_DONTWAIT);
        if (res < 0)
        {
            if (errno != EWOULDBLOCK)
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
        if (!dst || !len)
        {
            return _failed ? -1 : 0;
        }
        if (_pos == _fill && !r_available())
            return 0;

        size_t a = _fill - _pos;
        if (len <= a)
        {
            if (len == 1)
            {
                *dst = _buffer[_pos];
            }
            else
            {
                memcpy(dst, _buffer + _pos, len);
            }
            _pos += len;
            if (_pos == _fill)
            {
                _pos = _fill = 0;
            } // drained -> rewind
            return len;
        }

        // Bulk fast-path: request exceeds buffered data. Copy what we hold,
        // then recv() the remainder STRAIGHT into the caller's buffer — no
        // intermediate bounce through the internal buffer (saves one full
        // copy per large read).
        if (a > 0)
        {
            memcpy(dst, _buffer + _pos, a);
        }
        _pos = _fill = 0; // drained -> rewind
        ssize_t r = recv(_fd, dst + a, len - a, MSG_DONTWAIT);
        if (r > 0)
        {
            a += (size_t)r;
        }
        else if (r < 0 && errno != EWOULDBLOCK && errno != EINTR)
        {
            _failed = true;
        }
        return a;
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

public:
    WiFiClientSocketHandle(int fd) : sockfd(fd)
    {
    }

    ~WiFiClientSocketHandle()
    {
        close(sockfd);
    }

    int fd()
    {
        return sockfd;
    }
};

#endif /* _WIFICLIENT_INTERNAL_H_ */