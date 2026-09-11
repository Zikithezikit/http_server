/**
 * @file app_main.c
 * @brief Application entry point.
 *
 * Initializes NVS, LittleFS and the OLED display, connects to WiFi, waits
 * for an IP address, then starts the HTTP server task.
 */

#include "include/common.h"
#include "include/ssd1306.h"
#include "include/wifi.h"
#include "include/tcp_server.h"

static esp_err_t init_littlefs(void);
static esp_err_t init_oled(void);

static const char *TAG = "html_server";

/* OLED boot screen layout: divider lines around the assigned IP address. */
#define OLED_DIVIDER "----------------------"


void app_main(void)
{
    // Suppress noisy logs from WiFi and netif subcomponents
    esp_log_level_set("wifi", ESP_LOG_NONE);
    esp_log_level_set("wifi_init", ESP_LOG_NONE);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_NONE);
    esp_log_level_set("esp_netif_lwip", ESP_LOG_NONE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "nvs flash done");

    ESP_ERROR_CHECK(init_littlefs());
    ESP_LOGI(TAG, "LittleFS init done");

    ESP_ERROR_CHECK(init_oled());
    ESP_LOGI(TAG, "OLED initialised");

    char rows[OLED_NUM_ROWS][OLED_ROW_LEN] = {0};
    snprintf(rows[0], sizeof(rows[0]), "%s", OLED_DIVIDER);
    snprintf(rows[1], sizeof(rows[1]), "Getting an IP");
    snprintf(rows[OLED_NUM_ROWS - 1], sizeof(rows[OLED_NUM_ROWS - 1]), "%s", OLED_DIVIDER);
    oled_render(rows);

    esp_netif_t *wifi_handle = init_wifi();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .failure_retry_cnt = 10,
        },
    };

    ESP_ERROR_CHECK(set_config(wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect()); // enable connection retry

    esp_netif_ip_info_t ip_info = {0};
    while (ip_info.ip.addr == IP_ADDR_NONE)
    {
        vTaskDelay(pdMS_TO_TICKS(IP_POLL_DELAY_MS));
        esp_netif_get_ip_info(wifi_handle, &ip_info);
    }

    ESP_LOGI(TAG, "-------------------------------------");
    ESP_LOGI(TAG, "IP =      " IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Netmask = " IPSTR, IP2STR(&ip_info.netmask));
    ESP_LOGI(TAG, "GateWay = " IPSTR, IP2STR(&ip_info.gw));
    ESP_LOGI(TAG, "-------------------------------------");

    snprintf(rows[1], sizeof(rows[1]), "IP = " IPSTR, IP2STR(&ip_info.ip));
    oled_render(rows);



    TaskHandle_t xHtmlStaskHandle = NULL;
    xTaskCreatePinnedToCore(
        http_server_task,
        "http_server_ts",
        TASK_STACK_SIZE,
        NULL,
        TASK_PRIORITY,
        &xHtmlStaskHandle,
        TASK_CORE_ID
    );
}

static esp_err_t init_littlefs(void)
{
    ESP_LOGI(TAG, "Initializing LittleFS");

    esp_vfs_littlefs_conf_t config = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&config);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(config.partition_label, &total, &used);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Partition size: Total: %d, Used: %d", total, used);
    }

    return ret;
}

static esp_err_t init_oled(void)
{
    ssd1306_config_t dev_cfg = {
        .sda_pin = OLED_SDA_GPIO,
        .scl_pin = OLED_SCL_GPIO,
        .vdd_pin = OLED_VDD_GPIO,
        .gnd_pin = OLED_GND_GPIO,
        .i2c_address = OLED_I2C_ADDR,
        .i2c_port = I2C_NUM_0,
    };

    s_display_lock = xSemaphoreCreateMutex();
    if (s_display_lock == NULL)
    {
        ESP_LOGE(TAG, "Failed to create display mutex; OLED disabled");
        return ESP_FAIL;
    }
    esp_err_t err = ssd1306_init(&dev_cfg, &s_oled);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "OLED init failed; display disabled");
        return err;
    }
    
    return ESP_OK;
}
