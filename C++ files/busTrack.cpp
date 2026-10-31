/*
 * Bus Tracking Program for Lothian Buses
 *
 * Modes:
 *   busTrack                                  run the display (needs the BUSTRACKER_HW build)
 *   busTrack --snapshot out.png [--fixture f.json] [--now YYYY-MM-DDTHH:MM:SS] [--screen main]
 *                                             render one frame to a PNG; no GPIO/SPI/touch
 *   busTrack --fetch <atcocode>               print one stop's board from the live API
 *   busTrack --fixture f.json [--now ...]     print what a fixture parses to
 *
 * The display build is used only when wiringPi is installed (see Makefile).
 */

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "busStopInfo.h"
#include "departures.h"
#include "openmappull.h"    // getNearestStops, getNearestStopsGrouped
#include "png_write.h"
#include "renderDisplay.h"  // drawMainScreen, displayBuffer, shared UI state
#include "settings.h"

extern "C" {
    #include "../C/lib/GUI/GUI_Paint.h"
}

#ifdef BUSTRACKER_HW
extern "C" {
    #include "../C/lib/Config/DEV_Config.h"
    #include "../C/lib/lcd/st7796.h"
    #include "../C/lib/lcd/ft6336u.h"
}
#include <wiringPi.h>

const int BACKLIGHT_PIN = 18; // BCM 18 from ComponentsUsed.txt

void backlight_init() {
    wiringPiSetupGpio();           // use BCM numbering
    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, HIGH);
}

void backlight_on() { digitalWrite(BACKLIGHT_PIN, HIGH); }
void backlight_off() { digitalWrite(BACKLIGHT_PIN, LOW); }
#endif

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Command line

struct Options {
    std::string snapshotPath;   // --snapshot
    std::string fixturePath;    // --fixture
    std::string screen = "main";// --screen
    std::string fetchStop;      // --fetch
    time_t now = 0;             // --now (0 = real clock)
};

static bool parseOptions(int argc, char **argv, Options &o) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        bool hasNext = i + 1 < argc;
        if (arg == "--snapshot" && hasNext) o.snapshotPath = argv[++i];
        else if (arg == "--fixture" && hasNext) o.fixturePath = argv[++i];
        else if (arg == "--screen" && hasNext) o.screen = argv[++i];
        else if (arg == "--fetch" && hasNext) o.fetchStop = argv[++i];
        else if (arg == "--now" && hasNext) {
            std::tm tm{};
            std::istringstream in(argv[++i]);
            in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
            if (in.fail()) { std::cerr << "--now wants YYYY-MM-DDTHH:MM:SS\n"; return false; }
            tm.tm_isdst = -1;
            o.now = mktime(&tm);
        } else {
            std::cerr << "Unknown or incomplete option: " << arg << "\n";
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Fixtures: a raw StopEvents response (array), or an error state to render:
//   {"status": "key_rejected", "name": "Stop name", "atcocode": "..."}
// status: ok | no_departures | network_error | key_rejected | rate_limited | loading

static bool loadFixtureBoard(const std::string &path, time_t now, StopBoard &out) {
    std::ifstream f(path);
    if (!f) { std::cerr << "Cannot open fixture " << path << "\n"; return false; }
    std::stringstream ss;
    ss << f.rdbuf();

    json doc;
    try {
        doc = json::parse(ss.str());
    } catch (const json::exception &e) {
        std::cerr << "Fixture " << path << " is not JSON: " << e.what() << "\n";
        return false;
    }

    if (doc.is_object() && doc.contains("status")) {
        const std::string s = doc.value("status", "");
        out = StopBoard();
        out.stopName = doc.value("name", "");
        out.atcocode = doc.value("atcocode", "");
        out.fetchedAt = now;
        if      (s == "loading")       out.status = BoardStatus::Loading;
        else if (s == "ok")            out.status = BoardStatus::Ok;
        else if (s == "no_departures") out.status = BoardStatus::NoDepartures;
        else if (s == "network_error") out.status = BoardStatus::NetworkError;
        else if (s == "key_rejected")  out.status = BoardStatus::KeyRejected;
        else if (s == "rate_limited")  out.status = BoardStatus::RateLimited;
        else {
            std::cerr << "Fixture " << path << ": unknown status '" << s << "'\n";
            return false;
        }
        return true;
    }
    if (doc.is_array() && !doc.empty() && doc[0].is_object()) {
        out = boardFromStopEvents(doc[0], now);
        return true;
    }
    std::cerr << "Fixture " << path << " is neither a StopEvents array nor a status object\n";
    return false;
}

// ---------------------------------------------------------------------------
// Snapshot mode

static int runSnapshot(const Options &o) {
    const time_t now = o.now ? o.now : time(nullptr);

    StopBoard board;
    if (!o.fixturePath.empty()) {
        if (!loadFixtureBoard(o.fixturePath, now, board)) return 2;
    } else {
        std::vector<BusStopInfo> nearest;
        if (!getNearestStops(CONFIG_ADDRESS(), 1, nearest) || nearest.empty()) {
            std::cerr << "Snapshot: no fixture and no nearest stop (is BUS_ADDRESS set?)\n";
            return 3;
        }
        board = fetchStopBoard(nearest.front().stopID, now);
        if (board.stopName.empty()) board.stopName = nearest.front().stopName;
    }

    if (o.screen != "main") {
        std::cerr << "Unknown --screen '" << o.screen << "'. Known screens: main\n";
        return 2;
    }

    // Same Paint setup as the display loop, so the PNG is the buffer the LCD gets
    const int w = ST7796_WIDTH, h = ST7796_HEIGHT;
    UWORD *buffer = static_cast<UWORD *>(malloc(sizeof(UWORD) * w * h));
    if (!buffer) { std::cerr << "Out of memory\n"; return 1; }
    Paint_NewImage(buffer, w, h, 0, BLACK, 16);
    Paint_SetMirroring(MIRROR_HORIZONTAL);
    Paint_Clear(BLACK);

    drawMainScreen(buffer, board);

    bool ok = writePngRGB565(o.snapshotPath, buffer, w, h);
    free(buffer);
    if (!ok) { std::cerr << "Could not write " << o.snapshotPath << "\n"; return 1; }

    std::cout << "Wrote " << o.snapshotPath << " (" << board.stopName
              << ", " << board.departures.size() << " departures, status "
              << boardStatusName(board.status) << ")\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Fetch mode: one stop, printed as text

static int printBoard(const StopBoard &board) {
    std::cout << "Stop: " << board.stopName << " [" << board.atcocode << "] "
              << board.indicator << "\n"
              << "Status: " << boardStatusName(board.status);
    if (!board.statusDetail.empty()) std::cout << " (" << board.statusDetail << ")";
    std::cout << "\n";
    for (const auto &d : board.departures) {
        std::cout << "  " << d.service << " to " << d.destination
                  << "  sched " << d.scheduled
                  << "  in " << d.minutes << " min"
                  << (d.live ? "  live" : "")
                  << (d.cancelled ? "  CANCELLED" : "")
                  << (d.delayed ? "  delayed" : "") << "\n";
    }
    for (const auto &n : board.notices) std::cout << "  notice: " << n << "\n";
    return board.status == BoardStatus::Ok || board.status == BoardStatus::NoDepartures ? 0 : 1;
}

static int runFetch(const Options &o) {
    return printBoard(fetchStopBoard(o.fetchStop, time(nullptr)));
}

// --fixture without --snapshot: print what the fixture parses to
static int runFixtureDump(const Options &o) {
    StopBoard board;
    if (!loadFixtureBoard(o.fixturePath, o.now ? o.now : time(nullptr), board)) return 2;
    return printBoard(board);
}

// ---------------------------------------------------------------------------
// Display mode (needs wiringPi: see the Makefile)

#ifdef BUSTRACKER_HW

static int runDisplay() {
    std::cout << "Lothian Bus Display Starting" << std::endl;

    std::string address = CONFIG_ADDRESS();
    int wanted = CONFIG_NUM_STOPS();
    if (address.empty()) {
        std::cerr << "BUS_ADDRESS is not set; nothing to show\n";
        return 1;
    }

    std::vector<BusStopInfo> busStops;
    if (!getNearestStops(address, wanted, busStops) || busStops.empty()) {
        std::cerr << "Could not find stops near " << address << "\n";
        return 1;
    }

    // Index of current stop shown on screen: nearest stop at startup
    int currentBusStopIndex = 0;
    {
        std::vector<BusStopInfo> nearest;
        if (getNearestStops(address, 1, nearest) && !nearest.empty()) {
            g_selectedStopID = nearest.front().stopID;
            for (size_t i = 0; i < busStops.size(); ++i) {
                if (busStops[i].stopID == g_selectedStopID) {
                    currentBusStopIndex = i;
                    break;
                }
            }
        }
    }

    // Start display
    if(DEV_ModuleInit() != 0) {
        std::cerr << "Failed to initialize display" << std::endl;
        return 1;
    }
    backlight_init();
    st7796_init();
    st7796_clear(BLACK);
    ft6336u_init();

    // Prefetch grouped stops so the first dropdown interaction is fast
    {
        int perGroup = CONFIG_NUM_STOPS();
        GroupedStops pre;
        std::cerr << "Prefetching grouped stops (perGroup=" << perGroup << ")..." << std::endl;
        if (getNearestStopsGrouped(address, perGroup, pre)) {
            g_groupedStops = pre;
        } else {
            std::cerr << "Prefetch failed or network unavailable; will fetch on demand." << std::endl;
        }
    }

    touch_data_t touch_data;

    // Paint buffer for the whole screen
    UWORD *BlackImage;
    uint32_t Imagesize = ((uint32_t)ST7796_WIDTH) * ((uint32_t)ST7796_HEIGHT);
    if((BlackImage = (UWORD *)malloc(Imagesize * sizeof(UWORD))) == NULL) {
        std::cerr << "Failed to allocate memory for image buffer" << std::endl;
        DEV_ModuleExit();
        return 1;
    }
    Paint_NewImage(BlackImage, ST7796_WIDTH, ST7796_HEIGHT, 0, BLACK, 16);
    Paint_SetMirroring(MIRROR_HORIZONTAL);
    Paint_Clear(BLACK);

    // Welcome message
    Paint_DrawString_EN(70, 200, "Bus Tracker", &Font24, WHITE, WHITE);
    Paint_DrawString_EN(70, 230, "Starting...", &Font20, WHITE, WHITE);
    displayBuffer(BlackImage);
    DEV_Delay_ms(1000);

    //decides whether to show dropdown menu
    bool showDropDownMenu = false;
    int autoTurnOff = 0;  // Track idle time across loop iterations

    // Main loop
    while(true) {

        if(!showDropDownMenu) {
            const BusStopInfo &stop = busStops[currentBusStopIndex];
            std::cout << "\n=== Fetching bus data for stop: " << stop.stopName
                      << " (ID: " << stop.stopID << ") ===" << std::endl;

            StopBoard board = fetchStopBoard(stop.stopID, time(nullptr));
            if (board.stopName.empty()) board.stopName = stop.stopName;
            if (board.status != BoardStatus::Ok && board.status != BoardStatus::NoDepartures) {
                std::cerr << "Board status: " << boardStatusName(board.status)
                          << " " << board.statusDetail << std::endl;
            }
            drawMainScreen(BlackImage, board);
            displayBuffer(BlackImage);
        }

        // If menu should be shown, draw it on top
        if(showDropDownMenu) {
            Paint_Clear(GRAY);

            // Compute how many rows fit on screen (start Y=50, row height=50)
            const int rowY0 = 50;
            const int rowH = 50;
            size_t maxRows = busStops.size();
            if (ST7796_HEIGHT > rowY0) {
                size_t fit = (ST7796_HEIGHT - rowY0) / rowH;
                if (fit < maxRows) maxRows = fit;
            } else {
                maxRows = 0;
            }

            if (g_showDirectionModal) {
                drawDirectionChoiceModal();
                displayBuffer(BlackImage);
            } else {
                bool usingGrouped = !g_dropdownStops.empty();
                size_t sourceSize = usingGrouped ? g_dropdownStops.size() : busStops.size();
                size_t rowsToDraw = std::min(maxRows, sourceSize);

                if (rowsToDraw == 0) {
                    Paint_DrawString_EN(20, rowY0 + 4, "No stops found", &Font16, RED, GRAY);
                } else {
                    for(size_t i = 0; i < rowsToDraw; i++) {
                        int itemY = rowY0 + (i * rowH);
                        const std::string &name = usingGrouped ? g_dropdownStops[i].stopName : busStops[i].stopName;

                        UWORD bgColor = (static_cast<size_t>(currentBusStopIndex) == i && !usingGrouped) ? BLUE : WHITE;
                        Paint_DrawString_EN(20, itemY + 12, name.c_str(), &Font16, BLACK, bgColor);
                    }
                }
                displayBuffer(BlackImage);
            }
        }

        // Wait until the next minute boundary, checking for touch input
        time_t now = time(nullptr);
        std::tm* timeinfo = localtime(&now);
        int secondsIntoMinute = timeinfo->tm_sec;
        int secondsToWait = (60 - secondsIntoMinute);
        if (secondsToWait <= 0) secondsToWait = 60;
        int iterations = secondsToWait * 10;  // 10 iterations per second (100ms each)
        std::cout << "Waiting " << secondsToWait << " seconds until next refresh" << std::endl;

        for(int i = 0; i < iterations && i < 600; i++) {  // Cap at 60 seconds max

            if (get_touch_data(&touch_data)) {
                int touchX = touch_data.coords[0].x;
                int touchY = touch_data.coords[0].y;

                std::cout << "Touch detected at (" << touchX << ", " << touchY << ")" << std::endl;

                // Tap the top area: open the dropdown + direction modal
                if (touchY < 50) {
                    std::cout << "Dropdown touched" << std::endl;
                    g_showDirectionModal = true;
                    g_dirChoice = DirectionChoice::None;
                    g_groupedStops.intoCity.clear();
                    g_groupedStops.outOfCity.clear();
                    g_dropdownStops.clear();
                    showDropDownMenu = true;
                    autoTurnOff = 0;
                    DEV_Delay_ms(15);
                    break;
                }

                // Direction modal: handle taps on its buttons
                if (g_showDirectionModal) {
                    int x0 = 40, x1 = 280;
                    int btn_x = x0 + 10;
                    int btn_w = x1 - x0 - 20;
                    const int into_y = 60;
                    const int out_y = 300;
                    const int btn_h = 36;

                    if (touchX >= btn_x && touchX <= btn_x + btn_w && touchY >= into_y && touchY <= into_y + btn_h) {
                        g_dirChoice = DirectionChoice::IntoCity;
                    }
                    else if (touchX >= btn_x && touchX <= btn_x + btn_w && touchY >= out_y && touchY <= out_y + btn_h) {
                        g_dirChoice = DirectionChoice::OutOfCity;
                    }

                    if (g_dirChoice != DirectionChoice::None) {
                        int perGroup = CONFIG_NUM_STOPS();
                        bool ok = getNearestStopsGrouped(address, perGroup, g_groupedStops);
                        if (!ok) {
                            std::vector<BusStopInfo> fallback;
                            if (getNearestStops(address, perGroup, fallback)) {
                                g_dropdownStops = fallback;
                            } else {
                                g_dropdownStops.clear();
                            }
                            std::cerr << "getNearestStopsGrouped failed, using fallback size=" << g_dropdownStops.size() << std::endl;
                        } else {
                            if (g_dirChoice == DirectionChoice::IntoCity) g_dropdownStops = g_groupedStops.intoCity;
                            else g_dropdownStops = g_groupedStops.outOfCity;
                        }

                        g_showDirectionModal = false;
                        showDropDownMenu = true;
                    }
                }
                // Dropdown visible: handle row selection
                else if (showDropDownMenu && touchY >= 50) {
                    int rowY0 = 50;
                    int rowH = 50;
                    int selectedIndex = (touchY - rowY0) / rowH;

                    bool usingGrouped = !g_dropdownStops.empty();
                    size_t sourceSize = usingGrouped ? g_dropdownStops.size() : busStops.size();

                    if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < sourceSize) {
                        BusStopInfo chosen = usingGrouped ? g_dropdownStops[selectedIndex] : busStops[selectedIndex];
                        g_selectedStopID = chosen.stopID;
                        for (size_t i = 0; i < busStops.size(); ++i) {
                            if (busStops[i].stopID == g_selectedStopID) {
                                currentBusStopIndex = i;
                                break;
                            }
                        }
                        showDropDownMenu = false;
                        g_dropdownStops.clear();
                        autoTurnOff = 0;
                    }
                }

                DEV_Delay_ms(5);

                autoTurnOff = 0;  // Reset idle timer on any touch
                break;  // Exit the wait loop and refresh immediately
            }

            DEV_Delay_ms(100);  // Check every 100ms

            autoTurnOff++;
            if(autoTurnOff >= 3000){ // After 5 minutes of idle (3000 * 100ms), turn off display
                std::cout << "No activity detected, turning off display to save power." << std::endl;
                std::cout << "Current time: " << std::put_time(timeinfo, "%H:%M:%S") << std::endl;
                Paint_Clear(BLACK);
                displayBuffer(BlackImage);

                st7796_sleep();
                backlight_off();

                // Wait for touch to turn back on
                bool wokenUp = false;
                while(!wokenUp){
                    if (get_touch_data(&touch_data)) {
                        std::cout << "Touch detected, turning display back on." << std::endl;
                        time_t now = time(nullptr);
                        std::tm* timeinfo = localtime(&now);
                        std::cout << "Current time: " << std::put_time(timeinfo, "%H:%M:%S") << std::endl;
                        DEV_Delay_ms(50);
                        autoTurnOff = 0;
                        wokenUp = true;
                    }
                    DEV_Delay_ms(100);
                }
                st7796_wakeup();
                backlight_on();
                displayBuffer(BlackImage);
                break;
            }
        }
    }

    // Not reached: the loop above runs until the process is stopped
    free(BlackImage);
    DEV_ModuleExit();
    return 0;
}

#endif // BUSTRACKER_HW

// ---------------------------------------------------------------------------

int main(int argc, char **argv) {
    Options o;
    if (!parseOptions(argc, argv, o)) return 2;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    int rc;
    if (!o.snapshotPath.empty()) {
        rc = runSnapshot(o);
    } else if (!o.fetchStop.empty()) {
        rc = runFetch(o);
    } else if (!o.fixturePath.empty()) {
        rc = runFixtureDump(o);
    } else {
#ifdef BUSTRACKER_HW
        rc = runDisplay();
#else
        std::cerr << "This busTrack was built without display support (wiringPi not found).\n"
                  << "Use --snapshot or --fetch, or install wiringPi and rebuild.\n";
        rc = 1;
#endif
    }
    curl_global_cleanup();
    return rc;
}
