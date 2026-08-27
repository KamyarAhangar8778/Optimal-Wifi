/*
  Client.h - Base class that provides Client
  Copyright (c) 2011 Adrian McEwen.  All right reserved.

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

#ifndef _WIFICLIENT_H_
#define _WIFICLIENT_H_

#include "Arduino.h"
#include "Client.h"
#include <memory>

class WiFiClientSocketHandle;
class WiFiClientRxBuffer;

struct EndpointCache
{
    mutable IPAddress peerIp;
    mutable IPAddress localIp;
    mutable uint16_t peerPort;
    mutable uint16_t localPort;
    mutable bool valid;

    EndpointCache() : peerIp((uint32_t)0), localIp((uint32_t)0), peerPort(0), localPort(0), valid(false)
    {
    }

    void invalidate() const
    {
        valid = false;
    }

    void refresh(int fd) const;
};

class AsyncTxView
{
private:
    const uint8_t *_buf;
    size_t _len;

public:
    AsyncTxView() : _buf(nullptr), _len(0)
    {
    }

    bool isBusy() const
    {
        return _len != 0;
    }

    size_t pending() const
    {
        return _len;
    }

    void reset()
    {
        _buf = nullptr;
        _len = 0;
    }

    void set(const uint8_t *buf, size_t len)
    {
        _buf = buf;
        _len = len;
    }

    const uint8_t *data() const
    {
        return _buf;
    }

    void advance(size_t n)
    {
        if (n >= _len)
        {
            reset();
        }
        else
        {
            _buf += n;
            _len -= n;
        }
    }
};

class ESPLwIPClient : public Client
{
public:
    virtual int connect(IPAddress ip, uint16_t port, int32_t timeout) = 0;
    virtual int connect(const char *host, uint16_t port, int32_t timeout) = 0;
    virtual int setTimeout(uint32_t seconds) = 0;
};

class WiFiClient : public ESPLwIPClient
{
protected:
    // Typed state for the async connect machine (compiles to the same byte
    // as the old uint8_t flag, but the compiler now rejects bogus values).
    enum class ConnState : uint8_t
    {
        Idle,
        Connecting
    };

    std::shared_ptr<WiFiClientSocketHandle> clientSocketHandle;
    bool _connected;
    int _timeout;
    uint32_t _lastConnCheck; // throttle window for connected() socket probe
    // Async poll state-machine (see WiFiClientAsync.cpp)
    ConnState _asyncConnState;
    uint32_t _asyncStartMs; // connect deadline reference
    AsyncTxView _txView;    // zero-copy pending TX view into caller buffer
    EndpointCache _ep;      // cached TCP endpoints

    void _handleBufferFailure(); // Cold-path error helper
    WiFiClientRxBuffer *_rx() const;

public:
    WiFiClient *next;
    WiFiClient();
    WiFiClient(int fd);
    WiFiClient(WiFiClient &&rhs);            // transfer socket w/o refcount churn
    WiFiClient &operator=(WiFiClient &&rhs); // ditto
    ~WiFiClient();
    int connect(IPAddress ip, uint16_t port);
    int connect(IPAddress ip, uint16_t port, int32_t timeout_ms);
    int connect(const char *host, uint16_t port);
    int connect(const char *host, uint16_t port, int32_t timeout_ms);
    size_t write(uint8_t data);
    size_t write(const uint8_t *buf, size_t size);
    size_t write_P(PGM_P buf, size_t size);
    size_t write(Stream &stream);
    int available();
    int read();
    int read(uint8_t *buf, size_t size);
    int peek();
    void flush();
    void stop();
    uint8_t connected();

    operator bool()
    {
        return connected();
    }
    WiFiClient &operator=(const WiFiClient &other);
    bool operator==(const bool value)
    {
        return bool() == value;
    }
    bool operator!=(const bool value)
    {
        return bool() != value;
    }
    bool operator==(const WiFiClient &);
    bool operator!=(const WiFiClient &rhs)
    {
        return !this->operator==(rhs);
    };

    virtual int fd() const;

    int setSocketOption(int option, char *value, size_t len);
    int setSocketOption(int level, int option, const void *value, size_t len);
    int setOption(int option, int *value);
    int getOption(int option, int *value);
    int setTimeout(uint32_t seconds);
    int setNoDelay(bool nodelay);
    bool getNoDelay();

    // ---- Asynchronous API (non-blocking, poll-driven) ----
    // Start a TCP handshake without stalling loop(). Returns false only on
    // immediate failure (socket/DNS). Progress it with pollConnect().
    // NOTE: the host variant resolves DNS synchronously (typically a few ms).
    bool connectAsync(IPAddress ip, uint16_t port);
    bool connectAsync(const char *host, uint16_t port);
    // Pump the async connect. Returns 1 = connected, 0 = still handshaking,
    // -1 = failed or timed out (deadline = _timeout ms).
    int pollConnect();
    bool isConnecting() const
    {
        return _asyncConnState != ConnState::Idle;
    }

    // Queue bytes for transmission WITHOUT ever blocking. Returns bytes
    // accepted by the kernel right away; the remainder is remembered as a
    // zero-copy view into the CALLER's buffer — it must stay valid and
    // unmodified until writeBusy() turns false. Rejects (returns 0) while a
    // previous chunk is still flushing.
    size_t writeAsync(const uint8_t *buf, size_t size);
    // Flush pending TX. Returns 1 = all flushed, 0 = still pending, -1 = error.
    int pollWrite();
    size_t pendingWrite() const
    {
        return _txView.pending();
    }
    bool writeBusy() const
    {
        return _txView.isBusy();
    }

    void cacheEndpoints() const;

    IPAddress remoteIP() const;
    IPAddress remoteIP(int fd) const;
    uint16_t remotePort() const;
    uint16_t remotePort(int fd) const;
    IPAddress localIP() const;
    IPAddress localIP(int fd) const;
    uint16_t localPort() const;
    uint16_t localPort(int fd) const;

    // friend class WiFiServer;
    using Print::write;
};

#endif /* _WIFICLIENT_H_ */
