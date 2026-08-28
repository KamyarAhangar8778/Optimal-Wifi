/*
 ESP8266WiFiAP.cpp - WiFi library for esp8266

 Copyright (c) 2014 Ivan Grokhotkov. All rights reserved.
 This file is part of the esp8266 core for Arduino environment.

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

 Reworked on 28 Dec 2015 by Markus Sattler
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
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <lwip/ip_addr.h>
#include "dhcpserver/dhcpserver_options.h"
}

// Forward declaration from WiFiGeneric
esp_err_t set_esp_interface_ip(esp_interface_t interface, IPAddress local_ip = INADDR_NONE, IPAddress gateway = INADDR_NONE, IPAddress subnet = INADDR_NONE, IPAddress dhcp_lease_start = INADDR_NONE);

static size_t _wifi_strncpy(char *dst, const char *src, size_t dst_len)
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

/**
 * Compare two AP configurations
 */
static bool softap_config_equal(const wifi_config_t &lhs, const wifi_config_t &rhs)
{
    if (strncmp(reinterpret_cast<const char *>(lhs.ap.ssid), reinterpret_cast<const char *>(rhs.ap.ssid), 32) != 0)
    {
        return false;
    }
    if (strncmp(reinterpret_cast<const char *>(lhs.ap.password), reinterpret_cast<const char *>(rhs.ap.password), 64) != 0)
    {
        return false;
    }
    return (lhs.ap.channel == rhs.ap.channel) &&
           (lhs.ap.authmode == rhs.ap.authmode) &&
           (lhs.ap.ssid_hidden == rhs.ap.ssid_hidden) &&
           (lhs.ap.max_connection == rhs.ap.max_connection) &&
           (lhs.ap.pairwise_cipher == rhs.ap.pairwise_cipher) &&
           (lhs.ap.ftm_responder == rhs.ap.ftm_responder);
}

void wifi_softap_config(wifi_config_t *wifi_config, const char *ssid = NULL, const char *password = NULL, uint8_t channel = 6, wifi_auth_mode_t authmode = WIFI_AUTH_WPA2_PSK, uint8_t ssid_hidden = 0, uint8_t max_connections = 4, bool ftm_responder = false, uint16_t beacon_interval = 100)
{
    wifi_config->ap.channel = channel;
    wifi_config->ap.max_connection = max_connections;
    wifi_config->ap.beacon_interval = beacon_interval;
    wifi_config->ap.ssid_hidden = ssid_hidden;
    wifi_config->ap.authmode = WIFI_AUTH_OPEN;
    wifi_config->ap.ssid_len = 0;
    wifi_config->ap.ssid[0] = 0;
    wifi_config->ap.password[0] = 0;
    wifi_config->ap.ftm_responder = ftm_responder;
    if (ssid != NULL && ssid[0] != 0)
    {
        _wifi_strncpy((char *)wifi_config->ap.ssid, ssid, 32);
        wifi_config->ap.ssid_len = strlen(ssid);
        if (password != NULL && password[0] != 0)
        {
            wifi_config->ap.authmode = authmode;
            wifi_config->ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
            _wifi_strncpy((char *)wifi_config->ap.password, password, 64);
        }
    }
}

/**
 * Set up an access point
 */
bool WiFiAPClass::softAP(const char *ssid, const char *passphrase, int channel, int ssid_hidden, int max_connection, bool ftm_responder)
{
    if (UNLIKELY(!ssid || *ssid == 0))
    {
        log_e("SSID missing!");
        return false;
    }

    if (passphrase && (strlen(passphrase) > 0 && strlen(passphrase) < 8))
    {
        log_e("passphrase too short!");
        return false;
    }

    if (UNLIKELY(!WiFi.enableAP(true)))
    {
        log_e("enable AP first!");
        return false;
    }

    wifi_config_t conf;
    wifi_config_t conf_current;
    wifi_softap_config(&conf, ssid, passphrase, channel, WIFI_AUTH_WPA2_PSK, ssid_hidden, max_connection, ftm_responder);
    esp_err_t err = esp_wifi_get_config((wifi_interface_t)WIFI_IF_AP, &conf_current);
    if (UNLIKELY(err))
    {
        log_e("get AP config failed");
        return false;
    }
    if (!softap_config_equal(conf, conf_current))
    {
        err = esp_wifi_set_config((wifi_interface_t)WIFI_IF_AP, &conf);
        if (UNLIKELY(err))
        {
            log_e("set AP config failed");
            return false;
        }
    }

    return true;
}

/**
 * Return the current SSID associated with the network
 */
String WiFiAPClass::softAPSSID() const
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return String();
    }
    wifi_config_t info;
    if (LIKELY(!esp_wifi_get_config(WIFI_IF_AP, &info)))
    {
        return String(reinterpret_cast<char *>(info.ap.ssid));
    }
    return String();
}

/**
 * Configure access point
 */
bool WiFiAPClass::softAPConfig(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dhcp_lease_start)
{
    if (UNLIKELY(!WiFi.enableAP(true)))
    {
        return false;
    }

    esp_err_t err = set_esp_interface_ip(ESP_IF_WIFI_AP, local_ip, gateway, subnet, dhcp_lease_start);
    return err == ESP_OK;
}

/**
 * Disconnect from the network (close AP)
 */
bool WiFiAPClass::softAPdisconnect(bool wifioff)
{
    wifi_config_t conf;
    wifi_softap_config(&conf);

    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return false;
    }

    bool ret = esp_wifi_set_config((wifi_interface_t)WIFI_IF_AP, &conf) == ESP_OK;

    if (ret && wifioff)
    {
        ret = WiFi.enableAP(false) == ESP_OK;
    }

    return ret;
}

/**
 * Get the count of the Station / client that are connected to the softAP interface
 */
uint8_t WiFiAPClass::softAPgetStationNum()
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return 0;
    }
    wifi_sta_list_t clients;
    if (LIKELY(esp_wifi_ap_get_sta_list(&clients) == ESP_OK))
    {
        return clients.num;
    }
    return 0;
}
