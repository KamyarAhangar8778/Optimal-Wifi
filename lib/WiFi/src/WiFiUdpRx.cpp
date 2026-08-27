/*
  WiFiUdpRx.cpp - RX path for WiFiUDP
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

#include "WiFiUdp.h"
#include <new> //std::nothrow
#include <lwip/sockets.h>
#include <errno.h>

#undef write
#undef read

int WiFiUDP::parsePacket()
{
  // Note: unlike stock, a leftover fully-consumed cbuf no longer blocks
  // parsing the next packet; it is reused in place.
  if (rx_buffer && rx_buffer->available())
    return 0;

  struct sockaddr_in si_other;
  int slen = sizeof(si_other), len;
  // Static staging buffer: zero heap traffic on every poll (the old code
  // malloc+free'd on EVERY call, even when no packet was waiting).
  static char stagingBuf[1460];
  if ((len = recvfrom(udp_server, stagingBuf, 1460, MSG_DONTWAIT, (struct sockaddr *)&si_other, (socklen_t *)&slen)) == -1)
  {
    if (errno == EWOULDBLOCK)
    {
      return 0;
    }
    log_e("could not receive data: %d", errno);
    return 0;
  }
  remote_ip = IPAddress(si_other.sin_addr.s_addr);
  remote_port = ntohs(si_other.sin_port);
  if (len > 0)
  {
    // Steady state reuses one cbuf across packets -> zero allocations per
    // packet. Only grows when a datagram larger than current buffer arrives.
    if (!rx_buffer || rx_buffer->size() < ((size_t)len + 1))
    {
      delete rx_buffer;
      rx_buffer = new (std::nothrow) cbuf(len);
      if (!rx_buffer)
      {
        return 0;
      }
    }
    rx_buffer->flush();
    if (rx_buffer->write(stagingBuf, len) != (size_t)len)
    {
      return 0; // defensive: never expose a partially-filled packet
    }
  }
  return len;
}

int WiFiUDP::available()
{
  if (!rx_buffer)
    return 0;
  return rx_buffer->available();
}

int WiFiUDP::read()
{
  if (!rx_buffer)
    return -1;
  int out = rx_buffer->read();
  if (!rx_buffer->available())
  {
    cbuf *b = rx_buffer;
    rx_buffer = 0;
    delete b;
  }
  return out;
}

int WiFiUDP::read(unsigned char *buffer, size_t len)
{
  return read((char *)buffer, len);
}

int WiFiUDP::read(char *buffer, size_t len)
{
  if (!rx_buffer)
    return 0;
  int out = rx_buffer->read(buffer, len);
  if (!rx_buffer->available())
  {
    cbuf *b = rx_buffer;
    rx_buffer = 0;
    delete b;
  }
  return out;
}

int WiFiUDP::peek()
{
  if (!rx_buffer)
    return -1;
  return rx_buffer->peek();
}

void WiFiUDP::flush()
{
  if (!rx_buffer)
    return;
  cbuf *b = rx_buffer;
  rx_buffer = 0;
  delete b;
}