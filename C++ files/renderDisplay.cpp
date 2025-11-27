/*
 * Bus Tracking Program for Lothian Buses
 * Displays real-time bus times for Heriot-Watt University
 */


extern "C" {
    #include "../C/lib/Config/DEV_Config.h"
    #include "../C/lib/lcd/st7796.h"
    #include "../C/lib/GUI/GUI_Paint.h"
    #include "../C/lib/Fonts/fonts.h"
    #include "../C/lib/lcd/ft6336u.h"
}

#include <iostream>
#include <string>      
// For storing multiple bus stops/services  
#include <vector>  
// For std::sort     
#include <algorithm>    
#include <iomanip>
// For malloc/free
#include <cstdlib> 
// For uint32_t     
#include <cstdint>
 // For time functions      
#include <ctime> 
// For HTTP API calls (install libcurl later)      
#include <curl/curl.h>   
// For JSON parsing
#include <nlohmann/json.hpp>   

//making it easier to use nlohmann json library
using json = nlohmann::json;

// Use shared headers for stop info and lookup
#include "busStopInfo.h"
#include "openmappull.h"    // for GroupedStops, getNearestStopsGrouped
#include "config.h"         // access to CONFIG_ADDRESS(), CITY_CENTER_LAT/LON if needed
#include "busColours.h"     // for bus route color mappings

// Include the public header that declares the display API and shared globals
#include "renderDisplay.h"

// Define the shared UI globals (declared in renderDisplay.h)
DirectionChoice g_dirChoice = DirectionChoice::None;
GroupedStops g_groupedStops;
std::vector<BusStopInfo> g_dropdownStops;
bool g_showDirectionModal = false;
std::string g_selectedStopID;


// Forward declaration of st7796 functions from C library
extern "C" {
    void st7796_set_windows(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end);
}

// Helper function to convert hex color string to RGB565 format (UWORD)
UWORD hexToRGB565(const std::string& hexColor) {
    // Remove '#' if present
    std::string hex = hexColor;
    if (!hex.empty() && hex[0] == '#') {
        hex = hex.substr(1);
    }
    
    // Parse hex string to RGB components
    if (hex.length() == 6) {
        try {
            unsigned int r = std::stoi(hex.substr(0, 2), nullptr, 16);
            unsigned int g = std::stoi(hex.substr(2, 2), nullptr, 16);
            unsigned int b = std::stoi(hex.substr(4, 2), nullptr, 16);
            
            // Convert 8-bit RGB to 5-6-5 format
            // RGB565: RRRRR GGGGGG BBBBB
            UWORD rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            return rgb565;
        } catch (...) {
            return WHITE; // Fallback to white on error
        }
    }
    return WHITE; // Default fallback
}

// Helper function to get color for a bus route
UWORD getRouteColor(const std::string& routeName) {
    auto it = busLineColours.find(routeName);
    if (it != busLineColours.end()) {
        return hexToRGB565(it->second);
    }
    return WHITE; // Default color if route not found
}

// Helper function to estimate text width (approximate)
int estimateTextWidth(const std::string& text, const sFONT* font) {
    return text.length() * font->Width;
}

// Helper function to truncate text to fit within maxWidth
std::string truncateText(const std::string& text, int maxWidth, const sFONT* font) {
    int charWidth = font->Width;
    int maxChars = maxWidth / charWidth;
    
    if (text.length() <= static_cast<size_t>(maxChars)) {
        return text;
    }
    
    // Reserve 3 chars for "..."
    if (maxChars > 3) {
        return text.substr(0, maxChars - 3) + "...";
    }
    return text.substr(0, maxChars);
}

// Helper function to split text into multiple lines if needed
std::vector<std::string> splitTextToFit(const std::string& text, int maxWidth, const sFONT* font) {
    std::vector<std::string> lines;
    int charWidth = font->Width;
    int maxCharsPerLine = maxWidth / charWidth;
    
    if (text.length() <= static_cast<size_t>(maxCharsPerLine)) {
        lines.push_back(text);
        return lines;
    }
    
    // Try to split at spaces
    size_t pos = 0;
    while (pos < text.length()) {
        size_t remaining = text.length() - pos;
        if (remaining <= static_cast<size_t>(maxCharsPerLine)) {
            lines.push_back(text.substr(pos));
            break;
        }
        
        // Find last space before maxCharsPerLine
        size_t endPos = pos + maxCharsPerLine;
        size_t spacePos = text.rfind(' ', endPos);
        
        if (spacePos != std::string::npos && spacePos > pos) {
            lines.push_back(text.substr(pos, spacePos - pos));
            pos = spacePos + 1; // Skip the space
        } else {
            // No space found, force break
            lines.push_back(text.substr(pos, maxCharsPerLine));
            pos += maxCharsPerLine;
        }
        
        // Limit to 2 lines for destinations
        if (lines.size() >= 2) {
            break;
        }
    }
    
    return lines;
}

// Helper function to display the Paint buffer on the LCD
void displayBuffer(UWORD *image) {
    // Set the display window to full screen
    st7796_set_windows(0, 0, ST7796_WIDTH - 1, ST7796_HEIGHT - 1);
    
    // Send data line by line to the display, set to datamode
    LCD_DC_1;  
    
    for(uint16_t y = 0; y < ST7796_HEIGHT; y++) {
        // Send one row at a time
        DEV_SPI_Write_nByte((uint8_t*)&image[y * ST7796_WIDTH], ST7796_WIDTH * 2);
    }
}

void drawBusInfo(const std::string& routeNum, const std::string& destination, 
                 const std::string& timeStr, bool isLive, int yPos) {
    
    // was used to print to terminal for debug
    // std::cout << "Drawing at yPos=" << yPos 
    //           << " route=" << routeNum 
    //           << " dest=" << destination 
    //           << " time=" << timeStr << std::endl;
    
    // Check if position is valid on display
    // if(yPos > 430) {  // Leave room for text height
    //     std::cout << "WARNING: yPos too large, skipping" << std::endl;
    //     return;
    // }
    
    // Truncate destination name to fit screen
    std::string shortDest = destination;
    if(shortDest.length() > 20) {  // Adjust based on font size
        shortDest = shortDest.substr(0, 17) + "...";
    }
}

// Note: renderDisplay.cpp no longer contains `main()`; `busTrack.cpp` is the program entry.
// Implement modal and dropdown draw functions here so the drawing logic is centralized

void drawDirectionChoiceModal() {
    // simple centered panel
    int x0 = 40, x1 = 280;
    // Use fixed Y positions so touch hitboxes are unambiguous
    int btn_w = x1 - x0 - 20;
    int btn_x = x0 + 10;
    const int into_y = 60;   // top button Y (user requested)
    const int out_y = 300;   // bottom button Y (user requested)
    const int btn_h = 36;

    // Draw outlines for clarity
    Paint_DrawRectangle(btn_x, into_y, btn_x + btn_w, into_y + btn_h, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawRectangle(btn_x, out_y, btn_x + btn_w, out_y + btn_h, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);

    // Labels
    Paint_DrawString_EN(btn_x + 12, into_y + 8, "Into city", &Font16, FONT_BACKGROUND, FONT_FOREGROUND);
    Paint_DrawString_EN(btn_x + 12, out_y + 8, "Out of city", &Font16, FONT_BACKGROUND, FONT_FOREGROUND);
    // optional: small subtitle
    Paint_DrawString_EN(x0 + 10, into_y - 18, "Choose direction", &Font12, FONT_BACKGROUND, FONT_FOREGROUND);
}


void drawDropdownFromGrouped() {
    // basic top-down list drawing; reuse your existing row drawing code
    int rowY = 20;
    int rowH = 34;
    for (size_t i = 0; i < g_dropdownStops.size(); ++i) {
        int y = rowY + i * rowH;
        // background highlight for selected row could be added
        // Paint_DrawRectangle(10, y, 300, y + rowH - 4, DRAW_FILL_EMPTY, LINE_STYLE_SOLID, DOT_PIXEL_2X2);
        std::string name = g_dropdownStops[i].stopName;
        // trim if too long - reuse your existing text truncation helper
        Paint_DrawString_EN(16, y + 6, name.c_str(), &Font16, FONT_BACKGROUND, FONT_FOREGROUND);
    }
}

void drawMainScreen(UWORD *image, const BusStopInfo &currentStop, const json &busData) {
    // Clear Paint buffer (not the physical screen)
    Paint_Clear(GRAY);

    // Display constants
    const int SCREEN_WIDTH = ST7796_WIDTH;   // 320
    const int SCREEN_HEIGHT = ST7796_HEIGHT;  // 480
    const int MARGIN = 5;
    const int HEADER_START_Y = 20;
    
    // get current time
    time_t now = time(nullptr);
    struct tm* currentTime = localtime(&now);
    int currentMinutes = currentTime->tm_hour * 60 + currentTime->tm_min;

    int yPos = HEADER_START_Y;

    // Draw header arrow (centered)
    int arrowCenterX = SCREEN_WIDTH / 2;
    Paint_DrawLine(arrowCenterX - 15, yPos, arrowCenterX, yPos + 15, WHITE, DOT_PIXEL_4X4, LINE_STYLE_SOLID);
    Paint_DrawLine(arrowCenterX, yPos + 15, arrowCenterX + 15, yPos, WHITE, DOT_PIXEL_4X4, LINE_STYLE_SOLID);

    yPos += 30;
    Paint_DrawString_EN(MARGIN, yPos, "------------------", &Font24, WHITE, WHITE);

    // Draw header - stop name (truncated to fit)
    yPos += 20;
    std::string displayStopName = truncateText(currentStop.stopName, SCREEN_WIDTH - 60, &Font20);
    Paint_DrawString_EN(30, yPos, displayStopName.c_str(), &Font20, BLACK, WHITE);
    
    // Draw direction label based on which array the stop was selected from
    if (g_dirChoice == DirectionChoice::IntoCity) {
        Paint_DrawString_EN(120, yPos + 20, "Into City", &Font12, GRAY, BLACK);
    } else if (g_dirChoice == DirectionChoice::OutOfCity) {
        Paint_DrawString_EN(120, yPos + 20, "Out of City", &Font12, GRAY, BLACK);
    }

    yPos += 40;
    Paint_DrawString_EN(30, yPos, "Current Time: ", &Font20, WHITE, WHITE);

    // Display current time
    char timeString[10];
    snprintf(timeString, sizeof(timeString), "%02d:%02d", currentTime->tm_hour, currentTime->tm_min);
    Paint_DrawString_EN(220, yPos, timeString, &Font20, WHITE, WHITE);

    // Collect departures into a vector
    struct BusDeparture {
        std::string routeName;
        std::string destination;
        std::string time;
        int minutesSinceMidnight;
    };
    std::vector<BusDeparture> allDepartures;

    for(const auto& route : busData) {
        std::string routeName = route.value("routeName", "");
        if(!route.contains("departures") || !route["departures"].is_array()) continue;
        for(const auto& dep : route["departures"]) {
            BusDeparture busDep;
            busDep.routeName = routeName;
            busDep.destination = dep.value("destination", "Unknown");
            std::string localTime = dep.value("ineoUTCTime", "");
            if(localTime.length() >= 5) {
                busDep.time = localTime.substr(0,5);
                try {
                    int hour = std::stoi(localTime.substr(0,2));
                    int minute = std::stoi(localTime.substr(3,2));
                    busDep.minutesSinceMidnight = hour * 60 + minute;
                    if(busDep.minutesSinceMidnight < 360 && currentMinutes > 1080) busDep.minutesSinceMidnight += 1440;
                } catch(...) { busDep.minutesSinceMidnight = 9999; }
            } else { busDep.time = "??:??"; busDep.minutesSinceMidnight = 9999; }
            allDepartures.push_back(busDep);
        }
    }

    std::sort(allDepartures.begin(), allDepartures.end(), [](const BusDeparture& a, const BusDeparture& b){
        return a.minutesSinceMidnight < b.minutesSinceMidnight;
    });

    // Debug output
    std::cout << "Total departures collected: " << allDepartures.size() << std::endl;

    // Calculate available space and max buses that can fit
    yPos += 60;
    int busListStartY = yPos;
    int availableHeight = SCREEN_HEIGHT - busListStartY - 20; // Leave 20px bottom margin
    const int BUS_ROW_HEIGHT = 70; // Height per bus entry (route + dest + divider)
    int maxBuses = std::max(1, availableHeight / BUS_ROW_HEIGHT);
    maxBuses = std::min(maxBuses, static_cast<int>(allDepartures.size()));

    // std::cout << "busListStartY: " << busListStartY << ", availableHeight: " << availableHeight 
    //           << ", maxBuses: " << maxBuses << std::endl;

    // If no buses available, display message
    if (allDepartures.empty()) {
        Paint_DrawString_EN(40, SCREEN_HEIGHT / 2 - 40, "No buses", &Font24, RED, GRAY);
        Paint_DrawString_EN(40, SCREEN_HEIGHT / 2 - 10, "available", &Font24, RED, GRAY);
        displayBuffer(image);
        return;
    }

    int busCount = 0;

    for(const auto& bus : allDepartures) {
        if(busCount >= maxBuses) break;
        
        // Layout columns: [Route(60px)] [Destination(120px)] [Time(140px)]
        const int ROUTE_X = 10;
        const int ROUTE_WIDTH = 60;
        const int DEST_X = ROUTE_X + ROUTE_WIDTH + 5;
        const int DEST_WIDTH = 120;
        const int TIME_X = DEST_X + DEST_WIDTH + 10;
        
        std::string dest = bus.destination;
        std::string shortRoute = bus.routeName;
        if(shortRoute.length() > 3) shortRoute = shortRoute.substr(0,3);

        // Draw route number with color
        UWORD routeColor = getRouteColor(bus.routeName);
        Paint_DrawString_EN(ROUTE_X, yPos, shortRoute.c_str(), &Font48, routeColor, WHITE);

        // Split destination text intelligently to fit in available space
        std::vector<std::string> destLines = splitTextToFit(dest, DEST_WIDTH, &Font20);
        
        // Draw destination (1 or 2 lines)
        if (destLines.size() == 1) {
            // Single line - center vertically
            Paint_DrawString_EN(DEST_X, yPos + 12, destLines[0].c_str(), &Font20, WHITE, WHITE);
        } else if (destLines.size() >= 2) {
            // Two lines
            Paint_DrawString_EN(DEST_X, yPos + 4, destLines[0].c_str(), &Font20, WHITE, WHITE);
            Paint_DrawString_EN(DEST_X, yPos + 24, destLines[1].c_str(), &Font20, WHITE, WHITE);
        }

        // Draw time or "DUE"
        int minutesUntilBus = bus.minutesSinceMidnight - currentMinutes;
        if(minutesUntilBus < 0) minutesUntilBus += 1440;

        if(minutesUntilBus <= 2 && minutesUntilBus >= -2) {
            Paint_DrawString_EN(TIME_X, yPos + 4, "DUE", &Font48, RED, WHITE);
        } else {
            // Display minutes until arrival - Font48 for number, Font24 for "min"
            char minutesNumStr[5];
            snprintf(minutesNumStr, sizeof(minutesNumStr), "%d", minutesUntilBus);
            
            // Draw the number in large font
            Paint_DrawString_EN(TIME_X, yPos + 10, minutesNumStr, &Font48, WHITE, WHITE);
            
            // Calculate position for "min" text (after the number)
            int numWidth = strlen(minutesNumStr) * Font48.Width;
            Paint_DrawString_EN(TIME_X + numWidth + 2, yPos + 35, "min", &Font24, WHITE, WHITE);
        }

        yPos += 50;
        Paint_DrawString_EN(MARGIN, yPos, "------------------", &Font24, WHITE, WHITE);
        yPos += 20;
        busCount++;
        
        // std::cout << "Drew bus #" << busCount << ": " << bus.routeName 
        //           << " to " << bus.destination << " at yPos=" << (yPos - 70) << std::endl;
    }

    // std::cout << "Finished drawing " << busCount << " buses" << std::endl;

    // Push the buffer to the physical display
    displayBuffer(image);
}
