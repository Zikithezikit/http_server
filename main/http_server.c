/**
 * @file http_server.c
 * @brief HTTP request parsing and per-client connection handling implementation.
 */

#include "include/http_server.h"

static bool copy_token(char *o_dest, size_t dest_size, const char *start, const char *end);
static const char *parse_request_line(HttpRequest *o_request, const char *start, const char *buf_end);
static bool parse_single_header(HttpHeader *o_hdr, const char *line_start, const char *line_end);
static void parse_headers(HttpRequest *o_request, const char *curr, const char *buf_end);

static const char *TAG = "http_server";

void html_client_task(void *pvParameters)
{
    int client_socket = (int)pvParameters;

    char rx_buffer[RX_BUFFER_SIZE] = {0};
    int len = recv(client_socket, rx_buffer, sizeof(rx_buffer) - 1, 0);
    if (len > 0)
    {
        if (handle_incoming_message(client_socket, rx_buffer, sizeof(rx_buffer)) != ESP_OK)
        {
            ESP_LOGE(TAG, "Can't handle in-coming message from socket %d", client_socket);
        }
    }

    shutdown(client_socket, 0);
    close(client_socket);
    ESP_LOGI(TAG, "Client disconnected.");

    vTaskDelete(NULL);
}

esp_err_t handle_incoming_message(int client_socket, char *rx_buffer, size_t buffer_length)
{
    ESP_LOGI(TAG, "From socket %d", client_socket);

    if (strncmp(rx_buffer, EXIT_COMMAND, sizeof(EXIT_COMMAND) - 1) == 0)
    {
        return ESP_FAIL;
    }

    HttpRequest http_request = {0};

    if (parse_http_request(rx_buffer, buffer_length, &http_request) == 0)
    {
        ESP_LOGI(TAG, "Method:     %s\n", http_request.method);
        ESP_LOGI(TAG, "Path:       %s\n", http_request.path);
        ESP_LOGI(TAG, "Version:    %s\n", http_request.version);
        ESP_LOGI(TAG, "Host:       %s\n", get_header_value(&http_request, "host"));
        ESP_LOGI(TAG, "User-Agent: %s\n", get_header_value(&http_request, "user-agent"));
    }

    char *resp =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 51\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body><h1>404 File Not Found</h1></body></html>";   


    ssize_t err = -1;
    FILE *fd = fopen(http_request.path, "r");
    if (fd == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file: %s", http_request.path);
        err = send(client_socket, resp, strlen(resp), MSG_NOSIGNAL);
        return ESP_FAIL;
    }

    char file_buffer[RX_BUFFER_SIZE] = {0};
    if (fread(file_buffer, 1, RX_BUFFER_SIZE, fd) == 0)
    {
        ESP_LOGE(TAG, "File sending failed, %s", http_request.path);
        err = send(client_socket, resp, strlen(resp), MSG_NOSIGNAL);
    }
    else 
    {
        err = send(client_socket, file_buffer, strlen(file_buffer), MSG_NOSIGNAL);
    }
    
    if (fclose(fd) == -1)
    {
        ESP_LOGE(TAG, "Can't close the file, aborting.");
        abort();
    }

    if (err < 0)
    {
        if (errno == EPIPE || errno == ECONNRESET)
        {
            ESP_LOGE(TAG, "Send failed: %s", strerror(errno));
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Sent the file %s", http_request.path);
    return ESP_OK;
}

// Helper: Safely copies a delimited slice into an output target buffer
static bool copy_token(char *o_dest, size_t dest_size, const char *start, const char *end)
{
    if (!start || !end || end < start)
    {
        return false;
    }

    size_t len = end - start;
    if (len >= dest_size)
    {
        len = dest_size - 1;
    }
    memcpy(o_dest, start, len);
    o_dest[len] = '\0';
    return true;
}

// Helper: Boundary-safe memchr equivalent to find a character within remaining buffer space
static const char *find_char(const char *start, const char *buf_end, char target)
{
    if (!start || start >= buf_end)
    {
        return NULL;
    }
    return (const char *)memchr(start, target, buf_end - start);
}

// Helper: Boundary-safe memmem replacement to find a string sequence within remaining buffer space
static const char *find_str(const char *start, const char *buf_end, const char *target, size_t target_len)
{
    if (!start || start >= buf_end || (size_t)(buf_end - start) < target_len)
    {
        return NULL;
    }

    const char *search_end = buf_end - target_len + 1;
    for (const char *p = start; p < search_end; p++)
    {
        if (memcmp(p, target, target_len) == 0)
        {
            return p;
        }
    }
    return NULL;
}

// Parses the request line (method, path, HTTP version) and returns the header start
static const char *parse_request_line(HttpRequest *o_request, const char *start, const char *buf_end)
{
    // Method
    const char *method_end = find_char(start, buf_end, ' ');
    if (!method_end)
    {
        return NULL;
    }
    copy_token(o_request->method, sizeof(o_request->method), start, method_end);

    // Path
    const char *path_start = method_end + 1;
    const char *path_end = find_char(path_start, buf_end, ' ');
    if (!path_end)
    {
        return NULL;
    }
    copy_token(o_request->path, sizeof(o_request->path), path_start, path_end);

    // Version
    const char *version_start = path_end + 1;
    const char *version_end = find_str(version_start, buf_end, "\r\n", CRLF_LEN);
    if (!version_end)
    {
        return NULL;
    }
    copy_token(o_request->version, sizeof(o_request->version), version_start, version_end);

    return version_end + CRLF_LEN; // Move past \r\n
}

// Parses a single "Key: value" header line into o_hdr
static bool parse_single_header(HttpHeader *o_hdr, const char *line_start, const char *line_end)
{
    const char *colon = find_char(line_start, line_end, HEADER_SEPARATOR);
    if (!colon)
    {
        return false;
    }
    copy_token(o_hdr->key, sizeof(o_hdr->key), line_start, colon);

    const char *val_start = colon + 1;
    while (val_start < line_end && (*val_start == ' ' || *val_start == '\t'))
    {
        val_start++;
    }

    copy_token(o_hdr->value, sizeof(o_hdr->value), val_start, line_end);
    return true;
}

// Parses all header lines into o_request until the blank line ending the header block
static void parse_headers(HttpRequest *o_request, const char *curr, const char *buf_end)
{
    while (curr < buf_end && o_request->header_count < MAX_HEADERS)
    {
        if ((buf_end - curr) >= CRLF_LEN && curr[0] == '\r' && curr[1] == '\n')
        {
            break;
        }

        const char *line_end = find_str(curr, buf_end, "\r\n", CRLF_LEN);
        if (!line_end)
        {
            break;
        }

        HttpHeader *hdr = &o_request->headers[o_request->header_count];
        if (parse_single_header(hdr, curr, line_end))
        {
            o_request->header_count++;
        }

        curr = line_end + CRLF_LEN; // Move to next line
    }
}

int parse_http_request(char *rx_buffer, size_t buffer_length, HttpRequest *o_request)
{
    if (!rx_buffer || buffer_length == 0 || !o_request)
    {
        return -1;
    }

    const char *buf_end = rx_buffer + buffer_length;

    const char *headers_start = parse_request_line(o_request, rx_buffer, buf_end);
    if (!headers_start)
    {
        return -1;
    }

    parse_headers(o_request, headers_start, buf_end);
    return 0;
}

const char* get_header_value(const HttpRequest *req, const char *key)
{
    if (!req || !key) 
    {
        return NULL;
    }

    for (size_t i = 0; i < req->header_count; ++i) {
        if (strcasecmp(req->headers[i].key, key) == 0)
        {
            return req->headers[i].value;
        }
    }

    return NULL;
}

