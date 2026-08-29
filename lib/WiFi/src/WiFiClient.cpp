/*
  Client.cpp - Client class for Raspberry Pi
  Copyright (c) 2016 Hristo Gochkov  All right reserved.

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

static constexpr int WIFI_CLIENT_DEF_CONN_TIMEOUT_MS = 3000;

WiFiClientRxBuffer *WiFiClient::_rx() const
{
    return clientSocketHandle ? &clientSocketHandle->rx() : nullptr;
}

// Delegating constructor: one shared member-init chain, zero duplicated code.
WiFiClient::WiFiClient()
    : clientSocketHandle(), _connected(false), _timeout(WIFI_CLIENT_DEF_CONN_TIMEOUT_MS),
      _lastConnCheck(0), _asyncConnState(ConnState::Idle), _asyncStartMs(0),
      _txView(), _ep(), next(NULL)
{
}

WiFiClient::WiFiClient(int fd) : WiFiClient()
{
    clientSocketHandle = uniuno::AtomicSharedPtr<WiFiClientSocketHandle>::make(fd);
    _connected = true;
}

// Move operations: hand over the socket handle without atomic refcount churn.
WiFiClient::WiFiClient(WiFiClient &&rhs)
    : clientSocketHandle(std::move(rhs.clientSocketHandle)),
      _connected(rhs._connected), _timeout(rhs._timeout), _lastConnCheck(rhs._lastConnCheck),
      _asyncConnState(rhs._asyncConnState), _asyncStartMs(rhs._asyncStartMs),
      _txView(rhs._txView), _ep(), next(rhs.next)
{
    rhs._connected = false;
    rhs._asyncConnState = ConnState::Idle;
    rhs._txView.reset();
    rhs._ep.invalidate();
}

WiFiClient &WiFiClient::operator=(WiFiClient &&rhs)
{
    if (this != &rhs)
    {
        stop();
        clientSocketHandle = std::move(rhs.clientSocketHandle);
        _connected = rhs._connected;
        _timeout = rhs._timeout;
        _lastConnCheck = rhs._lastConnCheck;
        _asyncConnState = rhs._asyncConnState;
        _asyncStartMs = rhs._asyncStartMs;
        _txView = rhs._txView;
        _ep.invalidate();
        next = rhs.next;

        rhs._connected = false;
        rhs._asyncConnState = ConnState::Idle;
        rhs._txView.reset();
        rhs._ep.invalidate();
    }
    return *this;
}

// Copy shares the socket handle via shared_ptr — mirrors the original Arduino
// WiFiClient (no move smantics existed there). Needed so third-party libs that
// take a WiFiClient by value (e.g. ArduinoWebsockets) keep compiling.
WiFiClient::WiFiClient(const WiFiClient &other)
    : clientSocketHandle(other.clientSocketHandle),
      _connected(other._connected), _timeout(other._timeout), _lastConnCheck(other._lastConnCheck),
      _asyncConnState(other._asyncConnState), _asyncStartMs(other._asyncStartMs),
      _txView(other._txView), _ep(), next(other.next)
{
    _ep.invalidate();
}

WiFiClient::~WiFiClient()
{
    stop();
}

WiFiClient &WiFiClient::operator=(const WiFiClient &other)
{
    stop();
    clientSocketHandle = other.clientSocketHandle;
    _connected = other._connected;
    _timeout = other._timeout;
    _lastConnCheck = other._lastConnCheck;
    _asyncConnState = other._asyncConnState;
    _asyncStartMs = other._asyncStartMs;
    _txView = other._txView;
    _ep.invalidate();
    next = other.next;
    return *this;
}

void WiFiClient::stop()
{
    _asyncConnState = ConnState::Idle;
    _txView.reset();
    _ep.invalidate();
    clientSocketHandle.reset();
    _connected = false;
}

COLD_FUNC void WiFiClient::_handleBufferFailure()
{
    log_e("fail on fd %d, errno: %d, \"%s\"", fd(), errno, strerror(errno));
    stop();
}

bool WiFiClient::operator==(const WiFiClient &rhs)
{
    return clientSocketHandle == rhs.clientSocketHandle;
}

int WiFiClient::fd() const
{
    if (!clientSocketHandle)
    {
        return -1;
    }
    else
    {
        return clientSocketHandle->fd();
    }
}