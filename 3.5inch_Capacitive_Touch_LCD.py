#!/usr/bin/python
# -*- coding: UTF-8 -*-
#import chardet
import os
import sys 
import time
import logging
import st7796
import ft6336u
from PIL import Image,ImageDraw,ImageFont

if __name__=='__main__':
    
    disp = st7796.st7796()
    disp.clear()
    touch = ft6336u.ft6336u()
    

    logging.info("show image")
    ImagePath = ["./pic/img_4.jpg", "./pic/img_5.jpg", "./pic/img_6.jpg", "./pic/img_7.jpg"]
    for i in range(0, 4):
        image = Image.open(ImagePath[i])	
        # image = image.rotate(0)
        disp.show_image(image)
        time.sleep(4)
    
    disp.clear()

    while True:
        touch.read_touch_data()
        point, coordinates = touch.get_touch_xy()
        if point != 0 and coordinates:
            disp.dre_rectangle(
                coordinates[0]['x'], coordinates[0]['y'],
                coordinates[0]['x'] + 5, coordinates[0]['y'] + 5,
                0x00ff  # 矩形的颜色
            )
            print(f"point 1 coordinates: x={coordinates[0]['x']}, y={coordinates[0]['y']}")
        time.sleep(0.02)
