/*
  Client.h - Client class for Raspberry Pi
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
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <errno.h>

#define WIFI_CLIENT_DEF_CONN_TIMEOUT_MS (3000)
#define WIFI_CLIENT_MAX_WRITE_RETRY (10)
#define WIFI_CLIENT_SELECT_TIMEOUT_US (1000000)
#define WIFI_CLIENT_FLUSH_BUFFER_SIZE (4096)
#define WIFI_CLIENT_CONN_CHECK_MS (50) // throttle window for connected() probe

#undef connect
#undef write
#undef read

// WiFiClientRxBuffer and WiFiClientSocketHandle moved to WiFiClientInternal.h
// so the async implementation (WiFiClientAsync.cpp) can share them without
// duplication.

WiFiClient::WiFiClient() : _rxBuffer(nullptr), _connected(false), _timeout(WIFI_CLIENT_DEF_CONN_TIMEOUT_MS), next(NULL),
                           _asyncConnState(0), _asyncStartMs(0), _wPendBuf(NULL), _wPendLen(0),
                           _epValid(false), _peerIp(), _peerPort(0), _locAddr(), _locPort(0)
{
    _lastConnCheck = 0;
}

WiFiClient::WiFiClient(int fd) : _connected(true), _timeout(WIFI_CLIENT_DEF_CONN_TIMEOUT_MS), next(NULL),
                                 _asyncConnState(0), _asyncStartMs(0), _wPendBuf(NULL), _wPendLen(0),
                                 _epValid(false), _peerIp(), _peerPort(0), _locAddr(), _locPort(0)
{
    clientSocketHandle.reset(new WiFiClientSocketHandle(fd));
    _rxBuffer.reset(new WiFiClientRxBuffer(fd));
    _lastConnCheck = 0;
}

WiFiClient::~WiFiClient()
{
    stop();
}

WiFiClient &WiFiClient::operator=(const WiFiClient &other)
{
    stop();
    clientSocketHandle = other.clientSocketHandle;
    _rxBuffer = other._rxBuffer;
    _connected = other._connected;
    _asyncConnState = other._asyncConnState;
    _asyncStartMs = other._asyncStartMs;
    _wPendBuf = other._wPendBuf;
    _wPendLen = other._wPendLen;
    _epValid = false; // never inherit a cache resolved for another socket
    return *this;
}

void WiFiClient::stop()
{
    _asyncConnState = 0; // abort any in-flight async connect/print path
    _wPendBuf = NULL;
    _wPendLen = 0;
    _epValid = false; // endpoints belong to the closed socket
    clientSocketHandle = NULL;
    _rxBuffer = NULL;
    _connected = false;
}

int WiFiClient::connect(IPAddress ip, uint16_t port)
{
    return connect(ip, port, _timeout);
}
int WiFiClient::connect(IPAddress ip, uint16_t port, int32_t timeout_ms)
{
    _timeout = timeout_ms;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        log_e("socket: %d", errno);
        return 0;
    }
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

    uint32_t ip_addr = ip;
    struct sockaddr_in serveraddr;
    memset((char *)&serveraddr, 0, sizeof(serveraddr));
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
        close(sockfd);
        return 0;
    }

    res = select(sockfd + 1, nullptr, &fdset, nullptr, _timeout < 0 ? nullptr : &tv);
    if (res < 0)
    {
        log_e("select on fd %d, errno: %d, \"%s\"", sockfd, errno, strerror(errno));
        close(sockfd);
        return 0;
    }
    else if (res == 0)
    {
        log_i("select returned due to timeout %d ms for fd %d", _timeout, sockfd);
        close(sockfd);
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
            close(sockfd);
            return 0;
        }

        if (sockerr != 0)
        {
            log_e("socket error on fd %d, errno: %d, \"%s\"", sockfd, sockerr, strerror(sockerr));
            close(sockfd);
            return 0;
        }
    }

#define ROE_WIFICLIENT(x, msg)                                                                                 \
    {                                                                                                          \
        if (((x) < 0))                                                                                         \
        {                                                                                                      \
            log_e("Setsockopt '" msg "'' on fd %d failed. errno: %d, \"%s\"", sockfd, errno, strerror(errno)); \
            return 0;                                                                                          \
        }                                                                                                      \
    }
    int rcvBuf = 8192;
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &rcvBuf, sizeof(int)); // Best effort - lwip may not support
    ROE_WIFICLIENT(setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvBuf, sizeof(int)), "SO_RCVBUF");
    ROE_WIFICLIENT(setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)), "SO_SNDTIMEO");
    ROE_WIFICLIENT(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), "SO_RCVTIMEO");

    // Enable TCP_NODELAY (disable Nagle's algorithm) for low-latency small packets
    int flag = 1;
    ROE_WIFICLIENT(setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)), "TCP_NODELAY");

    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) & (~O_NONBLOCK));
    clientSocketHandle.reset(new WiFiClientSocketHandle(sockfd));
    _rxBuffer.reset(new WiFiClientRxBuffer(sockfd));
    _epValid = false; // endpoints of the freshly opened socket are unknown yet

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

size_t WiFiClient::write(uint8_t data)
{
    return write(&data, 1);
}

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

size_t WiFiClient::write(const uint8_t *buf, size_t size)
{
    if (!_connected || (fd() < 0))
    {
        return 0;
    }

    // Fast path: non-blocking send succeeds instantly when the socket send
    // buffer has room (the overwhelmingly common case for small/medium writes).
    int res = send(fd(), (void *)buf, size, MSG_DONTWAIT);
    if (res >= 0)
    {
        return res;
    }

    // Buffer-full path: bounded select-retry. Wait in short slices (never an
    // unbounded blocking send) and push data the moment the buffer frees up.
    // The deadline is derived from millis() — NOT from counting assumed slice
    // durations — so OS timer quantization can never overshoot the budget by
    // more than a single slice. Partial sends are accepted (Arduino contract).
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
    {
        uint32_t startMs = millis();
        while (true)
        {
            uint32_t waitedMs = millis() - startMs;
            if (waitedMs >= (uint32_t)_timeout)
            {
                break; // real-time budget exhausted -> report short write
            }
            uint32_t remainMs = (uint32_t)_timeout - waitedMs;
            // Cap the slice at 10ms: bounds worst-case loop stall tightly,
            // and the final slice shrinks to exactly the remaining budget.
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
                continue; // slice elapsed with no room — deadline re-checked above
            }

            res = send(fd(), (void *)buf, size, MSG_DONTWAIT);
            if (res >= 0)
            {
                return res;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            {
                break; // hard error -> fall through to teardown below
            }
        }
        // Timed out waiting for buffer space: report short write (0), do NOT
        // tear down the connection — caller may retry later.
        return 0;
    }
    // Unrecoverable hard error (e.g. connection reset) — tear down the socket.
    stop();
    return 0;
}

size_t WiFiClient::write_P(PGM_P buf, size_t size)
{
    return write(buf, size);
}

size_t WiFiClient::write(Stream &stream)
{
    // Zero-Allocation: use static buffer (align to 4 bytes for optimal DMA)
    static uint8_t buf[WIFI_CLIENT_FLUSH_BUFFER_SIZE] __attribute__((aligned(4)));
    size_t written = 0;
    while (true)
    {
        size_t avail = stream.available();
        if (avail == 0)
            break;
        size_t toRead = (avail > WIFI_CLIENT_FLUSH_BUFFER_SIZE) ? WIFI_CLIENT_FLUSH_BUFFER_SIZE : avail;
        size_t toWrite = stream.readBytes(buf, toRead);
        if (toWrite == 0)
            break;
        written += write(buf, toWrite);
    }
    return written;
}

int WiFiClient::read(uint8_t *buf, size_t size)
{
    int res = -1;
    if (_rxBuffer)
    {
        res = _rxBuffer->read(buf, size);
        if (_rxBuffer->failed())
        {
            log_e("fail on fd %d, errno: %d, \"%s\"", fd(), errno, strerror(errno));
            stop();
        }
    }
    return res;
}

int WiFiClient::peek()
{
    int res = -1;
    if (_rxBuffer)
    {
        res = _rxBuffer->peek();
        if (_rxBuffer->failed())
        {
            log_e("fail on fd %d, errno: %d, \"%s\"", fd(), errno, strerror(errno));
            stop();
        }
    }
    return res;
}

int WiFiClient::available()
{
    if (!_rxBuffer)
    {
        return 0;
    }
    int res = _rxBuffer->available();
    if (_rxBuffer->failed())
    {
        log_e("fail on fd %d, errno: %d, \"%s\"", fd(), errno, strerror(errno));
        stop();
    }
    return res;
}

// Though flushing means to send all pending data,
// seems that in Arduino it also means to clear RX
void WiFiClient::flush()
{
    if (_rxBuffer != nullptr)
    {
        _rxBuffer->flush();
    }
}

uint8_t WiFiClient::connected()
{
    if (_connected)
    {
        // Hot-path #1: unread buffered data proves the peer is alive and the
        // socket open — answer from RAM with ZERO syscalls and no millis().
        if (_rxBuffer && _rxBuffer->hasBuffered())
        {
            return 1;
        }
        // Throttle the socket probe: a recv() probe per call is wasteful when
        // connected() is polled every loop iteration. Only re-probe at most once
        // per WIFI_CLIENT_CONN_CHECK_MS.
        uint32_t now = millis();
        if ((now - _lastConnCheck) >= WIFI_CLIENT_CONN_CHECK_MS)
        {
            _lastConnCheck = now;
            uint8_t dummy;
            int res = recv(fd(), &dummy, 0, MSG_DONTWAIT);
            // avoid unused var warning by gcc
            (void)res;
            // recv only sets errno if res is <= 0
            if (res <= 0)
            {
                switch (errno)
                {
                case EWOULDBLOCK:
                case ENOENT: // caused by vfs
                    _connected = true;
                    break;
                case ENOTCONN:
                case EPIPE:
                case ECONNRESET:
                case ECONNREFUSED:
                case ECONNABORTED:
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

// Resolve both TCP endpoints ONCE per connection; afterwards every no-arg
// remoteIP/Port/localIP/Port call is a pure RAM read (TCP endpoints are
// immutable for the life of the connection). The fd-argument variants stay
// uncached — they exist precisely to query arbitrary sockets.
void WiFiClient::cacheEndpoints() const
{
    if (_epValid || fd() < 0)
    {
        return;
    }
    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    if (getpeername(fd(), (struct sockaddr *)&addr, &len) >= 0)
    {
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        _peerIp = IPAddress((uint32_t)(s->sin_addr.s_addr));
        _peerPort = ntohs(s->sin_port);
    }
    len = sizeof addr;
    if (getsockname(fd(), (struct sockaddr *)&addr, &len) >= 0)
    {
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        _locAddr = IPAddress((uint32_t)(s->sin_addr.s_addr));
        _locPort = ntohs(s->sin_port);
    }
    _epValid = true; // mark even on failure: retrying a dead socket each call
                     // would reintroduce exactly the cost we are removing
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
    cacheEndpoints();
    return _peerIp;
}

uint16_t WiFiClient::remotePort() const
{
    cacheEndpoints();
    return _peerPort;
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
    cacheEndpoints();
    return _locAddr;
}

uint16_t WiFiClient::localPort() const
{
    cacheEndpoints();
    return _locPort;
}

bool WiFiClient::operator==(const WiFiClient &rhs)
{
    return clientSocketHandle == rhs.clientSocketHandle && remotePort() == rhs.remotePort() && remoteIP() == rhs.remoteIP();
}

int WiFiClient::fd() const
{
    if (clientSocketHandle == NULL)
    {
        return -1;
    }
    else
    {
        return clientSocketHandle->fd();
    }
}