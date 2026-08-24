#ifndef __FT6336U_H__
#define __FT6336U_H__

#include "DEV_Config.h"
#include <stdint.h>

#include <stdlib.h>		//itoa()
#include <stdio.h>

#include "Debug.h"

#include "stdbool.h"


#define FT6336U_ADDR 0x38
#define FT6336U_ID_REG 0xa8

#define FT6336U_TOUCH_NUM_REG 0x02
#define FT6336U_TOUCH_XH_REG 0x03
#define FT6336U_TOUCH_XL_REG 0x04
#define FT6336U_TOUCH_YH_REG 0x05
#define FT6336U_TOUCH_YL_REG 0x06

#define FT6336U_TOUCH_MAX_NUM   2


typedef struct{
    uint16_t x;
    uint16_t y;
}touch_coords_t;

typedef struct{
  touch_coords_t coords[FT6336U_TOUCH_MAX_NUM];
  uint16_t touch_num;
}touch_data_t;

void ft6336u_init(void);

bool get_touch_data(touch_data_t *touch_data);


#endif  // __FT6336U_H__
