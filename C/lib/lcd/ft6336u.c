#include "ft6336u.h"


static bool ft6336u_read_flag = true;
static bool ft6336u_int_enable = false;


static void ft6336u_read(uint8_t reg_addr, uint8_t *data, uint32_t length) {
    I2C_Read_nByte(reg_addr, data, length);
}

bool get_touch_data(touch_data_t *touch_data) {
  uint8_t buff[12];
  uint8_t touch_num = 0;

  if (ft6336u_int_enable) {
    if (false == ft6336u_read_flag) {
      // Serial.println("ft6336u_read_flag is false");
      return false;
    } else {
      ft6336u_read_flag = false;
    }
  }

  ft6336u_read(FT6336U_TOUCH_NUM_REG, &touch_num, 1);
  touch_data->touch_num = touch_num;
  if (touch_num == 0)
    return false;

  ft6336u_read(FT6336U_TOUCH_XH_REG, buff, touch_num * 6);

  for (uint16_t i = 0; i < touch_num; i++) {
    touch_data->coords[i].x = 319 -( (((uint16_t)buff[(i * 6) + 0] & 0x0f) << 8) + buff[(i * 6) + 1]);
    touch_data->coords[i].y = (((uint16_t)buff[(i * 6) + 2] & 0x0f) << 8) + buff[(i * 6) + 3];
  }
  return true;
}


// 中断服务函数
void ft6336_touch_int_cb(void) {
  ft6336u_read_flag = true;
}


void ft6336u_touch_rst(void)
{
    TP_RST_0;
    DEV_Delay_ms(100);
    TP_RST_1;
    DEV_Delay_ms(200);
}

void ft6336u_init(void)
{
    uint8_t id = 0;
    ft6336u_touch_rst();

    ft6336u_read(FT6336U_ID_REG, &id, 1);
    if (0 != id)
    {
        DEBUG("ft6336u id:%x\r\n", id);
    }
    wiringPiISR(TP_INT,INT_EDGE_FALLING,&ft6336_touch_int_cb);
    ft6336u_int_enable = true;
}

