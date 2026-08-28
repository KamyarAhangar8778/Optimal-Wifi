/*
 WiFiSTA.cpp - WiFi Station Core Lifecycle and Connection Management

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

 Reworked on 28 Dec 2015 by Markus Sattler
 */

#include "WiFiSTAInternal.h"

bool WiFiSTAClass::_autoReconnect = true;
bool WiFiSTAClass::_useStaticIp = false;
wifi_auth_mode_t WiFiSTAClass::_minSecurity = WIFI_AUTH_WPA2_PSK;
wifi_scan_method_t WiFiSTAClass::_scanMethod = WIFI_FAST_SCAN;
wifi_sort_method_t WiFiSTAClass::_sortMethod = WIFI_CONNECT_AP_BY_SIGNAL;

static wl_status_t _sta_status = WL_NO_SHIELD;
static EventGroupHandle_t _sta_status_group = NULL;

void WiFiSTAClass::_setStatus(wl_status_t status)
{
    if (UNLIKELY(!_sta_status_group))
    {
        _sta_status_group = xEventGroupCreate();
        if (UNLIKELY(!_sta_status_group))
        {
            log_e("STA Status Group Create Failed!");
            _sta_status = status;
            return;
        }
    }
    xEventGroupClearBits(_sta_status_group, 0x00FFFFFF);
    xEventGroupSetBits(_sta_status_group, status);
}

/**
 * Return Connection status.
 */
wl_status_t WiFiSTAClass::status()
{
    if (UNLIKELY(!_sta_status_group))
    {
        return _sta_status;
    }
    return (wl_status_t)xEventGroupClearBits(_sta_status_group, 0);
}

/**
 * Start Wifi connection
 */
wl_status_t WiFiSTAClass::begin(const char *ssid, const char *passphrase, int32_t channel, const uint8_t *bssid, bool connect)
{
    if (UNLIKELY(!WiFi.enableSTA(true)))
    {
        log_e("STA enable failed!");
        return WL_CONNECT_FAILED;
    }

    if (UNLIKELY(!ssid || *ssid == 0x00 || strnlen(ssid, 33) > 32))
    {
        log_e("SSID too long or missing!");
        return WL_CONNECT_FAILED;
    }

    if (UNLIKELY(passphrase && strnlen(passphrase, 65) > 64))
    {
        log_e("passphrase too long!");
        return WL_CONNECT_FAILED;
    }

    wifi_config_t conf;
    memset(&conf, 0, sizeof(wifi_config_t));

    wifi_sta_config(&conf, ssid, passphrase, bssid, channel, _minSecurity, _scanMethod, _sortMethod);

    wifi_config_t current_conf;
    if (UNLIKELY(esp_wifi_get_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf) != ESP_OK))
    {
        log_e("get current config failed!");
        return WL_CONNECT_FAILED;
    }
    if (!sta_config_equal(current_conf, conf))
    {
        if (esp_wifi_disconnect())
        {
            log_e("disconnect failed!");
            return WL_CONNECT_FAILED;
        }

        if (esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &conf) != ESP_OK)
        {
            log_e("set config failed!");
            return WL_CONNECT_FAILED;
        }
    }
    else if (status() == WL_CONNECTED)
    {
        return WL_CONNECTED;
    }
    else
    {
        if (esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &conf) != ESP_OK)
        {
            log_e("set config failed!");
            return WL_CONNECT_FAILED;
        }
    }

    if (!_useStaticIp)
    {
        if (set_esp_interface_ip(ESP_IF_WIFI_STA) != ESP_OK)
        {
            return WL_CONNECT_FAILED;
        }
    }

    if (connect)
    {
        if (esp_wifi_connect() != ESP_OK)
        {
            log_e("connect failed!");
            return WL_CONNECT_FAILED;
        }
    }

    return status();
}

wl_status_t WiFiSTAClass::begin(char *ssid, char *passphrase, int32_t channel, const uint8_t *bssid, bool connect)
{
    return begin((const char *)ssid, (const char *)passphrase, channel, bssid, connect);
}

/**
 * Use to connect to SDK config.
 */
wl_status_t WiFiSTAClass::begin()
{
    if (UNLIKELY(!WiFi.enableSTA(true)))
    {
        log_e("STA enable failed!");
        return WL_CONNECT_FAILED;
    }

    wifi_config_t current_conf;
    if (UNLIKELY(esp_wifi_get_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf) != ESP_OK || esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &current_conf) != ESP_OK))
    {
        log_e("config failed");
        return WL_CONNECT_FAILED;
    }

    if (!_useStaticIp && set_esp_interface_ip(ESP_IF_WIFI_STA) != ESP_OK)
    {
        log_e("set ip failed!");
        return WL_CONNECT_FAILED;
    }

    if (status() != WL_CONNECTED)
    {
        esp_err_t err = esp_wifi_connect();
        if (UNLIKELY(err))
        {
            log_e("connect failed! 0x%x", err);
            return WL_CONNECT_FAILED;
        }
    }

    return status();
}

/**
 * Force a disconnect and then start reconnecting to AP
 */
bool WiFiSTAClass::reconnect()
{
    if (WiFi.getMode() & WIFI_MODE_STA)
    {
        if (esp_wifi_disconnect() == ESP_OK)
        {
            return esp_wifi_connect() == ESP_OK;
        }
    }
    return false;
}

/**
 * Disconnect from the network.
 */
bool WiFiSTAClass::disconnect(bool wifioff, bool eraseap)
{
    wifi_config_t conf;
    wifi_sta_config(&conf);

    if (WiFi.getMode() & WIFI_MODE_STA)
    {
        if (eraseap)
        {
            if (esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &conf))
            {
                log_e("clear config failed!");
            }
        }
        if (esp_wifi_disconnect())
        {
            log_e("disconnect failed!");
            return false;
        }
        if (wifioff)
        {
            return WiFi.enableSTA(false);
        }
        return true;
    }

    return false;
}

/**
 * Reset WiFi settings in NVS to default values.
 */
bool WiFiSTAClass::eraseAP(void)
{
    if (WiFi.getMode() == WIFI_MODE_NULL)
    {
        if (!WiFi.enableSTA(true))
        {
            return false;
        }
    }
    return esp_wifi_restore() == ESP_OK;
}

/**
 * Is STA interface connected?
 */
bool WiFiSTAClass::isConnected()
{
    return (status() == WL_CONNECTED);
}

/**
 * Wait for WiFi connection to reach a result
 */
uint8_t WiFiSTAClass::waitForConnectResult(unsigned long timeoutLength)
{
    if ((WiFiGenericClass::getMode() & WIFI_MODE_STA) == 0)
    {
        return WL_DISCONNECTED;
    }
    unsigned long start = millis();
    while ((!status() || status() >= WL_DISCONNECTED) && (millis() - start) < timeoutLength)
    {
        delay(10);
    }
    return status();
}
