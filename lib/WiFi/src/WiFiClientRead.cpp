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
#include <errno.h>

static constexpr uint32_t WIFI_CLIENT_CONN_CHECK_MS = 50;

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
            uint8_t dummy;
            int res = recv(fd(), &dummy, 1, MSG_PEEK | MSG_DONTWAIT);
            if (res == 0)
            {
                // Peer closed connection cleanly (received TCP FIN)
                _connected = false;
            }
            else if (res < 0)
            {
                switch (errno)
                {
                case EWOULDBLOCK:
#if defined(EAGAIN) && (EAGAIN != EWOULDBLOCK)
                case EAGAIN:
#endif
                case ENOENT: // caused by vfs
                    _connected = true;
                    break;
                case ENOTCONN:
                case EPIPE:
                case ECONNRESET:
                case ECONNREFUSED:
                case ECONNABORTED:
                case EBADF:
                    _connected = false;
                    log_d("Disconnected: RES: %d, ERR: %d", res, errno);
                    break;
                default:
                    log_i("Unexpected: RES: %d, ERR: %d", res, errno);
                    _connected = true;
                    break;
                }
            }
            else
            {
                _connected = true;
            }
        }
    }
    return _connected;
}