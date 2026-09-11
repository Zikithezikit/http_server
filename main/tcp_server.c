/**
 * @file tcp_server.c
 * @brief TCP server and per-connection client task implementation.
 */

#include "include/tcp_server.h"

static const char *TAG = "tcp_server";

int init_socket(void)
{
    int listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_socket == -1)
    {
        ESP_ERROR_CHECK(ESP_FAIL);
    }
    return listen_socket;
}

esp_err_t start_socket(esp_netif_t *wifi_handle, int listen_socket)
{
    esp_netif_ip_info_t ip_info = {0};
    while (ip_info.ip.addr == IP_ADDR_NONE) {
        vTaskDelay(pdMS_TO_TICKS(IP_POLL_DELAY_MS));
        esp_netif_get_ip_info(wifi_handle, &ip_info);
    }

    struct sockaddr_in socket_address = {
        .sin_addr.s_addr = ip_info.ip.addr,
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
    };

    int err = bind(listen_socket, (struct sockaddr *)&socket_address, sizeof(socket_address));
    if (err != 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(listen_socket);
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(listen(listen_socket, SOCKET_BACKLOG));
    ESP_LOGI(TAG, "Socket bound and listening on port %d", SERVER_PORT);

    return ESP_OK;
}

void http_server_task(void *pvParameters)
{
    esp_netif_t *wifi_handle = esp_netif_get_default_netif();
    int listen_socket = init_socket();
    ESP_ERROR_CHECK(start_socket(wifi_handle, listen_socket));

    while (1)
    {
        struct sockaddr_in client_address;
        socklen_t addr_len = sizeof(client_address);
        int client_socket = accept(listen_socket, (struct sockaddr *)&client_address, &addr_len);


        if (client_socket == -1)
        {
            ESP_LOGE(TAG, "Can't accept client's request to connect to the socket. Aborting now!");
            abort();
        }

        esp_ip4_addr_t ip = {.addr = client_address.sin_addr.s_addr};
        ESP_LOGI(TAG, "Accepted TCP connection to " IPSTR " on socket %d",  IP2STR(&ip), client_socket);

        xTaskCreate(html_client_task, "html_client_task", TASK_STACK_SIZE * 2, (void *)client_socket, TASK_PRIORITY, NULL);
    }
}
