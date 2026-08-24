#include "test.h"
#include <math.h>
#include <stdlib.h>     //exit()
#include <stdio.h>

#include "DEV_Config.h"
#include "st7796.h"
#include "ft6336u.h"


int main()
{
    touch_data_t touch_data;
    /* Module Init */
	if(DEV_ModuleInit() != 0){
        DEV_ModuleExit();
        exit(0);
    }
	
    st7796_init();
    ft6336u_init();

    st7796_clear(0XF800);
    DEV_Delay_ms(1000);
    st7796_clear(0X400);
    DEV_Delay_ms(1000);

    while (1){
        if (get_touch_data(&touch_data)) {
            printf("x: %d, y: %d \r\n", touch_data.coords[0].x, touch_data.coords[0].y);
            st7796_draw_rectangle(touch_data.coords[0].x, touch_data.coords[0].y, touch_data.coords[0].x + 10, touch_data.coords[0].y + 10, 0XF800);
        }
        DEV_Delay_ms(10);
    }
    
    return 0;
}
