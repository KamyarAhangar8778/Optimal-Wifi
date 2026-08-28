/*
 WiFiAPNetif.cpp - WiFi softAP Network Interface and Addressing

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

#include "WiFi.h"
#include "WiFiGeneric.h"
#include "WiFiAP.h"
#include <Optimization/CompilerTraits.h>

extern "C"
{
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <lwip/ip_addr.h>
}

// Forward declaration from WiFiGeneric
esp_netif_t *get_esp_interface_netif(esp_interface_t interface);

/**
 * Get the softAP interface IP address.
 */
IPAddress WiFiAPClass::softAPIP()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return IPAddress();
    }
    esp_netif_ip_info_t ip;
    if (UNLIKELY(esp_netif_get_ip_info(get_esp_interface_netif(ESP_IF_WIFI_AP), &ip) != ESP_OK))
    {
        log_e("Netif Get IP Failed!");
        return IPAddress();
    }
    return IPAddress(ip.ip.addr);
}

/**
 * Get the softAP broadcast IP address.
 */
IPAddress WiFiAPClass::softAPBroadcastIP()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return IPAddress();
    }
    esp_netif_ip_info_t ip;
    if (UNLIKELY(esp_netif_get_ip_info(get_esp_interface_netif(ESP_IF_WIFI_AP), &ip) != ESP_OK))
    {
        log_e("Netif Get IP Failed!");
        return IPAddress();
    }
    return WiFiGenericClass::calculateBroadcast(IPAddress(ip.gw.addr), IPAddress(ip.netmask.addr));
}

/**
 * Get the softAP network ID.
 */
IPAddress WiFiAPClass::softAPNetworkID()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return IPAddress();
    }
    esp_netif_ip_info_t ip;
    if (UNLIKELY(esp_netif_get_ip_info(get_esp_interface_netif(ESP_IF_WIFI_AP), &ip) != ESP_OK))
    {
        log_e("Netif Get IP Failed!");
        return IPAddress();
    }
    return WiFiGenericClass::calculateNetworkID(IPAddress(ip.gw.addr), IPAddress(ip.netmask.addr));
}

/**
 * Get the softAP subnet mask.
 */
IPAddress WiFiAPClass::softAPSubnetMask()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return IPAddress();
    }
    esp_netif_ip_info_t ip;
    if (UNLIKELY(esp_netif_get_ip_info(get_esp_interface_netif(ESP_IF_WIFI_AP), &ip) != ESP_OK))
    {
        log_e("Netif Get IP Failed!");
        return IPAddress();
    }
    return IPAddress(ip.netmask.addr);
}

/**
 * Get the softAP subnet CIDR.
 */
uint8_t WiFiAPClass::softAPSubnetCIDR()
{
    return WiFiGenericClass::calculateSubnetCIDR(softAPSubnetMask());
}

/**
 * Get the softAP interface MAC address.
 */
uint8_t *WiFiAPClass::softAPmacAddress(uint8_t *mac)
{
    if (LIKELY(WiFiGenericClass::getMode() != WIFI_MODE_NULL))
    {
        esp_wifi_get_mac((wifi_interface_t)WIFI_IF_AP, mac);
    }
    return mac;
}

/**
 * Get the softAP interface MAC address formatted as string.
 * Uses zero-allocation table lookup avoiding sprintf parsing overhead.
 */
String WiFiAPClass::softAPmacAddress(void)
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return String();
    }
    uint8_t mac[6];
    esp_wifi_get_mac((wifi_interface_t)WIFI_IF_AP, mac);

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
 * Get the softAP interface Host name.
 */
const char *WiFiAPClass::softAPgetHostname()
{
    const char *hostname = NULL;
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return hostname;
    }
    if (UNLIKELY(esp_netif_get_hostname(get_esp_interface_netif(ESP_IF_WIFI_AP), &hostname) != ESP_OK))
    {
        log_e("Netif Get Hostname Failed!");
    }
    return hostname;
}

/**
 * Set the softAP interface Host name.
 */
bool WiFiAPClass::softAPsetHostname(const char *hostname)
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return false;
    }
    return esp_netif_set_hostname(get_esp_interface_netif(ESP_IF_WIFI_AP), hostname) == ESP_OK;
}

/**
 * Enable IPv6 on the softAP interface.
 */
bool WiFiAPClass::softAPenableIpV6()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return false;
    }
    return esp_netif_create_ip6_linklocal(get_esp_interface_netif(ESP_IF_WIFI_AP)) == ESP_OK;
}

/**
 * Get the softAP interface IPv6 address.
 */
IPv6Address WiFiAPClass::softAPIPv6()
{
    esp_ip6_addr_t addr;
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return IPv6Address();
    }
    if (UNLIKELY(esp_netif_get_ip6_linklocal(get_esp_interface_netif(ESP_IF_WIFI_AP), &addr)))
    {
        return IPv6Address();
    }
    return IPv6Address(addr.addr);
}
