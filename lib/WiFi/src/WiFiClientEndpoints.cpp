/*
  WiFiClientEndpoints.cpp - IP/port introspection for WiFiClient
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

void EndpointCache::refresh(int fd) const
{
    if (valid || fd < 0)
    {
        return;
    }
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getpeername(fd, (struct sockaddr *)&addr, &len) < 0)
    {
        return; // don't cache a half-populated struct on failure
    }
    struct sockaddr_in *s = (struct sockaddr_in *)&addr;
    peerIp = IPAddress((uint32_t)(s->sin_addr.s_addr));
    peerPort = ntohs(s->sin_port);

    len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &len) < 0)
    {
        return; // same as above — leave valid=false for a later retry
    }
    s = (struct sockaddr_in *)&addr;
    localIp = IPAddress((uint32_t)(s->sin_addr.s_addr));
    localPort = ntohs(s->sin_port);

    valid = true;
}

void WiFiClient::cacheEndpoints() const
{
    _ep.refresh(fd());
}

IPAddress WiFiClient::remoteIP(int fd) const
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    getpeername(fd, (struct sockaddr *)&addr, &len);
    struct sockaddr_in *s = (struct sockaddr_in *)&addr;
    return IPAddress((uint32_t)(s->sin_addr.s_addr));
}

uint16_t WiFiClient::remotePort(int fd) const
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    getpeername(fd, (struct sockaddr *)&addr, &len);
    struct sockaddr_in *s = (struct sockaddr_in *)&addr;
    return ntohs(s->sin_port);
}

IPAddress WiFiClient::remoteIP() const
{
    _ep.refresh(fd());
    return _ep.peerIp;
}

uint16_t WiFiClient::remotePort() const
{
    _ep.refresh(fd());
    return _ep.peerPort;
}

IPAddress WiFiClient::localIP(int fd) const
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    getsockname(fd, (struct sockaddr *)&addr, &len);
    struct sockaddr_in *s = (struct sockaddr_in *)&addr;
    return IPAddress((uint32_t)(s->sin_addr.s_addr));
}

uint16_t WiFiClient::localPort(int fd) const
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    getsockname(fd, (struct sockaddr *)&addr, &len);
    struct sockaddr_in *s = (struct sockaddr_in *)&addr;
    return ntohs(s->sin_port);
}

IPAddress WiFiClient::localIP() const
{
    _ep.refresh(fd());
    return _ep.localIp;
}

uint16_t WiFiClient::localPort() const
{
    _ep.refresh(fd());
    return _ep.localPort;
}