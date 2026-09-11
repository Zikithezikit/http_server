/**
 * @file wifi.c
 * @brief WiFi initialization and configuration implementation.
 */

#include "include/wifi.h"

esp_netif_t *init_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    return esp_netif_create_default_wifi_sta();
}

esp_err_t set_config(wifi_config_t wifi_config)
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    return ESP_OK;
}
