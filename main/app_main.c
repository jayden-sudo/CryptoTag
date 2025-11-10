#include "esp_chip_info.h"
#include "esp_system.h"
#include "driver/i2c.h"
#include "i2c_lcd.h"
#include "esp_log.h"
#include "wifi_connect.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "config.h"
#include "http_request.h"
#include <freertos/task.h>

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

#ifdef USE_ALLTICK
static Kline *get_kline()
{
    char *buffer = http_get("https://quote.alltick.io/quote-b-api/kline?token=" ALLTICK_TOKEN "&query={%22data%22:{%22code%22:%22ETHUSDT%22,%22kline_type%22:%221%22,%22kline_timestamp_end%22:%220%22,%22query_kline_num%22:%22100%22,%22adjust_type%22:%220%22}}");
    ESP_LOGI(TAG, "get_kline end");
    if (buffer == NULL)
    {
        return NULL;
    }
    /*
        {
            "ret": 200,
            "data": {
                "kline_list": [
                    {
                        "open_price": "3591.35"
                    },
                    {
                        "open_price": "3588.60"
                    }
                ]
            }
        }
    */

    Kline *kline = malloc(sizeof(Kline));
    kline->Ok = false;
    cJSON *root = cJSON_Parse(buffer);
    if (root)
    {
        if (cJSON_GetObjectItem(root, "ret")->valueint != 200)
        {
            cJSON_Delete(root);
            free(buffer);
            return kline;
        }
        cJSON *data = cJSON_GetObjectItem(root, "data");
        cJSON *kline_list = cJSON_GetObjectItem(data, "kline_list");
        int array_size = cJSON_GetArraySize(kline_list);
        int _i = 0;
        for (int i = 0; i < array_size; i += 5)
        {
            cJSON *item = cJSON_GetArrayItem(kline_list, i);
            // get open_price
            const char *open_price = cJSON_GetObjectItem(item, "open_price")->valuestring;
            double open_price_num = strtod(open_price, NULL);
            kline->open[_i] = open_price_num;
            _i++;
        }
        kline->Ok = true;
        cJSON_Delete(root);
    }
    free(buffer);
    return kline;
}
#else
static Kline *get_kline()
{
    char *buffer = http_get("https://api.binance.com/api/v3/klines?symbol=ETHUSDT&interval=5m&limit=25");
    ESP_LOGI(TAG, "get_kline end");
    if (buffer == NULL)
    {
        return NULL;
    }
    Kline *kline = malloc(sizeof(Kline));
    kline->Ok = false;
    cJSON *root = cJSON_Parse(buffer);
    if (root)
    {
        kline->Ok = true;
        int array_size = cJSON_GetArraySize(root);
        for (int i = array_size - 1; i >= 0; i--)
        {
            cJSON *line = cJSON_GetArrayItem(root, i);
            if (!cJSON_IsArray(line))
                continue;
            const char *open = cJSON_GetArrayItem(line, 1)->valuestring;
            double open_num = strtod(open, NULL);
            int index = i - (array_size - 20);
            kline->open[index] = open_num;
            if (index == 0)
            {
                break;
            }
        }
        cJSON_Delete(root);
    }
    free(buffer);
    return kline;
}
#endif

static GasFee *get_basefee()
{
    char *buffer = http_get("https://api.etherscan.io/v2/api?chainid=1&module=gastracker&action=gasoracle&apikey=" ETHERSCAN_API_KEY);
    if (buffer == NULL)
    {
        return NULL;
    }
    cJSON *root = cJSON_Parse(buffer);

    GasFee *gas_fee = malloc(sizeof(GasFee));
    gas_fee->Ok = false;
    if (root)
    {
        if (strcmp(cJSON_GetObjectItem(root, "status")->valuestring, "1") == 0)
        {
            cJSON *result = cJSON_GetObjectItem(root, "result");
            const char *suggestBaseFee = cJSON_GetObjectItem(result, "suggestBaseFee")->valuestring;
            double suggestBaseFee_num = strtod(suggestBaseFee, NULL);
            gas_fee->suggestBaseFee = suggestBaseFee_num;
            gas_fee->Ok = true;
        }
        cJSON_Delete(root);
    }
    free(buffer);
    return gas_fee;
}

typedef struct
{
    GasFee *gas;
    Kline *kline;
    double update_gas;
    double update_kline;
} fetch_response_t;

static fetch_response_t response = {0};

void fetch_data()
{
    for (;;)
    {
        double now = esp_timer_get_time() / 1000000.0;
        if (response.gas == NULL && (response.update_gas == 0 || now - response.update_gas > 30))
        {
            ESP_LOGI(TAG, "fetch-gas");
            response.gas = get_basefee();
            response.update_gas = now;
        }

        static const int PRICE_UPDATE_INTERVAL =
#ifdef USE_ALLTICK
            30;
#else
            60 * 2;
#endif
        if (response.kline == NULL && (response.update_kline == 0 || now - response.update_kline > PRICE_UPDATE_INTERVAL))
        {
            ESP_LOGI(TAG, "fetch-kline");
            response.kline = get_kline();
            response.update_kline = now;
        }

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

static int last_y = 0;

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

    BaseType_t ret = xTaskCreate(fetch_data, "fetch_data", 5 * 1024, NULL, 10, NULL);
    if (ret != pdTRUE)
    {
        ESP_LOGE(TAG, "xTaskCreate failed");
    }

    for (int i = 0;; i++)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        bool connection_status_changed = false;
        bool new_status = check_wifi_status();
        if (new_status != connection_status)
        {
            connection_status_changed = true;
        }
        connection_status = new_status;
        if (connection_status_changed)
        {
            lcd_clear();
            if (connection_status)
            {
                lcd_put_cur(0, 5);
                lcd_send_string("GAS        ");
                lcd_put_cur(1, 5);
                lcd_send_string("ETH $      ");
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
                if (response.gas != NULL)
                {
                    char buf[10];
                    if (response.gas->Ok)
                        snprintf(buf, sizeof(buf), "%7.2f", response.gas->suggestBaseFee);
                    else
                        snprintf(buf, sizeof(buf), " error");
                    free(response.gas);
                    response.gas = NULL;
                    lcd_put_cur(0, 9);
                    lcd_send_string(buf);
                }
                if (response.kline != NULL)
                {
                    Kline *kline = response.kline;
                    if (kline->Ok)
                    {
                        lcd_backlight_off();
                        {
                            char buf[10];
                            snprintf(buf, sizeof(buf), "$%f", kline->open[19]);
                            lcd_put_cur(1, 9);
                            lcd_send_string(buf);
                        }
                        { // kline
                            double high = kline->open[0];
                            double low = kline->open[0];
                            for (int j = 0; j < 20; j++)
                            {
                                if (kline->open[j] > high)
                                    high = kline->open[j];
                                if (kline->open[j] < low)
                                    low = kline->open[j];
                            }
                            double step = (high - low) / 16;
                            klineBitMapClear();
                            for (int x = 0; x < 20; x++)
                            {
                                int y = (kline->open[x] - low) / step;
                                if (true && x < 19)
                                {
                                    int y_next = (kline->open[x + 1] - low) / step;
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
                    }
                    free(response.kline);
                    response.kline = NULL;
                }

                if (response.update_kline >= 0)
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
