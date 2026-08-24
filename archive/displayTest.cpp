/*
 * Simple Display Test
 * Just displays text on the screen to verify hardware works
 */

extern "C" {
    #include "../C/lib/Config/DEV_Config.h"
    #include "../C/lib/lcd/st7796.h"
    #include "../C/lib/GUI/GUI_Paint.h"
    #include "../C/lib/Fonts/fonts.h"
    
    // Forward declare the internal function
    void st7796_set_windows(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end);
}

#include <iostream>
#include <cstdint>
#include <cstdlib>

int main() {
    std::cout << "=== Simple Display Test ===" << std::endl;
    
    // Initialize hardware
    if(DEV_ModuleInit() != 0) {
        std::cerr << "Failed to initialize display" << std::endl;
        return 1;
    }
    
    std::cout << "Hardware initialized" << std::endl;
    
    // Initialize display
    st7796_init();
    
    // // Test 1: Color screens (should work - you've seen this)
    // std::cout << "Test 1: Colored screens..." << std::endl;
    // st7796_clear(0xF800);  // Red
    // DEV_Delay_ms(1000);
    // st7796_clear(0x07E0);  // Green
    // DEV_Delay_ms(1000);
    // st7796_clear(0x001F);  // Blue
    // DEV_Delay_ms(1000);
    // st7796_clear(BLACK);   // Black
    
    // std::cout << "Test 2: Drawing rectangles directly..." << std::endl;
    // // Draw some colored rectangles directly
    // st7796_draw_rectangle(10, 10, 100, 100, RED);
    // DEV_Delay_ms(1000);
    // st7796_draw_rectangle(120, 10, 210, 100, GREEN);
    // DEV_Delay_ms(1000);
    // st7796_draw_rectangle(10, 110, 100, 200, BLUE);
    // DEV_Delay_ms(2000);
    
    std::cout << "Test 3: Using Paint library..." << std::endl;
    
    // Clear screen
    st7796_clear(BLACK);
    
    // Allocate image buffer
    uint32_t imageSize = ((uint32_t)ST7796_WIDTH) * ((uint32_t)ST7796_HEIGHT);
    UWORD *imageBuffer = (UWORD *)malloc(imageSize * sizeof(UWORD));
    
    if(imageBuffer == NULL) {
        std::cerr << "Failed to allocate image buffer" << std::endl;
        DEV_ModuleExit();
        return 1;
    }
    
    std::cout << "Image buffer allocated: " << imageSize << " pixels" << std::endl;
    
    // Initialize Paint - try different rotation values
    // 0 = 0 degrees, 90 = 90 degrees, 180 = 180 degrees, 270 = 270 degrees
    Paint_NewImage(imageBuffer, ST7796_WIDTH, ST7796_HEIGHT, 0, BLACK, 16);
    
    // Try mirroring to fix backwards text
    // MIRROR_HORIZONTAL = flip left-right
    // MIRROR_VERTICAL = flip up-down  
    // MIRROR_ORIGIN = flip both ways
    Paint_SetMirroring(MIRROR_HORIZONTAL);
    
    Paint_Clear(BLACK);
    
    std::cout << "Paint initialized with MIRROR_HORIZONTAL" << std::endl;
    
    // Draw some text with Paint
    // With MIRROR_HORIZONTAL, X coordinates are mirrored, so use larger X values
    std::cout << "Drawing text with Paint..." << std::endl;
    Paint_DrawString_EN(200, 50, "Hello!", &Font24, WHITE, YELLOW);
    Paint_DrawString_EN(120, 100, "Bus tracker", &Font20, WHITE, RED);
    Paint_DrawString_EN(200, 150, "Test 123", &Font16, YELLOW, BLACK);
    
    // Draw a box
    Paint_DrawRectangle(10, 200, 200, 250, RED, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawString_EN(20, 215, "In a box!", &Font16, CYAN, BLACK);
    
    std::cout << "Text drawn to buffer, now sending to display..." << std::endl;
    
    // Push buffer to display - send line by line like st7796_clear() does
    st7796_set_windows(0, 0, ST7796_WIDTH - 1, ST7796_HEIGHT - 1);
    
    // DON'T swap bytes - let's try sending directly
    std::cout << "Sending data to display (no byte swap)..." << std::endl;
    LCD_DC_1;  // Set to data mode
    
    for(uint16_t y = 0; y < ST7796_HEIGHT; y++) {
        // Send one row at a time
        DEV_SPI_Write_nByte((uint8_t*)&imageBuffer[y * ST7796_WIDTH], ST7796_WIDTH * 2);
    }
    
    std::cout << "Buffer sent to display!" << std::endl;
    std::cout << "You should see colored text on the screen." << std::endl;
    std::cout << "Press Ctrl+C to exit..." << std::endl;
    
    // Wait forever
    while(1) {
        DEV_Delay_ms(1000);
    }
    
    // Cleanup (won't reach here unless Ctrl+C)
    free(imageBuffer);
    DEV_ModuleExit();
    
    return 0;
}
