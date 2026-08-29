/*
  WiFiClientSocketOps.cpp - Socket option getters/setters for WiFiClient
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

int WiFiClient::setSocketOption(int option, char *value, size_t len)
{
    return setSocketOption(SOL_SOCKET, option, (const void *)value, len);
}

int WiFiClient::setSocketOption(int level, int option, const void *value, size_t len)
{
    int res = setsockopt(fd(), level, option, value, len);
    if (res < 0)
    {
        log_e("fail on %d, errno: %d, \"%s\"", fd(), errno, strerror(errno));
    }
    return res;
}

int WiFiClient::setTimeout(uint32_t seconds)
{
    Client::setTimeout(seconds * 1000); // This should be here?
    _timeout = seconds * 1000;
    if (fd() >= 0)
    {
        struct timeval tv;
        tv.tv_sec = seconds;
        tv.tv_usec = 0;
        if (setSocketOption(SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval)) < 0)
        {
            return -1;
        }
        return setSocketOption(SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval));
    }
    else
    {
        return 0;
    }
}

int WiFiClient::setOption(int option, int *value)
{
    return setSocketOption(IPPROTO_TCP, option, (const void *)value, sizeof(int));
}

int WiFiClient::getOption(int option, int *value)
{
    socklen_t size = sizeof(int);
    int res = getsockopt(fd(), IPPROTO_TCP, option, (char *)value, &size);
    if (res < 0)
    {
        log_e("fail on fd %d, errno: %d, \"%s\"", fd(), errno, strerror(errno));
    }
    return res;
}

int WiFiClient::setNoDelay(bool nodelay)
{
    int flag = nodelay;
    return setOption(TCP_NODELAY, &flag);
}

bool WiFiClient::getNoDelay()
{
    int flag = 0;
    getOption(TCP_NODELAY, &flag);
    return flag;
}

int WiFiClient::setKeepAlive(bool enable, int idleSec, int intervalSec, int count)
{
    int on = enable ? 1 : 0;
    int res = setSocketOption(SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
    if (res < 0 || !enable)
        return res;
    setSocketOption(IPPROTO_TCP, TCP_KEEPIDLE, &idleSec, sizeof(idleSec));
    setSocketOption(IPPROTO_TCP, TCP_KEEPINTVL, &intervalSec, sizeof(intervalSec));
    return setSocketOption(IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
}