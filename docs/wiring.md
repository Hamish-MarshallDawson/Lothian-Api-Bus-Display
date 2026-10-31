# Wiring

BCM numbering. The pin numbers match `third_party/waveshare/Config/DEV_Config.h`.

## Display (SPI)

| Display pin | Pi pin | BCM GPIO |
|-------------|--------|----------|
| VCC | 3.3 V | |
| GND | GND | |
| MOSI (LCD_DIN) | SPI0 MOSI | 10 |
| SCLK (LCD_CLK) | SPI0 SCLK | 11 |
| MISO | SPI0 MISO | 9 |
| LCD_CS | SPI0 CE0 | 8 |
| LCD_DC | | 25 |
| LCD_RST | | 27 |
| LCD_BL (backlight) | | 18 |

## Touch (I2C and interrupt)

| Touch pin | BCM GPIO |
|-----------|----------|
| TP_SDA | 2 (I2C1 SDA) |
| TP_SCL | 3 (I2C1 SCL) |
| TP_INT | 4 |
| TP_RST | 17 |
