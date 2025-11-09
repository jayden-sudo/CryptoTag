#ifndef I2C_LCD_H
#define I2C_LCD_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "driver/i2c.h"

// PCF8574T is 0x27
#define LCD_I2C_ADDRESS 0x27

    void lcd_init(i2c_port_t i2c_num);
    void lcd_send_cmd(char cmd);
    void lcd_send_data(char data);
    void lcd_send_string(const char *str);
    void lcd_clear(void);
    void lcd_put_cur(int row, int col);
    void lcd_backlight_on(void);
    void lcd_backlight_off(void);
    void lcd_create_char(uint8_t location, uint8_t charmap[]);

#ifdef __cplusplus
}
#endif

#endif // I2C_LCD_H
