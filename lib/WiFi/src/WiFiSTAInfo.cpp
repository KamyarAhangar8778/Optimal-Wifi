/*
 WiFiSTAInfo.cpp - WiFi Station Network Query, BSSID/RSSI, and SmartConfig

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
#include <esp_smartconfig.h>

bool WiFiSTAClass::_smartConfigStarted = false;
bool WiFiSTAClass::_smartConfigDone = false;

/**
 * Return the current SSID associated with the network
 */
String WiFiSTAClass::SSID() const
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return String();
    }
    wifi_ap_record_t info;
    if (LIKELY(!esp_wifi_sta_get_ap_info(&info)))
    {
        return String(reinterpret_cast<char *>(info.ssid));
    }
    return String();
}

/**
 * Return the current pre shared key associated with the network
 */
String WiFiSTAClass::psk() const
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return String();
    }
    wifi_config_t conf;
    esp_wifi_get_config((wifi_interface_t)ESP_IF_WIFI_STA, &conf);
    return String(reinterpret_cast<char *>(conf.sta.password));
}

/**
 * Return the current BSSID / MAC associated with the network if configured
 */
uint8_t *WiFiSTAClass::BSSID(void)
{
    static uint8_t bssid[6];
    wifi_ap_record_t info;
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return NULL;
    }
    if (LIKELY(!esp_wifi_sta_get_ap_info(&info)))
    {
        memcpy(bssid, info.bssid, 6);
        return reinterpret_cast<uint8_t *>(bssid);
    }
    return NULL;
}

/**
 * Return the current BSSID / MAC formatted as string.
 * Uses zero-allocation table lookup avoiding sprintf parsing overhead.
 */
String WiFiSTAClass::BSSIDstr(void)
{
    uint8_t *bssid = BSSID();
    if (UNLIKELY(!bssid))
    {
        return String();
    }
    static const char hex_digits[] = "0123456789ABCDEF";
    char mac[18];
    mac[0]  = hex_digits[(bssid[0] >> 4) & 0x0F];
    mac[1]  = hex_digits[bssid[0] & 0x0F];
    mac[2]  = ':';
    mac[3]  = hex_digits[(bssid[1] >> 4) & 0x0F];
    mac[4]  = hex_digits[bssid[1] & 0x0F];
    mac[5]  = ':';
    mac[6]  = hex_digits[(bssid[2] >> 4) & 0x0F];
    mac[7]  = hex_digits[bssid[2] & 0x0F];
    mac[8]  = ':';
    mac[9]  = hex_digits[(bssid[3] >> 4) & 0x0F];
    mac[10] = hex_digits[bssid[3] & 0x0F];
    mac[11] = ':';
    mac[12] = hex_digits[(bssid[4] >> 4) & 0x0F];
    mac[13] = hex_digits[bssid[4] & 0x0F];
    mac[14] = ':';
    mac[15] = hex_digits[(bssid[5] >> 4) & 0x0F];
    mac[16] = hex_digits[bssid[5] & 0x0F];
    mac[17] = '\0';

    return String(mac);
}

/**
 * Return the current network RSSI.
 */
int8_t WiFiSTAClass::RSSI(void)
{
    if (UNLIKELY(WiFiGenericClass::getMode() == WIFI_MODE_NULL))
    {
        return 0;
    }
    wifi_ap_record_t info;
    if (LIKELY(!esp_wifi_sta_get_ap_info(&info)))
    {
        return info.rssi;
    }
    return 0;
}

/**
 * Start SmartConfig
 */
bool WiFiSTAClass::beginSmartConfig(smartconfig_type_t type, char *crypt_key)
{
    esp_err_t err;
    if (_smartConfigStarted)
    {
        return false;
    }

    if (!WiFi.mode(WIFI_STA))
    {
        return false;
    }
    esp_wifi_disconnect();

    smartconfig_start_config_t conf = SMARTCONFIG_START_CONFIG_DEFAULT();

    if (type == SC_TYPE_ESPTOUCH_V2)
    {
        conf.esp_touch_v2_enable_crypt = true;
        conf.esp_touch_v2_key = crypt_key;
    }

    err = esp_smartconfig_set_type(type);
    if (UNLIKELY(err != ESP_OK))
    {
        log_e("SmartConfig Set Type Failed!");
        return false;
    }
    err = esp_smartconfig_start(&conf);
    if (UNLIKELY(err != ESP_OK))
    {
        log_e("SmartConfig Start Failed!");
        return false;
    }
    _smartConfigStarted = true;
    _smartConfigDone = false;
    return true;
}

bool WiFiSTAClass::stopSmartConfig()
{
    if (!_smartConfigStarted)
    {
        return true;
    }

    if (esp_smartconfig_stop() == ESP_OK)
    {
        _smartConfigStarted = false;
        return true;
    }

    return false;
}

bool WiFiSTAClass::smartConfigDone()
{
    if (!_smartConfigStarted)
    {
        return false;
    }

    return _smartConfigDone;
}
