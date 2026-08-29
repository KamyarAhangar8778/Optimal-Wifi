/*
  WiFiClientRead.cpp - RX data path and connection liveness for WiFiClient
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
#include <lwip/sockets.h>
#include <sys/poll.h>
#include <errno.h>

static constexpr uint32_t WIFI_CLIENT_CONN_CHECK_MS = 10;

int WiFiClient::read()
{
    uint8_t data = 0;
    int res = read(&data, 1);
    if (res < 0)
    {
        return res;
    }
    if (res == 0)
    { //  No data available.
        return -1;
    }
    return data;
}

int WiFiClient::read(uint8_t *buf, size_t size)
{
    int res = -1;
    WiFiClientRxBuffer *rx = _rx();
    if (LIKELY(rx != nullptr))
    {
        res = rx->read(buf, size);
        if (UNLIKELY(rx->failed()))
        {
            _handleBufferFailure();
        }
    }
    return res;
}

int WiFiClient::peek()
{
    int res = -1;
    WiFiClientRxBuffer *rx = _rx();
    if (LIKELY(rx != nullptr))
    {
        res = rx->peek();
        if (UNLIKELY(rx->failed()))
        {
            _handleBufferFailure();
        }
    }
    return res;
}

int WiFiClient::available()
{
    WiFiClientRxBuffer *rx = _rx();
    if (UNLIKELY(!rx))
    {
        return 0;
    }
    int res = rx->available();
    if (UNLIKELY(rx->failed()))
    {
        _handleBufferFailure();
    }
    return res;
}

// Though flushing means to send all pending data,
// seems that in Arduino it also means to clear RX
void WiFiClient::flush()
{
    WiFiClientRxBuffer *rx = _rx();
    if (rx != nullptr)
    {
        rx->flush();
    }
}

uint8_t WiFiClient::connected()
{
    if (_connected)
    {
        WiFiClientRxBuffer *rx = _rx();
        if (rx && rx->hasBuffered())
        {
            return 1;
        }
        // Throttle the socket probe: only re-probe at most once per WIFI_CLIENT_CONN_CHECK_MS.
        uint32_t now = millis();
        if ((now - _lastConnCheck) >= WIFI_CLIENT_CONN_CHECK_MS)
        {
            _lastConnCheck = now;
            // poll() with zero timeout checks TCP socket state WITHOUT entering
            // the data path (unlike recv(MSG_PEEK) which copies a byte from the
            // RX queue). POLLHUP fires on peer FIN; POLLERR/POLLHUP on RST.
            struct pollfd pfd;
            pfd.fd = fd();
            pfd.events = POLLIN; // data available OR connection closed
            pfd.revents = 0;
            int res = poll(&pfd, 1, 0);
            if (res < 0)
            {
                switch (errno)
                {
                case EBADF:
                    _connected = false;
                    log_d("Disconnected: fd closed");
                    break;
                default:
                    _connected = true; // transient — assume connected
                    break;
                }
            }
            else if (res == 0)
            {
                // No events: socket is idle but open (no data, no close)
                _connected = true;
            }
            else
            {
                // revents non-zero: data pending (POLLIN) or connection torn down
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
                {
                    _connected = false;
                    log_d("Disconnected: revents: 0x%x", pfd.revents);
                }
                else
                {
                    _connected = true; // POLLIN: data or graceful close — hasBuffered() covers it
                }
            }
        }
    }
    return _connected;
}