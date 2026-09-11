#pragma once

/**
 * @file wifi.h
 * @brief WiFi initialization and configuration.
 *
 * Credentials are injected at build time from the project .env file
 * (see .env.example). The defaults below only apply when no .env supplies
 * WIFI_SSID/WIFI_PASSWORD.
 */

#include "common.h"

/** @brief WiFi network SSID to connect to (overridable via .env) */
#ifndef WIFI_SSID
#define WIFI_SSID "default-wifi-name"
#endif

/** @brief Passphrase for the configured WiFi network (overridable via .env) */
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "1"
#endif

/**
 * @brief Initialize WiFi subsystem and create default STA interface.
 *
 * @return Pointer to the created WiFi network interface handle.
 */
esp_netif_t *init_wifi(void);

/**
 * @brief Configure WiFi station mode with the provided credentials.
 *
 * @param[in] wifi_config WiFi configuration (SSID, password, etc.)
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t set_config(wifi_config_t wifi_config);
