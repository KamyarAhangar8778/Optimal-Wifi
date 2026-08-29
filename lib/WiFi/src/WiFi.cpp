/*
 ESP8266WiFi.cpp - WiFi library for esp8266

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
}

// -----------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------- Debug ------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

/**
 * Output WiFi settings to an object derived from Print interface (like Serial).
 * @param p Print interface
 */
void WiFiClass::printDiag(Print &p)
{
    const char *modes[] = {"NULL", "STA", "AP", "STA+AP"};

    // esp_wifi_get_mode() returns ESP_ERR_WIFI_NOT_INIT when WiFi is stopped
    // (e.g. after WiFi.mode(WIFI_OFF)); it then leaves `mode` untouched, so
    // reading it would be uninitialized garbage. Guard every call.
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t modeErr = esp_wifi_get_mode(&mode);

    uint8_t primaryChan = 0;
    wifi_second_chan_t secondChan = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&primaryChan, &secondChan);

    p.print("Mode: ");
    if (modeErr == ESP_OK && mode < (sizeof(modes) / sizeof(modes[0])))
    {
        p.println(modes[mode]);
    }
    else
    {
        p.println("UNKNOWN");
    }

    p.print("Channel: ");
    p.println(primaryChan);
    /*
        p.print("AP id: ");
        p.println(wifi_station_get_current_ap_id());

        p.print("Status: ");
        p.println(wifi_station_get_connect_status());
    */

    // Only read STA config while WiFi is initialized and in a STA-bearing mode.
    if (modeErr == ESP_OK && (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA))
    {
        wifi_config_t conf;
        if (esp_wifi_get_config((wifi_interface_t)WIFI_IF_STA, &conf) == ESP_OK)
        {
            // esp-idf does not guarantee NUL-termination of ssid/password;
            // bound strlen to the field size to avoid reading past the buffer.
            const char *ssid = reinterpret_cast<const char *>(conf.sta.ssid);
            size_t ssidLen = strnlen(ssid, sizeof(conf.sta.ssid));
            p.print("SSID (");
            p.print(ssidLen);
            p.print("): ");
            p.println(ssid);

            const char *passphrase = reinterpret_cast<const char *>(conf.sta.password);
            size_t passLen = strnlen(passphrase, sizeof(conf.sta.password));
            p.print("Passphrase (");
            p.print(passLen);
            p.print("): ");
            p.println(passphrase);

            p.print("BSSID set: ");
            p.println(conf.sta.bssid_set);
        }
    }
    else
    {
        p.println("SSID (0): <wifi off>");
        p.println("Passphrase (0): <wifi off>");
        p.println("BSSID set: 0");
    }
}

void WiFiClass::enableProv(bool status)
{
    prov_enable = status;
}

bool WiFiClass::isProvEnabled()
{
    return prov_enable;
}

WiFiClass WiFi;
