#include "esp_chip_info.h"
#include "esp_system.h"
#include "driver/i2c.h"
#include "i2c_lcd.h"
#include "esp_log.h"
#include "wifi_connect.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "config.h"

static const char *TAG = "Crypto Tag";

typedef struct
{
    bool Ok;
    double open[20];
} Kline;

typedef struct
{
    bool Ok;
    double suggestBaseFee;
} GasFee;

static uint8_t klineBitMap[8][8];

static double lastUpdate_price = 0;
static double lastUpdate_basefee = 0;

static void i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        //.master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static void klineBitMapClear()
{
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            klineBitMap[i][j] = 0;
}

static void klineSetPixel(int x /*time*/, int y /*price*/)
{
    if (x >= 20)
        x = 19;
    if (y >= 16)
        y = 15;
    // if (y > 15 || y < 0)
    //     return;

    int _index = 4;
    if (y > 7)
        _index = 0;

    int index = (x / 5) + _index;

    int _x = x % 5;
    int _y = y % 8;

    (klineBitMap[index][7 - _y]) |= (1 << (4 - _x));
}

static void klineClearPixel(int x /*time*/, int y /*price*/)
{
    if (x >= 20)
        x = 19;
    if (y >= 16)
        y = 15;
    // if (y > 15 || y < 0)
    //     return;

    int _index = 4;
    if (y > 7)
        _index = 0;

    int index = (x / 5) + _index;

    int _x = x % 5;
    int _y = y % 8;

    int mask = 0b11111 ^ (1 << (4 - _x));
    (klineBitMap[index][7 - _y]) &= mask;
}

static Kline get_kline_binance()
{
    Kline kline = {.Ok = false};
    esp_http_client_config_t config = {
        .url = "https://api.binance.com/api/v3/klines?symbol=ETHUSDT&interval=5m&limit=25",
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    }
    else
    {
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0)
        {
            ESP_LOGE(TAG, "HTTP client fetch headers failed");
        }
        else
        {
            char *buffer = malloc(content_length + 1);
            buffer[0] = 0;
            buffer[content_length] = 0;
            int data_read = esp_http_client_read_response(client, buffer, content_length);
            if (data_read == 0)
            {
                free(buffer);
                goto end;
            }
            // if (data_read >= 0)
            // {
            //     // ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRId64,
            //     //          esp_http_client_get_status_code(client),
            //     //          esp_http_client_get_content_length(client));
            //     // ESP_LOG_BUFFER_HEX(TAG, output_buffer, data_read);
            //     // ESP_LOGI(TAG, "resp:%s", output_buffer);
            // }
            // else
            // {
            //     ESP_LOGE(TAG, "Failed to read response");
            // }
            // ESP_LOGI(TAG, "resp:%s", buffer);

            cJSON *root = cJSON_Parse(buffer);
            if (root)
            {
                kline.Ok = true;
                int array_size = cJSON_GetArraySize(root);
                for (int i = array_size - 1; i >= 0; i--)
                {
                    cJSON *line = cJSON_GetArrayItem(root, i);
                    if (!cJSON_IsArray(line))
                        continue;
                    const char *open = cJSON_GetArrayItem(line, 1)->valuestring;
                    double open_num = strtod(open, NULL);
                    int index = i - (array_size - 20);
                    kline.open[index] = open_num;
                    if (index == 0)
                    {
                        break;
                    }
                }
                cJSON_Delete(root);
            }
            free(buffer);
        }
    }
end:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return kline;
}

static GasFee get_basefee()
{
    GasFee gas_fee = {.Ok = false};
    esp_http_client_config_t config = {
        .url = "https://api.etherscan.io/v2/api?chainid=1&module=gastracker&action=gasoracle&apikey=" ETHERSCAN_API_KEY,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    }
    else
    {
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0)
        {
            ESP_LOGE(TAG, "HTTP client fetch headers failed");
        }
        else
        {
            char *buffer = malloc(content_length + 1);
            buffer[0] = 0;
            buffer[content_length] = 0;
            int data_read = esp_http_client_read_response(client, buffer, content_length);
            if (data_read == 0)
            {
                free(buffer);
                goto end;
            }
            cJSON *root = cJSON_Parse(buffer);
            if (root)
            {
                if (strcmp(cJSON_GetObjectItem(root, "status")->valuestring, "1") == 0)
                {
                    cJSON *result = cJSON_GetObjectItem(root, "result");
                    const char *suggestBaseFee = cJSON_GetObjectItem(result, "suggestBaseFee")->valuestring;
                    double suggestBaseFee_num = strtod(suggestBaseFee, NULL);
                    gas_fee.suggestBaseFee = suggestBaseFee_num;
                    gas_fee.Ok = true;
                }
                cJSON_Delete(root);
            }
            free(buffer);
        }
    }
end:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return gas_fee;
}

static esp_err_t check_network_status(void)
{
    esp_http_client_config_t config = {
        .url = "http://www.bing.com",
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
        if (esp_http_client_get_status_code(client) != 200)
            err = ESP_FAIL;
    esp_http_client_cleanup(client);
    return err;
}

void app_main(void)
{
    i2c_master_init();
    lcd_init(I2C_MASTER_NUM);
    lcd_backlight_off();
    lcd_clear();

    wifi_connect_start();
    bool connection_status = false;

    lcd_clear();
    lcd_put_cur(0, 0);
    lcd_send_string("WIFI");
    lcd_put_cur(1, 0);
    lcd_send_string("connecting");

    int last_y = 0;

    for (int i = 0;; i++)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        bool connection_status_changed = false;
        if (check_wifi_status())
        {
            if (connection_status == false)
            {
                if (i % 3 == 0 && check_network_status() == ESP_OK)
                {
                    connection_status = true;
                    connection_status_changed = true;
                }
            }
        }
        else
        {
            if (connection_status != false)
            {
                connection_status = false;
                connection_status_changed = true;
            }
        }
        if (connection_status_changed)
        {
            lcd_clear();
            if (connection_status)
            {
                lcd_put_cur(0, 5);
                lcd_send_string("GAS        ");
                lcd_put_cur(1, 5);
                lcd_send_string("ETH $.     ");
            }
            else
            {
                lcd_put_cur(0, 0);
                lcd_send_string("WIFI");
                lcd_put_cur(1, 0);
                lcd_send_string("connecting");
            }
        }
        else
        {
            if (connection_status)
            {
                double now = esp_timer_get_time() / 1000000.0;
                if (lastUpdate_basefee == 0 || now - lastUpdate_basefee > 30)
                {
                    lastUpdate_basefee = now;
                    // base fee
                    GasFee gas_fee = get_basefee();
                    char buf[10];
                    if (gas_fee.Ok)
                        snprintf(buf, sizeof(buf), "%7.2f", gas_fee.suggestBaseFee);
                    else
                        snprintf(buf, sizeof(buf), " ------");
                    lcd_put_cur(0, 9);
                    lcd_send_string(buf);
                }

                if (lastUpdate_price == 0 || now - lastUpdate_price > 60 * 2)
                {
                    Kline kline = get_kline_binance();
                    if (kline.Ok)
                    {
                        lcd_backlight_off();
                        lastUpdate_price = now;
                        { // #TODO real time price
                            char buf[10];
                            snprintf(buf, sizeof(buf), "$%f", kline.open[19]);
                            lcd_put_cur(1, 9);
                            lcd_send_string(buf);
                        }
                        { // kline
                            double high = kline.open[0];
                            double low = kline.open[0];
                            for (int j = 0; j < 20; j++)
                            {
                                if (kline.open[j] > high)
                                    high = kline.open[j];
                                if (kline.open[j] < low)
                                    low = kline.open[j];
                            }
                            double step = (high - low) / 16;
                            klineBitMapClear();
                            for (int x = 0; x < 20; x++)
                            {
                                int y = (kline.open[x] - low) / step;
                                if (true && x < 19)
                                {
                                    int y_next = (kline.open[x + 1] - low) / step;
                                    int diff = abs(y - y_next);
                                    if (diff > 1)
                                    {
                                        for (int _y = 1; _y < diff; _y++)
                                        {
                                            if (y > y_next)
                                                klineSetPixel(x, y - _y);
                                            else
                                                klineSetPixel(x, y + _y);
                                        }
                                    }
                                }
                                klineSetPixel(x, y);
                                if (x == 19)
                                {
                                    last_y = y;
                                }
                            }
                            for (int j = 0; j < 8; j++)
                            {
                                lcd_create_char(j, klineBitMap[j]);
                                vTaskDelay(20 / portTICK_PERIOD_MS);
                            }
                            for (int i = 0; i < 4; i++)
                            {
                                lcd_put_cur(0, i);
                                lcd_send_data(i);
                            }
                            for (int i = 0; i < 4; i++)
                            {
                                lcd_put_cur(1, i);
                                lcd_send_data(i + 4);
                            }
                        }
                    }
                    else
                    {
                        lcd_backlight_on();
                        vTaskDelay(30000 / portTICK_PERIOD_MS);
                    }
                }

                if (lastUpdate_price >= 0)
                {
                    if (i % 2 == 0)
                    {
                        klineClearPixel(19, last_y);
                    }
                    else
                    {
                        klineSetPixel(19, last_y);
                    }
                    lcd_create_char(3, klineBitMap[3]);
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                    lcd_create_char(7, klineBitMap[7]);
                    vTaskDelay(20 / portTICK_PERIOD_MS);
                }
            }
            else
            {
                lcd_put_cur(1, 10);
                switch (i % 4)
                {
                case 0:
                    lcd_send_string("   ");
                    break;
                case 1:
                    lcd_send_string(".  ");
                    break;
                case 2:
                    lcd_send_string(".. ");
                    break;
                case 3:
                    lcd_send_string("...");
                    break;
                default:
                    break;
                }
            }
        }
    }

    for (int i = 0;; i++)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        if (i % 2 == 0)
            lcd_backlight_on();
        else
            lcd_backlight_off();
    }
}
