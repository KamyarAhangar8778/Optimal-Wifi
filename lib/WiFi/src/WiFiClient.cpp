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
#define WIFI_CLIENT_CONN_CHECK_MS (50) // throttle window for connected() probe

#undef connect
#undef write
#undef read

// WiFiClientRxBuffer and WiFiClientSocketHandle moved to WiFiClientInternal.h
// so the async implementation (WiFiClientAsync.cpp) can share them without
// duplication.

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
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int rcvBuf = 8192;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &rcvBuf, sizeof(int)); // Best effort
    ROE_CFG(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvBuf, sizeof(int)), "SO_RCVBUF");
    ROE_CFG(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)), "SO_SNDTIMEO");
    ROE_CFG(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)), "SO_RCVTIMEO");

    int flag = 1;
    ROE_CFG(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)), "TCP_NODELAY");
    return 0;
}

void EndpointCache::refresh(int fd) const
{
    if (valid || fd < 0)
    {
        return;
    }
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getpeername(fd, (struct sockaddr *)&addr, &len) >= 0)
    {
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        peerIp = IPAddress((uint32_t)(s->sin_addr.s_addr));
        peerPort = ntohs(s->sin_port);
    }
    len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &len) >= 0)
    {
        struct sockaddr_in *s = (struct sockaddr_in *)&addr;
        localIp = IPAddress((uint32_t)(s->sin_addr.s_addr));
        localPort = ntohs(s->sin_port);
    }
    valid = true;
}

WiFiClientRxBuffer *WiFiClient::_rx() const
{
    return clientSocketHandle ? &clientSocketHandle->rx() : nullptr;
}

// Delegating constructor: one shared member-init chain, zero duplicated code.
WiFiClient::WiFiClient()
    : clientSocketHandle(nullptr), _connected(false), _timeout(WIFI_CLIENT_DEF_CONN_TIMEOUT_MS),
      _lastConnCheck(0), _asyncConnState(ConnState::Idle), _asyncStartMs(0),
      _txView(), _ep(), next(NULL)
{
}

WiFiClient::WiFiClient(int fd) : WiFiClient()
{
    clientSocketHandle.reset(new WiFiClientSocketHandle(fd));
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
    clientSocketHandle = nullptr;
    _connected = false;
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

    if (_configureSocket(sockfd, _timeout) < 0)
    {
        return 0;
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
    if (!_connected || (fd() < 0) || !buf || size == 0)
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
    // Reentrant stack buffer (512B aligned to 4 bytes): saves 1KB .bss RAM and prevents multithreading data race
    uint8_t buf[512] __attribute__((aligned(4)));
    size_t written = 0;
    while (true)
    {
        size_t avail = stream.available();
        if (avail == 0)
            break;
        size_t toRead = (avail > sizeof(buf)) ? sizeof(buf) : avail;
        size_t toWrite = stream.readBytes(buf, toRead);
        if (toWrite == 0)
            break;
        written += write(buf, toWrite);
    }
    return written;
}

COLD_FUNC void WiFiClient::_handleBufferFailure()
{
    log_e("fail on fd %d, errno: %d, \"%s\"", fd(), errno, strerror(errno));
    stop();
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

bool WiFiClient::operator==(const WiFiClient &rhs)
{
    return clientSocketHandle == rhs.clientSocketHandle;
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