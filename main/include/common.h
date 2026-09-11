#pragma once

/**
 * @file common.h
 * @brief Common defines and shared types for the HTML server.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_littlefs.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "esp_log.h"

/* Forward declaration so common.h needs no include of ssd1306.h (the struct
 * is defined in ssd1306.c; the full typedef lives in ssd1306.h). */
typedef struct ssd1306_dev_t *ssd1306_handle_t;

/** @brief OLED I2C SDA GPIO (see ssd1306_config_t) */
#define OLED_SDA_GPIO GPIO_NUM_38
/** @brief OLED I2C SCL GPIO (see ssd1306_config_t) */
#define OLED_SCL_GPIO GPIO_NUM_37
/** @brief OLED VDD power GPIO, or GPIO_NUM_NC if hardware-powered */
#define OLED_VDD_GPIO GPIO_NUM_36
/** @brief OLED GND power GPIO, or GPIO_NUM_NC if hardware-powered */
#define OLED_GND_GPIO GPIO_NUM_35
/** @brief OLED I2C slave address (7-bit) */
#define OLED_I2C_ADDR 0x3C

/** @brief Global OLED handle; NULL if the display failed to initialise.
 *         Defined in ssd1306.c and shared by every translation unit. */
extern ssd1306_handle_t s_oled;
/** @brief Serialises access to the OLED framebuffer between application tasks.
 *         Created in app_main(), used by ssd1306.c. */
extern SemaphoreHandle_t s_display_lock;

/** @brief Default HTTP server port */
#define SERVER_PORT 80

/** @brief Delay in milliseconds between IP address polling attempts */
#define IP_POLL_DELAY_MS 250

/** @brief Socket listen backlog (max pending connections) */
#define SOCKET_BACKLOG 1

/** @brief IP address value indicating no address assigned */
#define IP_ADDR_NONE 0

/** @brief Stack size in bytes for server and client tasks */
#define TASK_STACK_SIZE 16384

/** @brief Priority for server and client tasks (higher = more urgent) */
#define TASK_PRIORITY 5

/** @brief CPU core to pin server tasks to (0 or 1) */
#define TASK_CORE_ID 1

/** @brief Size of the receive buffer for client requests in bytes */
#define RX_BUFFER_SIZE 4096

/** @brief Command string that signals client disconnection */
#define EXIT_COMMAND "exit"

/** @brief Length of the CRLF line terminator in HTTP messages */
#define CRLF_LEN 2
