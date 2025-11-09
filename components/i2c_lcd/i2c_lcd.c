#include <stdio.h>
#include "i2c_lcd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "I2C_LCD"

// PCF8574T pin connections to the LCD
#define LCD_BACKLIGHT 0x08 // P3
#define LCD_EN 0x04        // P2
#define LCD_RW 0x02        // P1
#define LCD_RS 0x01        // P0

static uint8_t backlight_state = LCD_BACKLIGHT;
static i2c_port_t s_i2c_port; // Store the I2C port number

static esp_err_t i2c_write_byte(uint8_t data)
{
    // ESP_LOGD(TAG, "Writing byte: 0x%02X", data);
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (LCD_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, data | backlight_state, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(s_i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C Write Error: %s", esp_err_to_name(ret));
    }
    return ret;
}

static void lcd_send_four_bits(uint8_t data)
{
    i2c_write_byte(data);
    i2c_write_byte(data | LCD_EN);
    vTaskDelay(1 / portTICK_PERIOD_MS);
    i2c_write_byte(data);
    vTaskDelay(5 / portTICK_PERIOD_MS);
}

void lcd_send_cmd(char cmd)
{
    uint8_t high_nibble = cmd & 0xF0;
    uint8_t low_nibble = (cmd << 4) & 0xF0;
    lcd_send_four_bits(high_nibble);
    lcd_send_four_bits(low_nibble);
}

void lcd_send_data(char data)
{
    uint8_t high_nibble = data & 0xF0;
    uint8_t low_nibble = (data << 4) & 0xF0;
    lcd_send_four_bits(high_nibble | LCD_RS);
    lcd_send_four_bits(low_nibble | LCD_RS);
}

void lcd_clear(void)
{
    lcd_send_cmd(0x01); // Clear screen
    vTaskDelay(10 / portTICK_PERIOD_MS);
}

void lcd_put_cur(int row, int col)
{
    int row_offsets[] = {0x00, 0x40};
    if (row > 1)
    {
        row = 1;
    }
    lcd_send_cmd(0x80 | (col + row_offsets[row]));
}

void lcd_init(i2c_port_t i2c_num)
{
    s_i2c_port = i2c_num;                // Store the port number
    vTaskDelay(50 / portTICK_PERIOD_MS); // Wait for LCD to power up
    lcd_send_four_bits(0x30);
    vTaskDelay(5 / portTICK_PERIOD_MS);
    lcd_send_four_bits(0x30);
    vTaskDelay(1 / portTICK_PERIOD_MS);
    lcd_send_four_bits(0x30);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    lcd_send_four_bits(0x20); // Set to 4-bit mode
    vTaskDelay(10 / portTICK_PERIOD_MS);

    lcd_send_cmd(0x28); // Function set: 4-bit, 2-line, 5x8 dot matrix
    vTaskDelay(1 / portTICK_PERIOD_MS);
    lcd_send_cmd(0x08); // Display off
    vTaskDelay(1 / portTICK_PERIOD_MS);
    lcd_send_cmd(0x01); // Clear screen
    vTaskDelay(2 / portTICK_PERIOD_MS);
    lcd_send_cmd(0x06); // Entry mode set
    vTaskDelay(1 / portTICK_PERIOD_MS);
    lcd_send_cmd(0x0C); // Display on, cursor off, blink off
    vTaskDelay(2 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "LCD Initialized");
}

void lcd_send_string(const char *str)
{
    while (*str)
    {
        lcd_send_data(*str++);
    }
}

void lcd_backlight_on(void)
{
    backlight_state = LCD_BACKLIGHT;
    i2c_write_byte(0); // Send a dummy byte to update backlight state immediately
}

void lcd_backlight_off(void)
{
    backlight_state = 0x00;
    i2c_write_byte(0); // Send a dummy byte to update backlight state immediately
}

void lcd_create_char(uint8_t location, uint8_t charmap[])
{
    location &= 0x7; // We only have 8 locations (0-7)
    lcd_send_cmd(0x40 | (location << 3));
    for (int i = 0; i < 8; i++)
    {
        lcd_send_data(charmap[i]);
    }
    lcd_send_cmd(0x80); // Return to DDRAM address
}
