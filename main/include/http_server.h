#pragma once

/**
 * @file http_server.h
 * @brief HTTP request parsing and per-client connection handling.
 */

#include "common.h"

/** @brief Maximum length of the HTTP method field (e.g. GET) */
#define MAX_METHOD_LEN  16
/** @brief Maximum length of the request path field */
#define MAX_PATH_LEN    256
/** @brief Maximum length of the HTTP version field (e.g. HTTP/1.1) */
#define MAX_VERSION_LEN 16
/** @brief Maximum length of a header key */
#define MAX_KEY_LEN     64
/** @brief Maximum length of a header value */
#define MAX_VAL_LEN     256
/** @brief Maximum number of headers parsed per request */
#define MAX_HEADERS     20

/** @brief Character separating a header key from its value (RFC 7230) */
#define HEADER_SEPARATOR ':'

/**
 * @brief A single HTTP header field (key and value).
 */
typedef struct {
    char key[MAX_KEY_LEN];
    char value[MAX_VAL_LEN];
} HttpHeader;

/**
 * @brief Parsed HTTP request: start line and collected headers.
 */
typedef struct {
    char method[MAX_METHOD_LEN];
    char path[MAX_PATH_LEN];
    char version[MAX_VERSION_LEN];
    HttpHeader headers[MAX_HEADERS];
    size_t header_count;
} HttpRequest;


/**
 * @brief Per-client task that reads data from a connected socket.
 *
 * @param[in] pvParameters Client socket file descriptor cast to void*.
 */
void html_client_task(void *pvParameters);

/**
 * @brief Process data received from a connected client.
 *
 * @param[in] client_socket  Socket file descriptor of the connected client.
 * @param[in] rx_buffer      Buffer containing the received data.
 * @param[in] buffer_length  Length of the data in the buffer.
 * @return ESP_OK on success, ESP_FAIL on exit command or send error.
 */
esp_err_t handle_incoming_message(int client_socket, char *rx_buffer, size_t buffer_length);

/**
 * @brief Parse an HTTP request line and headers from a raw buffer.
 *
 * @param[in]     rx_buffer      Buffer containing the raw HTTP request.
 * @param[in]     buffer_length  Length of the data in the buffer.
 * @param[in,out] o_request      Structure filled with the parsed request.
 * @return 0 on success, -1 on malformed input.
 */
int parse_http_request(char *rx_buffer, size_t buffer_length, HttpRequest *o_request);

/**
 * @brief Retrieve the value of a named header (case-insensitive).
 *
 * @param[in] req Request previously filled by parse_http_request().
 * @param[in] key Header name to look up.
 * @return Pointer to the header value, or NULL if not found.
 */
const char* get_header_value(const HttpRequest *req, const char *key);
