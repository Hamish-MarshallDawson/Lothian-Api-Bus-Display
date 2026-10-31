/*
 * Drawing for the bus display. Everything draws into a RGB565 Paint buffer.
 */

extern "C" {
    #include "../C/lib/GUI/GUI_Paint.h"
    #include "../C/lib/Fonts/fonts.h"
#ifdef BUSTRACKER_HW
    #include "../C/lib/Config/DEV_Config.h"
    #include "../C/lib/lcd/st7796.h"
    void st7796_set_windows(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end);
#endif
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include "busColours.h"     // for bus route color mappings
#include "departures.h"
#include "renderDisplay.h"

// Define the shared UI globals (declared in renderDisplay.h)
DirectionChoice g_dirChoice = DirectionChoice::None;
GroupedStops g_groupedStops;
std::vector<BusStopInfo> g_dropdownStops;
bool g_showDirectionModal = false;
std::string g_selectedStopID;

// Helper function to convert hex color string to RGB565 format (UWORD)
UWORD hexToRGB565(const std::string& hexColor) {
    std::string hex = hexColor;
    if (!hex.empty() && hex[0] == '#') {
        hex = hex.substr(1);
    }
    if (hex.length() == 6) {
        try {
            unsigned int r = std::stoi(hex.substr(0, 2), nullptr, 16);
            unsigned int g = std::stoi(hex.substr(2, 2), nullptr, 16);
            unsigned int b = std::stoi(hex.substr(4, 2), nullptr, 16);
            // RGB565: RRRRR GGGGGG BBBBB
            return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        } catch (...) {
            return WHITE;
        }
    }
    return WHITE;
}

// Helper function to get color for a bus route
UWORD getRouteColor(const std::string& routeName) {
    auto it = busLineColours.find(routeName);
    if (it != busLineColours.end()) {
        return hexToRGB565(it->second);
    }
    return WHITE;
}

// Helper function to truncate text to fit within maxWidth
std::string truncateText(const std::string& text, int maxWidth, const sFONT* font) {
    int maxChars = maxWidth / font->Width;
    if (text.length() <= static_cast<size_t>(maxChars)) {
        return text;
    }
    if (maxChars > 3) {
        return text.substr(0, maxChars - 3) + "...";
    }
    return text.substr(0, maxChars);
}

// Helper function to split text into at most two lines that fit maxWidth
std::vector<std::string> splitTextToFit(const std::string& text, int maxWidth, const sFONT* font) {
    std::vector<std::string> lines;
    int maxCharsPerLine = maxWidth / font->Width;

    if (text.length() <= static_cast<size_t>(maxCharsPerLine)) {
        lines.push_back(text);
        return lines;
    }

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
            pos = spacePos + 1;
        } else {
            // No space found, force break
            lines.push_back(text.substr(pos, maxCharsPerLine));
            pos += maxCharsPerLine;
        }

        if (lines.size() >= 2) {
            break;
        }
    }

    return lines;
}

#ifdef BUSTRACKER_HW
// Push the Paint buffer to the LCD, one row at a time
void displayBuffer(UWORD *image) {
    st7796_set_windows(0, 0, ST7796_WIDTH - 1, ST7796_HEIGHT - 1);
    LCD_DC_1;
    for(uint16_t y = 0; y < ST7796_HEIGHT; y++) {
        DEV_SPI_Write_nByte((uint8_t*)&image[y * ST7796_WIDTH], ST7796_WIDTH * 2);
    }
}
#else
// Snapshot-only build has no LCD. busTrack refuses to run the display loop here.
void displayBuffer(UWORD *) {}
#endif

void drawDirectionChoiceModal() {
    // simple centered panel
    int x0 = 40, x1 = 280;
    // Use fixed Y positions so touch hitboxes are unambiguous
    int btn_w = x1 - x0 - 20;
    int btn_x = x0 + 10;
    const int into_y = 60;   // top button Y
    const int out_y = 300;   // bottom button Y
    const int btn_h = 36;

    Paint_DrawRectangle(btn_x, into_y, btn_x + btn_w, into_y + btn_h, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    Paint_DrawRectangle(btn_x, out_y, btn_x + btn_w, out_y + btn_h, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);

    Paint_DrawString_EN(btn_x + 12, into_y + 8, "Into city", &Font16, FONT_BACKGROUND, FONT_FOREGROUND);
    Paint_DrawString_EN(btn_x + 12, out_y + 8, "Out of city", &Font16, FONT_BACKGROUND, FONT_FOREGROUND);
    Paint_DrawString_EN(x0 + 10, into_y - 18, "Choose direction", &Font12, FONT_BACKGROUND, FONT_FOREGROUND);
}

// Text for the empty-list case, so an error is never shown as "no buses"
static void drawEmptyState(const StopBoard &board) {
    const int cy = ST7796_HEIGHT / 2 - 40;
    switch (board.status) {
        case BoardStatus::Loading:
            Paint_DrawString_EN(40, cy, "Loading...", &Font24, WHITE, GRAY);
            break;
        case BoardStatus::NetworkError:
            Paint_DrawString_EN(40, cy, "No data", &Font24, RED, GRAY);
            Paint_DrawString_EN(40, cy + 30, "network error", &Font20, RED, GRAY);
            break;
        case BoardStatus::KeyRejected:
            Paint_DrawString_EN(40, cy, "Key rejected", &Font24, RED, GRAY);
            break;
        case BoardStatus::RateLimited:
            Paint_DrawString_EN(40, cy, "Rate limited", &Font24, RED, GRAY);
            Paint_DrawString_EN(40, cy + 30, "try again soon", &Font20, RED, GRAY);
            break;
        default:  // Ok with nothing to list, or NoDepartures
            Paint_DrawString_EN(40, cy, "No buses", &Font24, RED, GRAY);
            Paint_DrawString_EN(40, cy + 30, "available", &Font24, RED, GRAY);
            break;
    }
}

void drawMainScreen(UWORD *image, const StopBoard &board) {
    (void)image;  // the buffer is the Paint default one; kept for the call shape
    Paint_Clear(GRAY);

    // Display constants
    const int SCREEN_WIDTH = ST7796_WIDTH;   // 320
    const int SCREEN_HEIGHT = ST7796_HEIGHT;  // 480
    const int MARGIN = 5;
    const int HEADER_START_Y = 20;

    // Clock comes from the board's fetch time
    time_t now = board.fetchedAt ? board.fetchedAt : time(nullptr);
    struct tm* currentTime = localtime(&now);

    int yPos = HEADER_START_Y;

    // Draw header arrow (centered)
    int arrowCenterX = SCREEN_WIDTH / 2;
    Paint_DrawLine(arrowCenterX - 15, yPos, arrowCenterX, yPos + 15, WHITE, DOT_PIXEL_4X4, LINE_STYLE_SOLID);
    Paint_DrawLine(arrowCenterX, yPos + 15, arrowCenterX + 15, yPos, WHITE, DOT_PIXEL_4X4, LINE_STYLE_SOLID);

    yPos += 30;
    Paint_DrawString_EN(MARGIN, yPos, "------------------", &Font24, WHITE, WHITE);

    // Stop name (truncated to fit)
    yPos += 20;
    std::string displayStopName = truncateText(board.stopName, SCREEN_WIDTH - 60, &Font20);
    Paint_DrawString_EN(30, yPos, displayStopName.c_str(), &Font20, BLACK, WHITE);

    if (g_dirChoice == DirectionChoice::IntoCity) {
        Paint_DrawString_EN(120, yPos + 20, "Into City", &Font12, GRAY, BLACK);
    } else if (g_dirChoice == DirectionChoice::OutOfCity) {
        Paint_DrawString_EN(120, yPos + 20, "Out of City", &Font12, GRAY, BLACK);
    }

    yPos += 40;
    Paint_DrawString_EN(30, yPos, "Current Time: ", &Font20, WHITE, WHITE);
    char timeString[10];
    snprintf(timeString, sizeof(timeString), "%02d:%02d", currentTime->tm_hour, currentTime->tm_min);
    Paint_DrawString_EN(220, yPos, timeString, &Font20, WHITE, WHITE);

    // Cancelled services are not shown on the main screen
    std::vector<const Departure*> shown;
    for (const auto &d : board.departures) {
        if (!d.cancelled && d.hasMinutes) shown.push_back(&d);
    }

    // Calculate available space and max buses that can fit
    yPos += 60;
    int busListStartY = yPos;
    int availableHeight = SCREEN_HEIGHT - busListStartY - 20; // Leave 20px bottom margin
    const int BUS_ROW_HEIGHT = 70;
    int maxBuses = std::max(1, availableHeight / BUS_ROW_HEIGHT);
    maxBuses = std::min(maxBuses, static_cast<int>(shown.size()));

    if (shown.empty()) {
        drawEmptyState(board);
        return;
    }

    int busCount = 0;
    for (const Departure *bus : shown) {
        if (busCount >= maxBuses) break;

        // Layout columns: [Route(60px)] [Destination(120px)] [Time(140px)]
        const int ROUTE_X = 10;
        const int ROUTE_WIDTH = 60;
        const int DEST_X = ROUTE_X + ROUTE_WIDTH + 5;
        const int DEST_WIDTH = 120;
        const int TIME_X = DEST_X + DEST_WIDTH + 10;

        std::string shortRoute = bus->service;
        if (shortRoute.length() > 3) shortRoute = shortRoute.substr(0, 3);

        UWORD routeColor = getRouteColor(bus->service);
        Paint_DrawString_EN(ROUTE_X, yPos, shortRoute.c_str(), &Font48, routeColor, WHITE);

        std::vector<std::string> destLines = splitTextToFit(bus->destination, DEST_WIDTH, &Font20);
        if (destLines.size() == 1) {
            Paint_DrawString_EN(DEST_X, yPos + 12, destLines[0].c_str(), &Font20, WHITE, WHITE);
        } else if (destLines.size() >= 2) {
            Paint_DrawString_EN(DEST_X, yPos + 4, destLines[0].c_str(), &Font20, WHITE, WHITE);
            Paint_DrawString_EN(DEST_X, yPos + 24, destLines[1].c_str(), &Font20, WHITE, WHITE);
        }

        int minutesUntilBus = std::max(0, bus->minutes);
        if (bus->minutes <= 2 && bus->minutes >= -2) {
            Paint_DrawString_EN(TIME_X, yPos + 4, "DUE", &Font48, RED, WHITE);
        } else {
            char minutesNumStr[12];
            snprintf(minutesNumStr, sizeof(minutesNumStr), "%d", minutesUntilBus);
            Paint_DrawString_EN(TIME_X, yPos + 10, minutesNumStr, &Font48, WHITE, WHITE);
            int numWidth = strlen(minutesNumStr) * Font48.Width;
            Paint_DrawString_EN(TIME_X + numWidth + 2, yPos + 35, "min", &Font24, WHITE, WHITE);
        }

        yPos += 50;
        Paint_DrawString_EN(MARGIN, yPos, "------------------", &Font24, WHITE, WHITE);
        yPos += 20;
        busCount++;
    }
    // The caller pushes the buffer (displayBuffer) or writes it to a PNG.
}
