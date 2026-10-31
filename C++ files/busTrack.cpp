/*
 * Bus Tracking Program for Lothian Buses
 *
 * Modes:
 *   busTrack                                  run the display (needs the BUSTRACKER_HW build)
 *   busTrack --snapshot out.png [--fixture f.json] [--now YYYY-MM-DDTHH:MM:SS] [--screen NAME]
 *                                             render one frame to a PNG; no GPIO/SPI/touch
 *                                             NAME: main boot loading direction stop-list
 *                                                   error-no-wifi error-key error-rate
 *                                                   error-no-stops empty asleep
 *   busTrack --fetch <atcocode>               print one stop's board from the live API
 *   busTrack --fixture f.json [--now ...]     print what a fixture parses to
 *
 * The display build is used only when wiringPi is installed (see Makefile).
 */

#include <algorithm>
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
#include "refresh.h"        // BoardRefresher: fetch policy and last good data
#include "renderDisplay.h"  // drawMainScreen, displayBuffer
#include "screens.h"        // screen state, touch areas, transitions
#include "screens_draw.h"   // drawing for every screen but the board
#include "settings.h"
#include "../gfx/canvas.h"

extern "C" {
    #include "../C/lib/GUI/GUI_Paint.h"
}

#ifdef BUSTRACKER_HW
extern "C" {
    #include "../C/lib/Config/DEV_Config.h"
    #include "../C/lib/lcd/st7796.h"
    #include "../C/lib/lcd/ft6336u.h"
}
#include <chrono>
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
// Drawing: one entry point for every screen, used by the display loop and by
// snapshot mode, so the PNG is the frame the LCD gets.

// Sample stops for the picker in snapshot mode (no network needed).
static const std::vector<std::string> kSampleStopNames = {
    "Princes Street", "Lothian Road", "Haymarket Station", "George Street",
    "North Bridge", "Morrison Street", "Tollcross"};

// The LCD takes the buffer in panel order. Paint (the board) draws that way
// already, through MIRROR_HORIZONTAL. Canvas draws in the order a person sees,
// so its frames are flipped here. shortcut: a flip per frame, until
// flushToLcd does it, when the board moves to Canvas too.
static void mirrorRows(UWORD *buf, int w, int h) {
    for (int y = 0; y < h; ++y) {
        UWORD *row = buf + static_cast<size_t>(y) * w;
        std::reverse(row, row + w);
    }
}

// Draws one frame into buf, in panel order, ready for displayBuffer.
static void drawFrame(UWORD *buf, gfx::Canvas &c, Screen screen, StopDirection dir,
                      ErrorReason reason, const StopBoard &board,
                      const std::vector<std::string> &names, int retrySecs) {
    if (screen == Screen::Board) {
        drawMainScreen(buf, board);
        return;
    }
    switch (screen) {
    case Screen::Boot:      drawBootScreen(c, "Finding stops..."); break;
    case Screen::Loading:   drawLoadingScreen(c, "Finding stops..."); break;
    case Screen::Direction: drawDirectionScreen(c); break;
    case Screen::StopList:
        drawStopListScreen(c, dir == StopDirection::Into ? "Into city" : "Out of city", names);
        break;
    case Screen::Error:     drawErrorScreen(c, reason, retrySecs); break;
    case Screen::Empty:     drawEmptyScreen(c, board.stopName); break;
    case Screen::Asleep:    drawAsleepScreen(c); break;
    case Screen::Board:     break;   // handled above
    }
    mirrorRows(buf, c.width(), c.height());
}

// ---------------------------------------------------------------------------
// Snapshot mode

static bool parseScreenName(const std::string &name, Screen &screen, ErrorReason &reason) {
    reason = ErrorReason::None;
    if      (name == "main")            screen = Screen::Board;
    else if (name == "boot")            screen = Screen::Boot;
    else if (name == "loading")         screen = Screen::Loading;
    else if (name == "direction")       screen = Screen::Direction;
    else if (name == "stop-list")       screen = Screen::StopList;
    else if (name == "empty")           screen = Screen::Empty;
    else if (name == "asleep")          screen = Screen::Asleep;
    else if (name == "error-no-wifi")   { screen = Screen::Error; reason = ErrorReason::NoNetwork; }
    else if (name == "error-key")       { screen = Screen::Error; reason = ErrorReason::KeyRejected; }
    else if (name == "error-rate")      { screen = Screen::Error; reason = ErrorReason::RateLimited; }
    else if (name == "error-no-stops")  { screen = Screen::Error; reason = ErrorReason::NoStops; }
    else return false;
    return true;
}

static int runSnapshot(const Options &o) {
    const time_t now = o.now ? o.now : time(nullptr);

    Screen screen;
    ErrorReason reason;
    if (!parseScreenName(o.screen, screen, reason)) {
        std::cerr << "Unknown --screen '" << o.screen << "'. Known screens: main boot loading "
                  << "direction stop-list error-no-wifi error-key error-rate error-no-stops "
                  << "empty asleep\n";
        return 2;
    }

    // Only the board needs data. The other screens draw fixed content.
    StopBoard board;
    if (screen == Screen::Board || screen == Screen::Empty) {
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
    }

    // Same Paint setup as the display loop, so the PNG is the buffer the LCD gets
    const int w = ST7796_WIDTH, h = ST7796_HEIGHT;
    UWORD *buffer = static_cast<UWORD *>(malloc(sizeof(UWORD) * w * h));
    if (!buffer) { std::cerr << "Out of memory\n"; return 1; }
    Paint_NewImage(buffer, w, h, 0, BLACK, 16);
    Paint_SetMirroring(MIRROR_HORIZONTAL);
    Paint_Clear(BLACK);

    gfx::Canvas canvas(buffer, w, h);
    drawFrame(buffer, canvas, screen, StopDirection::Into, reason, board, kSampleStopNames, 42);
    mirrorRows(buffer, w, h);   // the PNG shows the screen as a person sees it, not panel order

    bool ok = writePngRGB565(o.snapshotPath, buffer, w, h);
    free(buffer);
    if (!ok) { std::cerr << "Could not write " << o.snapshotPath << "\n"; return 1; }

    std::cout << "Wrote " << o.snapshotPath << " (screen " << o.screen << ")\n";
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

// Monotonic milliseconds, for tap timing and the idle timer.
static long nowMs() {
    using namespace std::chrono;
    return static_cast<long>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// Seconds until the next minute boundary (1..60): when the board refreshes.
static int secondsToNextMinute() {
    time_t t = time(nullptr);
    int s = 60 - std::localtime(&t)->tm_sec;
    return s <= 0 ? 60 : s;
}

static int runDisplay() {
    std::cout << "Lothian Bus Display Starting" << std::endl;

    const std::string address = CONFIG_ADDRESS();
    const int perGroup = CONFIG_NUM_STOPS();
    if (address.empty()) {
        std::cerr << "BUS_ADDRESS is not set; nothing to show\n";
        return 1;
    }

    if (DEV_ModuleInit() != 0) {
        std::cerr << "Failed to initialize display" << std::endl;
        return 1;
    }
    backlight_init();
    st7796_init();
    st7796_clear(BLACK);
    ft6336u_init();

    UWORD *buffer = static_cast<UWORD *>(malloc(sizeof(UWORD) * ST7796_WIDTH * ST7796_HEIGHT));
    if (!buffer) {
        std::cerr << "Failed to allocate memory for image buffer" << std::endl;
        DEV_ModuleExit();
        return 1;
    }
    Paint_NewImage(buffer, ST7796_WIDTH, ST7796_HEIGHT, 0, BLACK, 16);
    Paint_SetMirroring(MIRROR_HORIZONTAL);
    Paint_Clear(BLACK);
    gfx::Canvas canvas(buffer, ST7796_WIDTH, ST7796_HEIGHT);

    // What the state machine is driving, and the work it asks for
    UiState ui;
    StopBoard board;
    BusStopInfo boardStop;                   // the stop on the board
    std::vector<BusStopInfo> pick;           // stops for the direction being picked
    BoardRefresher refresher;                // fetch policy: normal, backoff, key, last good data
    touch_data_t touch;
    TapTracker taps;
    long lastTouchMs = nowMs();

    // Copy the refresher's board for the stop on screen; the name comes from the stop list.
    auto takeBoard = [&]() {
        board = refresher.board(0);
        board.stopName = boardStop.stopName;
    };

    auto flush = [&]() {
        std::vector<std::string> names;
        for (const auto &s : pick) names.push_back(s.stopName);
        drawFrame(buffer, canvas, ui.screen, ui.dir, ui.reason, board, names,
                  secondsToNextMinute());
        displayBuffer(buffer);
    };

    // Carry out the work an effect asks for; each result feeds back into the state machine.
    auto perform = [&](Effect e) {
        while (e.kind != Effect::None && e.kind != Effect::Sleep) {
            switch (e.kind) {
            case Effect::FetchBoard:
                // A user-asked fetch: point the refresher at this stop and fetch now
                refresher.setStops({boardStop.stopID});
                refresher.update(time(nullptr));
                takeBoard();
                e = onBoard(ui, board.status);
                break;
            case Effect::Lookup:
                if (e.dir == StopDirection::None) {          // startup, or retry after no stops
                    std::vector<BusStopInfo> nearest;
                    bool ok = getNearestStops(address, 1, nearest) && !nearest.empty();
                    if (ok) boardStop = nearest.front();
                    e = onBoot(ui, ok, ok ? 1 : 0);
                } else {
                    GroupedStops g;
                    bool ok = getNearestStopsGrouped(address, perGroup, g);
                    if (ok) pick = e.dir == StopDirection::Into ? g.intoCity : g.outOfCity;
                    else pick.clear();
                    e = onLookup(ui, ok, static_cast<int>(pick.size()));
                }
                break;
            case Effect::ShowStop:
                boardStop = pick[e.index];
                e = Effect(Effect::FetchBoard);
                break;
            default:
                e = Effect();
                break;
            }
        }
    };

    // The regular poll: the refresher fetches when one is due. Returns true if it fetched,
    // so the caller redraws.
    auto pollBoard = [&]() {
        if (!refresher.update(time(nullptr))) return false;
        takeBoard();
        perform(onBoard(ui, board.status));
        return true;
    };

    // Splash while the nearest stop is looked up; onBoot then fetches the first board
    drawFrame(buffer, canvas, Screen::Boot, StopDirection::None, ErrorReason::None, board, {}, 0);
    displayBuffer(buffer);
    perform(Effect(Effect::Lookup, StopDirection::None));

    while (true) {
        flush();

        // Wait for the next minute, polling touch. Taps act at once; idle time is counted.
        const long minuteEnd = nowMs() + secondsToNextMinute() * 1000L;

        bool tapped = false;
        while (nowMs() < minuteEnd && !tapped) {
            if (pollBoard()) break;                 // fetched: redraw now
            bool touching = get_touch_data(&touch);
            Tap tap;
            if (taps.update(touching, touch.coords[0].x, touch.coords[0].y, nowMs(), tap)) {
                tapped = true;
                lastTouchMs = nowMs();
                int arg = -1;
                Action action = hitTest(ui.screen, ui.stopCount, tap.x, tap.y, arg);
                std::cout << "Tap at (" << tap.x << ", " << tap.y << "): action " << static_cast<int>(action) << std::endl;
                if (action != Action::None) {
                    Effect e = step(ui, action, arg);
                    flush();                       // show "Finding stops..." before any network wait
                    perform(e);
                }
                break;
            }
            DEV_Delay_ms(20);

            // Five minutes with no touch on the board: sleep
            if (nowMs() - lastTouchMs >= 300000L) {
                Effect e = onIdle(ui);
                if (e.kind == Effect::Sleep) {
                    std::cout << "No activity, turning the display off." << std::endl;
                    canvas.clear(BLACK);
                    displayBuffer(buffer);
                    st7796_sleep();
                    backlight_off();

                    // Asleep: wait for one tap, which wakes the screen and is not acted on
                    while (true) {
                        Tap t;
                        if (taps.update(get_touch_data(&touch), touch.coords[0].x, touch.coords[0].y, nowMs(), t)) break;
                        DEV_Delay_ms(20);
                    }
                    st7796_wakeup();
                    backlight_on();
                    perform(step(ui, Action::Wake, 0));
                    lastTouchMs = nowMs();
                    tapped = true;                 // refresh straight away
                    break;
                }
                lastTouchMs = nowMs();             // menus stay up; restart the count
            }
        }

    }

    // Not reached: the loop above runs until the process is stopped
    free(buffer);
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
