/*
  WiFiClientAsync.cpp - Non-blocking poll-driven TCP connect/write for WiFiClient
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

#include "WiFiClient.h"
#include "WiFiClientInternal.h"
#include "WiFi.h"
#include <lwip/sockets.h>
#include <errno.h>

// ---- Asynchronous connect (poll state-machine) -----------------------------
//
// Three syscalls at start (socket/fcntl/connect), then each pollConnect()
// costs ONE zero-timeout select(). loop() never stalls — lwIP performs the
// handshake in its background context while sketch code keeps running.

bool WiFiClient::connectAsync(IPAddress ip, uint16_t port)
{
    if (_connected || _asyncConnState != ConnState::Idle)
    {
        return false; // already connected or connecting
    }

    // RAII: the failure path auto-closes; success releases ownership below.
    FdGuard g(socket(AF_INET, SOCK_STREAM, 0));
    if (g.fd < 0)
    {
        log_e("socket: %d", errno);
        return false;
    }
    int sockfd = g.fd;
    // Non-blocking socket: connect() returns immediately with EINPROGRESS.
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

    uint32_t ip_addr = ip;
    struct sockaddr_in serveraddr;
    memset((char *)&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    memcpy((void *)&serveraddr.sin_addr.s_addr, (const void *)(&ip_addr), 4);
    serveraddr.sin_port = htons(port);

#ifdef ESP_IDF_VERSION_MAJOR
    int res = lwip_connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
#else
    int res = lwip_connect_r(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
#endif
    if (res < 0 && errno != EINPROGRESS)
    {
        log_e("async connect on fd %d, errno: %d", sockfd, errno);
        return false; // guard closes the descriptor
    }

    clientSocketHandle.reset(new WiFiClientSocketHandle(g.release())); // ownership transferred
    _rxBuffer.reset(new WiFiClientRxBuffer(sockfd));
    _wPendBuf = NULL; // a stale pending view from a previous session must
    _wPendLen = 0;    // never leak onto the freshly opened socket
    _epValid = false; // endpoints belong to the freshly opened socket
    _asyncConnState = ConnState::Connecting;
    _asyncStartMs = millis();
    return true;
}

bool WiFiClient::connectAsync(const char *host, uint16_t port)
{
    IPAddress srv((uint32_t)0);
    if (!WiFiGenericClass::hostByName(host, srv))
    {
        return false;
    }
    return connectAsync(srv, port); // DNS resolves synchronously (a few ms)
}

int WiFiClient::pollConnect()
{
    if (_asyncConnState == ConnState::Idle)
    {
        // Not mid-handshake: report current state so callers can treat this
        // uniformly with an ongoing connect.
        return _connected ? 1 : -1;
    }
    if (fd() < 0)
    {
        // Socket vanished mid-handshake (stop() from user code).
        _asyncConnState = ConnState::Idle;
        return -1;
    }

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(fd(), &fdset);

    struct timeval zt;
    zt.tv_sec = 0;
    zt.tv_usec = 0;

    int res = select(fd() + 1, nullptr, &fdset, nullptr, &zt);
    if (res <= 0)
    {
        // res==0: handshake still in progress. Enforce the deadline here,
        // since nothing else watches the clock during an async connect.
        if ((millis() - _asyncStartMs) > (uint32_t)_timeout)
        {
            log_i("async connect timeout (%d ms) on fd %d", _timeout, fd());
            stop();
            return -1;
        }
        return 0; // still handshaking — pump again next loop iteration
    }

    int sockerr;
    socklen_t len = (socklen_t)sizeof(int);
    res = getsockopt(fd(), SOL_SOCKET, SO_ERROR, &sockerr, &len);
    if (res < 0 || sockerr != 0)
    {
        log_e("async connect failed on fd %d, errno: %d", fd(), sockerr ? sockerr : errno);
        stop();
        return -1;
    }

    // Handshake done — apply the same tuning as the blocking path.
#define ROE_ASYNC(x, msg)                                                           \
    {                                                                               \
        if (((x) < 0))                                                              \
        {                                                                           \
            log_e("Setsockopt '" msg "'' on fd %d failed. errno: %d", fd(), errno); \
            stop();                                                                 \
            return -1;                                                              \
        }                                                                           \
    }
    struct timeval tv;
    tv.tv_sec = _timeout / 1000;
    tv.tv_usec = (_timeout % 1000) * 1000;
    int rcvBuf = 8192;
    setsockopt(fd(), SOL_SOCKET, SO_SNDBUF, &rcvBuf, sizeof(int)); // best effort
    ROE_ASYNC(setsockopt(fd(), SOL_SOCKET, SO_RCVBUF, &rcvBuf, sizeof(int)), "SO_RCVBUF");
    ROE_ASYNC(setsockopt(fd(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)), "SO_SNDTIMEO");
    ROE_ASYNC(setsockopt(fd(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), "SO_RCVTIMEO");
    int flag = 1;
    ROE_ASYNC(setsockopt(fd(), IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)), "TCP_NODELAY");

    _asyncConnState = ConnState::Idle;
    _connected = true;
    return 1;
}

// ---- Asynchronous write (zero-copy queue + drain) --------------------------
//
// First call attempts an immediate non-blocking send. Whatever the kernel did
// not take is kept as a VIEW into the caller's buffer (no heap, no copy); the
// buffer must stay valid and unmodified until writeBusy() returns false.

size_t WiFiClient::writeAsync(const uint8_t *buf, size_t size)
{
    if (!_connected || fd() < 0 || writeBusy() || !buf || size == 0)
    {
        return 0;
    }

    int res = send(fd(), (void *)buf, size, MSG_DONTWAIT);
    if (res >= 0 && (size_t)res == size)
    {
        return size; // fully accepted by the kernel in one shot
    }

    // Partial send or kernel buffer momentarily full: remember the remainder.
    size_t sent = (res > 0) ? (size_t)res : 0;
    _wPendBuf = buf + sent;
    _wPendLen = size - sent;
    if (!(res >= 0) && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)
    {
        // Hard error (connection reset etc.) — nothing to retain.
        _wPendBuf = NULL;
        _wPendLen = 0;
        stop();
    }
    return sent;
}

int WiFiClient::pollWrite()
{
    if (!_connected || fd() < 0)
    {
        return -1;
    }
    if (_wPendLen == 0)
    {
        return 1; // nothing pending -> idle/flushed
    }

    int res = send(fd(), (void *)_wPendBuf, _wPendLen, MSG_DONTWAIT);
    if (res > 0)
    {
        _wPendBuf += res;
        _wPendLen -= (size_t)res;
        if (_wPendLen == 0)
        {
            _wPendBuf = NULL;
            return 1; // fully flushed
        }
        return 0; // still draining
    }
    if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
    {
        return 0; // kernel TX buffer still full — retry next iteration
    }
    // Hard error: drop the pending view and tear down.
    _wPendBuf = NULL;
    _wPendLen = 0;
    stop();
    return -1;
}