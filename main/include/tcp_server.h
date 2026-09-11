#pragma once

/**
 * @file tcp_server.h
 * @brief TCP server and client handling.
 */

#include "common.h"
#include "http_server.h"

/**
 * @brief Create a TCP listening socket.
 *
 * @return File descriptor of the listening socket.
 * @note Aborts the program on socket creation failure (via ESP_ERROR_CHECK).
 */
int init_socket(void);

/**
 * @brief Bind and start listening on the given socket.
 *
 * @param[in] wifi_handle  WiFi interface handle used to obtain the local IP.
 * @param[in] listen_socket  Socket file descriptor to bind.
 * @return ESP_OK on success, ESP_FAIL on bind failure.
 */
esp_err_t start_socket(esp_netif_t *wifi_handle, int listen_socket);

/**
 * @brief HTTP server task that accepts incoming TCP connections.
 *
 * @param[in] pvParameters Unused (NULL).
 */
void http_server_task(void *pvParameters);


