/*
 WiFiSTANetif.cpp - WiFi Station Network Interface and Addressing

 Copyright (c) 2014 Ivan Grokhotkov. All rights reserved.
 This file is part of the esp8266/esp32 core for Arduino environment.

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

#include "WiFiSTAInternal.h"
#include <esp32-hal.h>
#include "lwip/dns.h"

/**
 * Get the station interface IP address.
 */
IPAddress WiFiSTAClass::localIP()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return IPAddress();
    }
    esp_netif_ip_info_t ip;
    if (UNLIKELY(esp_netif_get_ip_info(get_esp_interface_netif(ESP_IF_WIFI_STA), &ip) != ESP_OK))
    {
        log_e("Netif Get IP Failed!");
        return IPAddress();
    }
    return IPAddress(ip.ip.addr);
}

/**
 * Get the station interface MAC address into byte array.
 */
uint8_t *WiFiSTAClass::macAddress(uint8_t *mac)
{
    if (LIKELY(WiFiGenericClass::getMode() != WIFI_MODE_NULL))
    {
        esp_wifi_get_mac((wifi_interface_t)ESP_IF_WIFI_STA, mac);
    }
    else
    {
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
    }
    return mac;
}

/**
 * Get the station interface MAC address formatted as string.
 * Uses zero-allocation table lookup avoiding sprintf parsing overhead.
 */
String WiFiSTAClass::macAddress(void)
{
    uint8_t mac[6];
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
    }
    else
    {
        esp_wifi_get_mac((wifi_interface_t)ESP_IF_WIFI_STA, mac);
    }

    static const char hex_digits[] = "0123456789ABCDEF";
    char macStr[18];
    macStr[0]  = hex_digits[(mac[0] >> 4) & 0x0F];
    macStr[1]  = hex_digits[mac[0] & 0x0F];
    macStr[2]  = ':';
    macStr[3]  = hex_digits[(mac[1] >> 4) & 0x0F];
    macStr[4]  = hex_digits[mac[1] & 0x0F];
    macStr[5]  = ':';
    macStr[6]  = hex_digits[(mac[2] >> 4) & 0x0F];
    macStr[7]  = hex_digits[mac[2] & 0x0F];
    macStr[8]  = ':';
    macStr[9]  = hex_digits[(mac[3] >> 4) & 0x0F];
    macStr[10] = hex_digits[mac[3] & 0x0F];
    macStr[11] = ':';
    macStr[12] = hex_digits[(mac[4] >> 4) & 0x0F];
    macStr[13] = hex_digits[mac[4] & 0x0F];
    macStr[14] = ':';
    macStr[15] = hex_digits[(mac[5] >> 4) & 0x0F];
    macStr[16] = hex_digits[mac[5] & 0x0F];
    macStr[17] = '\0';

    return String(macStr);
}

/**
 * Get the interface subnet mask address.
 */
IPAddress WiFiSTAClass::subnetMask()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return IPAddress();
    }
    esp_netif_ip_info_t ip;
    if (UNLIKELY(esp_netif_get_ip_info(get_esp_interface_netif(ESP_IF_WIFI_STA), &ip) != ESP_OK))
    {
        log_e("Netif Get IP Failed!");
        return IPAddress();
    }
    return IPAddress(ip.netmask.addr);
}

/**
 * Get the gateway IP address.
 */
IPAddress WiFiSTAClass::gatewayIP()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return IPAddress();
    }
    esp_netif_ip_info_t ip;
    if (UNLIKELY(esp_netif_get_ip_info(get_esp_interface_netif(ESP_IF_WIFI_STA), &ip) != ESP_OK))
    {
        log_e("Netif Get IP Failed!");
        return IPAddress();
    }
    return IPAddress(ip.gw.addr);
}

/**
 * Get the DNS IP address.
 */
IPAddress WiFiSTAClass::dnsIP(uint8_t dns_no)
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return IPAddress();
    }
    const ip_addr_t *dns_ip = dns_getserver(dns_no);
    return IPAddress(dns_ip->u_addr.ip4.addr);
}

/**
 * Get the broadcast IP address.
 */
IPAddress WiFiSTAClass::broadcastIP()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return IPAddress();
    }
    esp_netif_ip_info_t ip;
    if (UNLIKELY(esp_netif_get_ip_info(get_esp_interface_netif(ESP_IF_WIFI_STA), &ip) != ESP_OK))
    {
        log_e("Netif Get IP Failed!");
        return IPAddress();
    }
    return WiFiGenericClass::calculateBroadcast(IPAddress(ip.gw.addr), IPAddress(ip.netmask.addr));
}

/**
 * Get the network ID.
 */
IPAddress WiFiSTAClass::networkID()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return IPAddress();
    }
    esp_netif_ip_info_t ip;
    if (UNLIKELY(esp_netif_get_ip_info(get_esp_interface_netif(ESP_IF_WIFI_STA), &ip) != ESP_OK))
    {
        log_e("Netif Get IP Failed!");
        return IPAddress();
    }
    return WiFiGenericClass::calculateNetworkID(IPAddress(ip.gw.addr), IPAddress(ip.netmask.addr));
}

/**
 * Get the subnet CIDR.
 */
uint8_t WiFiSTAClass::subnetCIDR()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return (uint8_t)0;
    }
    esp_netif_ip_info_t ip;
    if (UNLIKELY(esp_netif_get_ip_info(get_esp_interface_netif(ESP_IF_WIFI_STA), &ip) != ESP_OK))
    {
        log_e("Netif Get IP Failed!");
        return IPAddress();
    }
    return WiFiGenericClass::calculateSubnetCIDR(IPAddress(ip.netmask.addr));
}

/**
 * Enable IPv6 on the station interface.
 */
bool WiFiSTAClass::enableIpV6()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return false;
    }
    return esp_netif_create_ip6_linklocal(get_esp_interface_netif(ESP_IF_WIFI_STA)) == ESP_OK;
}

/**
 * Get the station interface IPv6 address.
 */
IPv6Address WiFiSTAClass::localIPv6()
{
    esp_ip6_addr_t addr;
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return IPv6Address();
    }
    if (UNLIKELY(esp_netif_get_ip6_linklocal(get_esp_interface_netif(ESP_IF_WIFI_STA), &addr)))
    {
        return IPv6Address();
    }
    return IPv6Address(addr.addr);
}
