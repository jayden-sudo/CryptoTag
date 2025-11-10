#include "http_request.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

#define TAG "HTTP_REQUEST"

typedef struct
{
    char *buffer;
    int length;
} http_response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *response = (http_response_t *)evt->user_data;
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        // ESP_LOGI(TAG, "Received data, len=%d", evt->data_len);
        if (evt->data && response)
        {
            response->buffer = realloc(response->buffer, response->length + evt->data_len + 1);
            memcpy(response->buffer + response->length, evt->data, evt->data_len);
            response->length += evt->data_len;
            response->buffer[response->length] = 0;
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP request finished");
        break;
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP request error");
        break;
    default:
        break;
    }
    return ESP_OK;
}

char *http_get(char *url)
{
    http_response_t response = {0};
    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .user_data = &response,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        free(response.buffer);
        return NULL;
    }

    esp_http_client_cleanup(client);
    return response.buffer;
}