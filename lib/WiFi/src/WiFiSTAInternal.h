/*
  WiFiSTAInternal.h - Shared internal declarations for WiFiSTA translation units
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

#ifndef _WIFISTA_INTERNAL_H_
#define _WIFISTA_INTERNAL_H_

#include "WiFi.h"
#include "WiFiGeneric.h"
#include "WiFiSTA.h"
#include <Optimization/CompilerTraits.h>

extern "C"
{
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <lwip/ip_addr.h>
}

// Forward declarations from WiFiGeneric
esp_netif_t *get_esp_interface_netif(esp_interface_t interface);
esp_err_t set_esp_interface_dns(esp_interface_t interface, IPAddress main_dns = IPAddress(), IPAddress backup_dns = IPAddress(), IPAddress fallback_dns = IPAddress());
esp_err_t set_esp_interface_ip(esp_interface_t interface, IPAddress local_ip = INADDR_NONE, IPAddress gateway = INADDR_NONE, IPAddress subnet = INADDR_NONE, IPAddress dhcp_lease_start = INADDR_NONE);

// Internal string copy helper
FORCE_INLINE size_t _wifi_strncpy(char *dst, const char *src, size_t dst_len)
{
    if (UNLIKELY(!dst || !src || !dst_len))
    {
        return 0;
    }
    size_t src_len = strlen(src);
    if (src_len >= dst_len)
    {
        src_len = dst_len;
    }
    else
    {
        src_len += 1;
    }
    memcpy(dst, src, src_len);
    return src_len;
}

// Compare two STA configurations
FORCE_INLINE bool sta_config_equal(const wifi_config_t &lhs, const wifi_config_t &rhs)
{
    return memcmp(&lhs, &rhs, sizeof(wifi_config_t)) == 0;
}

// Populate wifi_config_t for STA mode
FORCE_INLINE void wifi_sta_config(wifi_config_t *wifi_config, const char *ssid = NULL, const char *password = NULL, const uint8_t *bssid = NULL, uint8_t channel = 0, wifi_auth_mode_t min_security = WIFI_AUTH_WPA2_PSK, wifi_scan_method_t scan_method = WIFI_ALL_CHANNEL_SCAN, wifi_sort_method_t sort_method = WIFI_CONNECT_AP_BY_SIGNAL, uint16_t listen_interval = 0, bool pmf_required = false)
{
    wifi_config->sta.channel = channel;
    wifi_config->sta.listen_interval = listen_interval;
    wifi_config->sta.scan_method = scan_method;
    wifi_config->sta.sort_method = sort_method;
    wifi_config->sta.threshold.rssi = -127;
    wifi_config->sta.pmf_cfg.capable = true;
    wifi_config->sta.pmf_cfg.required = pmf_required;
    wifi_config->sta.bssid_set = 0;
    memset(wifi_config->sta.bssid, 0, 6);
    wifi_config->sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config->sta.ssid[0] = 0;
    wifi_config->sta.password[0] = 0;
    if (ssid != NULL && ssid[0] != 0)
    {
        _wifi_strncpy((char *)wifi_config->sta.ssid, ssid, 32);
        if (password != NULL && password[0] != 0)
        {
            wifi_config->sta.threshold.authmode = min_security;
            _wifi_strncpy((char *)wifi_config->sta.password, password, 64);
        }
        if (bssid != NULL)
        {
            wifi_config->sta.bssid_set = 1;
            memcpy(wifi_config->sta.bssid, bssid, 6);
        }
    }
}

#endif /* _WIFISTA_INTERNAL_H_ */
