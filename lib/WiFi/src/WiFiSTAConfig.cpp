/*
 WiFiSTAConfig.cpp - WiFi Station Static IP and WPA2 Enterprise Configuration

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
extern "C"
{
#include "esp_wpa2.h"
}

/**
 * Start Wifi connection with a WPA2 Enterprise AP
 */
wl_status_t WiFiSTAClass::begin(const char *wpa2_ssid, wpa2_auth_method_t method, const char *wpa2_identity, const char *wpa2_username, const char *wpa2_password, const char *ca_pem, const char *client_crt, const char *client_key, int32_t channel, const uint8_t *bssid, bool connect)
{
    if (UNLIKELY(!WiFi.enableSTA(true)))
    {
        log_e("STA enable failed!");
        return WL_CONNECT_FAILED;
    }

    if (UNLIKELY(!wpa2_ssid || *wpa2_ssid == 0x00 || strnlen(wpa2_ssid, 33) > 32))
    {
        log_e("SSID too long or missing!");
        return WL_CONNECT_FAILED;
    }

    if (UNLIKELY(wpa2_identity && strnlen(wpa2_identity, 65) > 64))
    {
        log_e("identity too long!");
        return WL_CONNECT_FAILED;
    }

    if (UNLIKELY(wpa2_username && strnlen(wpa2_username, 65) > 64))
    {
        log_e("username too long!");
        return WL_CONNECT_FAILED;
    }

    if (UNLIKELY(wpa2_password && strnlen(wpa2_password, 65) > 64))
    {
        log_e("password too long!");
    }

    if (ca_pem)
    {
        esp_wifi_sta_wpa2_ent_set_ca_cert((uint8_t *)ca_pem, strlen(ca_pem));
    }

    if (client_crt)
    {
        esp_wifi_sta_wpa2_ent_set_cert_key((uint8_t *)client_crt, strlen(client_crt), (uint8_t *)client_key, strlen(client_key), NULL, 0);
    }

    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)wpa2_identity, strlen(wpa2_identity));
    if (method == WPA2_AUTH_PEAP || method == WPA2_AUTH_TTLS)
    {
        esp_wifi_sta_wpa2_ent_set_username((uint8_t *)wpa2_username, strlen(wpa2_username));
        esp_wifi_sta_wpa2_ent_set_password((uint8_t *)wpa2_password, strlen(wpa2_password));
    }
    esp_wifi_sta_wpa2_ent_enable();
    WiFi.begin(wpa2_ssid);

    return status();
}

/**
 * Change IP configuration settings disabling the DHCP client
 */
bool WiFiSTAClass::config(IPAddress local_ip, IPAddress gateway, IPAddress subnet, IPAddress dns1, IPAddress dns2)
{
    esp_err_t err = ESP_OK;

    if (UNLIKELY(!WiFi.enableSTA(true)))
    {
        return false;
    }
    err = set_esp_interface_ip(ESP_IF_WIFI_STA, local_ip, gateway, subnet);
    if (LIKELY(err == ESP_OK))
    {
        err = set_esp_interface_dns(ESP_IF_WIFI_STA, dns1, dns2);
    }
    _useStaticIp = (err == ESP_OK);
    return err == ESP_OK;
}

void WiFiSTAClass::setMinSecurity(wifi_auth_mode_t minSecurity)
{
    _minSecurity = minSecurity;
}

void WiFiSTAClass::setScanMethod(wifi_scan_method_t scanMethod)
{
    _scanMethod = scanMethod;
}

void WiFiSTAClass::setSortMethod(wifi_sort_method_t sortMethod)
{
    _sortMethod = sortMethod;
}

bool WiFiSTAClass::setAutoConnect(bool autoConnect)
{
    return false; // deprecated
}

bool WiFiSTAClass::getAutoConnect()
{
    return false; // deprecated
}

bool WiFiSTAClass::setAutoReconnect(bool autoReconnect)
{
    _autoReconnect = autoReconnect;
    return true;
}

bool WiFiSTAClass::getAutoReconnect()
{
    return _autoReconnect;
}
