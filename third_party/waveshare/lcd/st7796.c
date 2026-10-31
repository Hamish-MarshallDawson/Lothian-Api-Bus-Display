#include "st7796.h"

static void st7796_write_bytes(uint8_t *data, uint32_t lenght)
{
    LCD_DC_1;
    DEV_SPI_Write_nByte(data, lenght);
}

static void st7796_write_byte(uint8_t data)
{
    LCD_DC_1;
    //LCD_2IN8_CS_0;
    DEV_SPI_WriteByte(data);
    //LCD_2IN8_CS_1;
}

static void st7796_write_cmd(uint8_t cmd)
{
    LCD_DC_0;
    //LCD_2IN8_CS_0;
    DEV_SPI_WriteByte(cmd);
    //LCD_2IN8_CS_1;
}


static void st7796_rst(void)
{
    LCD_RST_1;
    DEV_Delay_ms(100);
    LCD_RST_0;
    DEV_Delay_ms(100);
    LCD_RST_1;
    DEV_Delay_ms(100);
}



void st7796_init(void)
{
    st7796_rst();

    st7796_write_cmd(0x11);     

    DEV_Delay_ms(120);                //DEV_Delay_ms 120ms

    st7796_write_cmd(0x36);     // Memory Data Access Control MY,MX~~
    st7796_write_byte(0x08);   

    st7796_write_cmd(0x3A);     
    st7796_write_byte(0x05);   //st7796_write_byte(0x66);

    st7796_write_cmd(0xF0);     // Command Set Control
    st7796_write_byte(0xC3);   

    st7796_write_cmd(0xF0);     
    st7796_write_byte(0x96);   

    st7796_write_cmd(0xB4);     
    st7796_write_byte(0x01);   

    st7796_write_cmd(0xB7);     
    st7796_write_byte(0xC6);   

    st7796_write_cmd(0xC0);     
    st7796_write_byte(0x80);   
    st7796_write_byte(0x45);   

    st7796_write_cmd(0xC1);     
    st7796_write_byte(0x13);   //18  //00

    st7796_write_cmd(0xC2);     
    st7796_write_byte(0xA7);   

    st7796_write_cmd(0xC5);     
    st7796_write_byte(0x0A);   

    st7796_write_cmd(0xE8);     
    st7796_write_byte(0x40);
    st7796_write_byte(0x8A);
    st7796_write_byte(0x00);
    st7796_write_byte(0x00);
    st7796_write_byte(0x29);
    st7796_write_byte(0x19);
    st7796_write_byte(0xA5);
    st7796_write_byte(0x33);

    st7796_write_cmd(0xE0);
    st7796_write_byte(0xD0);
    st7796_write_byte(0x08);
    st7796_write_byte(0x0F);
    st7796_write_byte(0x06);
    st7796_write_byte(0x06);
    st7796_write_byte(0x33);
    st7796_write_byte(0x30);
    st7796_write_byte(0x33);
    st7796_write_byte(0x47);
    st7796_write_byte(0x17);
    st7796_write_byte(0x13);
    st7796_write_byte(0x13);
    st7796_write_byte(0x2B);
    st7796_write_byte(0x31);

    st7796_write_cmd(0xE1);
    st7796_write_byte(0xD0);
    st7796_write_byte(0x0A);
    st7796_write_byte(0x11);
    st7796_write_byte(0x0B);
    st7796_write_byte(0x09);
    st7796_write_byte(0x07);
    st7796_write_byte(0x2F);
    st7796_write_byte(0x33);
    st7796_write_byte(0x47);
    st7796_write_byte(0x38);
    st7796_write_byte(0x15);
    st7796_write_byte(0x16);
    st7796_write_byte(0x2C);
    st7796_write_byte(0x32);


    st7796_write_cmd(0xF0);     
    st7796_write_byte(0x3C);   

    st7796_write_cmd(0xF0);     
    st7796_write_byte(0x69);   

    DEV_Delay_ms(120);                

    st7796_write_cmd(0x21);     

    st7796_write_cmd(0x29); 

}


void st7796_set_windows(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    uint8_t buffer[4];

    buffer[0] = x_start >> 8;
    buffer[1] = (uint8_t)(x_start & 0x00ff);
    buffer[2] = x_end >> 8;
    buffer[3] = (uint8_t)(x_end & 0x00ff);
    st7796_write_cmd(0x2a);
    st7796_write_bytes(buffer, 4);

    buffer[0] = y_start >> 8;
    buffer[1] = (uint8_t)(y_start & 0x00ff);
    buffer[2] = y_end >> 8;
    buffer[3] = (uint8_t)(y_end & 0x00ff);
    st7796_write_cmd(0x2b);
    st7796_write_bytes(buffer, 4);
    st7796_write_cmd(0x2c);
}


void st7796_draw_rectangle(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t color)
{
    uint8_t color_h = color >> 8;
    uint8_t color_l = (uint8_t)(color && 0x00ff);
    uint16_t i = 0;
    uint16_t color_lenght = (1 + x_end - x_start) * (1 + y_end - y_start);
    st7796_set_windows(x_start, y_start, x_end, y_end);
    for (i = 0; i < color_lenght; i++){
        st7796_write_byte(color_h);
        st7796_write_byte(color_l);
    }
}

void st7796_flush(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t *color)
{

}

uint16_t swap_uint16(uint16_t val) {
    return (val << 8) | (val >> 8);
}

void st7796_clear(uint16_t color)
{
    
    uint16_t buf[ST7796_WIDTH];
    uint16_t i;
    color = swap_uint16(color);
    for (i = 0; i < ST7796_WIDTH; i++)
        buf[i] = color;

    st7796_set_windows(0, 0, ST7796_WIDTH - 1, ST7796_HEIGHT - 1);

    for (i = 0; i < ST7796_HEIGHT; i++)
        st7796_write_bytes((uint8_t *)buf ,ST7796_WIDTH * 2);
}

// Put the display panel into sleep (low power). Touch controller stays active
// because it's on a separate I2C bus.
void st7796_sleep(void)
{
    // Display OFF
    st7796_write_cmd(0x28);
    DEV_Delay_ms(20);
    // Enter sleep mode
    st7796_write_cmd(0x10);
    DEV_Delay_ms(120);  // allow panel to fully enter sleep
}

// Wake the display panel from sleep back to normal operation.
void st7796_wakeup(void)
{
    // Exit sleep mode
    st7796_write_cmd(0x11);
    DEV_Delay_ms(120);  // per datasheet, wait after SLPOUT
    // Display ON
    st7796_write_cmd(0x29);
    DEV_Delay_ms(20);
}
