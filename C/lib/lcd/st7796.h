#ifndef __ST7796_H__
#define __ST7796_H__

#include "DEV_Config.h"
#include <stdint.h>

#include <stdlib.h>		//itoa()
#include <stdio.h>


#define ST7796_WIDTH        320
#define ST7796_HEIGHT       480

void st7796_init(void);
void st7796_draw_rectangle(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end, uint16_t color);
void st7796_clear(uint16_t color);

// Put the LCD panel into low-power sleep and wake it again.
void st7796_sleep(void);
void st7796_wakeup(void);

#endif  // __ST7796_H__

