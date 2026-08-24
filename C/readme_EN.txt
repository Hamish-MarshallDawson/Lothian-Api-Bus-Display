/*****************************************************************************
* | File      	:   Readme_EN.txt
* | Author      :   Waveshare team
* | Function    :   Help with use
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2024-03-13
* | Info        :   Here is an English version of the documentation for your quick use.
******************************************************************************/
This file is to help you use this demo.

1. Basic information:
This demo has been verified using the Separate LCD module. 
You can view the corresponding test demos in the \Examples\ of the project.
This Demo has been verified on the Raspberry Pi 4B;

2. Pin connection:
Pin connection You can view it in DEV_Config.h in the \lib\Config\ directory, and repeat it here:
LCD         ->    RPI(BCM)
VCC         ->    3.3V
GND        ->    GND
LCD_DIN  ->    10(SPI0_MOSI)
LCD_CLK  ->    11(SPI0_SCK)
LCD_CS    ->    8(CE0)
LCD_DC    ->    25
LCD_RST   ->    27
LCD_BL     ->    18

Touch  	=>	  RPI(BCM)
TP_SDA  ->    SDA
TP_SCL  ->    SCL
TP_RST  ->    17
TP_IRQ  ->    4

3.Installation library
git clone https://github.com/WiringPi/WiringPi
cd WiringPi
./build
gpio -v
# Run gpio -v and version will appear. If it does not appear, it means that there is an installation error

4. Basic use:
you need to execute: 
    make
compile the program, will generate the executable file: main
 If you need to run the example, then you should execute the command:
    sudo ./main
If you modify the program, you need to execute: 
	make clear
then:
    make

5. Directory structure (selection):
If you use our products frequently, you will be very familiar with our program directory structure. We have a copy of the specific function.
The API manual for the function, you can download it on our WIKI or request it as an after-sales customer service. Here is a brief introduction:
Config\: This directory is a hardware interface layer file. You can see many definitions in DEV_Config.c(.h), including:
   data type;
    GPIO;
    Read and write GPIO;
    Delay: Note: This delay function does not use an oscilloscope to measure specific values,so it will be inaccurate.
    Module Init and exit processing:
        void DEV_Module_Init(void);
        void DEV_Module_Exit(void);
            
\lib\GUI\: This directory is some basic image processing functions, in GUI_Paint.c(.h):
    Common image processing: creating graphics, flipping graphics, mirroring graphics, setting pixels, clearing screens, etc.
    Common drawing processing: drawing points, lines, boxes, circles, Chinese characters, English characters, numbers, etc.;
    Common time display: Provide a common display time function;
    Commonly used display pictures: provide a function to display bitmaps;
    
\lib\Fonts\: for some commonly used fonts:
    Ascii:
        Font8: 5*8
        Font12: 7*12
        Font16: 11*16
        Font20: 14*20
        Font24: 17*24
    Chinese:
        font12CN: 16*21
        font24CN: 32*41
        
\lib\LCD\: This screen is the LCD driver functions and touch driver functions;
Examples\: This is the test program for the LCD. You can see the specific usage method in it.