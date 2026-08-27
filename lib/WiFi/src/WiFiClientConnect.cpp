/*
  WiFiClientConnect.cpp - TCP connect path and socket tuning for WiFiClient
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

#define ROE_CFG(x, msg)                                                          \
    {                                                                            \
        if (((x) < 0))                                                           \
        {                                                                        \
            log_e("Setsockopt '" msg "' on fd %d failed. errno: %d, \"%s\"", fd, \
                  errno, strerror(errno));                                     \
            return -1;                                                           \
        }                                                                        \
    }

// Shared socket tuning applied immediately after the TCP handshake completes.
// Centralizing here eliminates duplicated setsockopt calls between blocking
// connect() and async pollConnect().
int WiFiClient::_configureSocket(int fd, int timeout_ms)
{
    struct timeval tv;
    if (timeout_ms < 0)
    {
        // Negative timeout = infinite (blocking). lwIP treats a zero timeval
        // as "no timeout" for SO_RCVTIMEO/SO_SNDTIMEO.
        tv.tv_sec = 0;
        tv.tv_usec = 0;
    }
    else
    {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
    }

    int rcvBuf = 8192;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &rcvBuf, sizeof(int)); // Best effort
    ROE_CFG(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvBuf, sizeof(int)), "SO_RCVBUF");
    ROE_CFG(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)), "SO_SNDTIMEO");
    ROE_CFG(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), "SO_RCVTIMEO");

    int flag = 1;
    ROE_CFG(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)), "TCP_NODELAY");
    return 0;
}

int WiFiClient::connect(IPAddress ip, uint16_t port)
{
    return connect(ip, port, _timeout);
}
int WiFiClient::connect(IPAddress ip, uint16_t port, int32_t timeout_ms)
{
    _timeout = timeout_ms;
    // RAII: any early return below auto-closes the descriptor. Ownership is
    // handed to the socket handle only on the success path.
    FdGuard g(socket(AF_INET, SOCK_STREAM, 0));
    if (g.fd < 0)
    {
        log_e("socket: %d", errno);
        return 0;
    }
    int sockfd = g.fd;
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

    // Tune buffers BEFORE the handshake so SO_RCVBUF/SO_SNDBUF influence the
    // TCP window scaling advertised during SYN/SYN-ACK exchange.
    if (_configureSocket(sockfd, _timeout) < 0)
    {
        return 0;
    }

    uint32_t ip_addr = ip;
    struct sockaddr_in serveraddr = {};
    serveraddr.sin_family = AF_INET;
    memcpy((void *)&serveraddr.sin_addr.s_addr, (const void *)(&ip_addr), 4);
    serveraddr.sin_port = htons(port);
    fd_set fdset;
    struct timeval tv;
    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);
    tv.tv_sec = _timeout / 1000;
    tv.tv_usec = (_timeout % 1000) * 1000;

#ifdef ESP_IDF_VERSION_MAJOR
    int res = lwip_connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
#else
    int res = lwip_connect_r(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
#endif
    if (res < 0 && errno != EINPROGRESS)
    {
        log_e("connect on fd %d, errno: %d, \"%s\"", sockfd, errno, strerror(errno));
        return 0; // guard closes the descriptor
    }

    res = select(sockfd + 1, nullptr, &fdset, nullptr, _timeout < 0 ? nullptr : &tv);
    if (res < 0)
    {
        log_e("select on fd %d, errno: %d, \"%s\"", sockfd, errno, strerror(errno));
        return 0;
    }
    else if (res == 0)
    {
        log_i("select returned due to timeout %d ms for fd %d", _timeout, sockfd);
        return 0;
    }
    else
    {
        int sockerr;
        socklen_t len = (socklen_t)sizeof(int);
        res = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &sockerr, &len);

        if (res < 0)
        {
            log_e("getsockopt on fd %d, errno: %d, \"%s\"", sockfd, errno, strerror(errno));
            return 0; // guard closes the descriptor
        }

        if (sockerr != 0)
        {
            log_e("socket error on fd %d, errno: %d, \"%s\"", sockfd, sockerr, strerror(sockerr));
            return 0;
        }
    }

    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) & (~O_NONBLOCK));
    clientSocketHandle.reset(new WiFiClientSocketHandle(g.release())); // ownership transferred
    _txView.reset();
    _asyncConnState = ConnState::Idle;
    _ep.invalidate();

    _connected = true;
    return 1;
}

int WiFiClient::connect(const char *host, uint16_t port)
{
    return connect(host, port, _timeout);
}

int WiFiClient::connect(const char *host, uint16_t port, int32_t timeout_ms)
{
    IPAddress srv((uint32_t)0);
    if (!WiFiGenericClass::hostByName(host, srv))
    {
        return 0;
    }
    return connect(srv, port, timeout_ms);
}