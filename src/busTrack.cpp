/*
 * Bus Tracking Program for Lothian Buses
 *
 * Modes:
 *   busTrack                                  run the display (needs the BUSTRACKER_HW build)
 *   busTrack --snapshot out.png [--fixture f.json] [--now YYYY-MM-DDTHH:MM:SS]
 *            [--age SECS] [--screen NAME]     render one frame to a PNG; no GPIO/SPI/touch
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
#include "renderDisplay.h"  // drawMainScreen, flushToLcd
#include "screens.h"        // screen state, touch areas, transitions
#include "screens_draw.h"   // drawing for every screen but the board
#include "settings.h"
#include "../gfx/canvas.h"

#ifdef BUSTRACKER_HW
extern "C" {
    #include "../third_party/waveshare/Config/DEV_Config.h"
    #include "../third_party/waveshare/lcd/st7796.h"
    #include "../third_party/waveshare/lcd/ft6336u.h"
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
    int ageSecs = 0;            // --age: render as if the data is this many seconds old
    std::string direction = "none";  // --direction into|out|none: board highlight (snapshot)
};

static bool parseOptions(int argc, char **argv, Options &o) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        bool hasNext = i + 1 < argc;
        if (arg == "--snapshot" && hasNext) o.snapshotPath = argv[++i];
        else if (arg == "--fixture" && hasNext) o.fixturePath = argv[++i];
        else if (arg == "--screen" && hasNext) o.screen = argv[++i];
        else if (arg == "--fetch" && hasNext) o.fetchStop = argv[++i];
        else if (arg == "--age" && hasNext) o.ageSecs = std::atoi(argv[++i]);
        else if (arg == "--direction" && hasNext) o.direction = argv[++i];
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
// snapshot mode, so the PNG is the frame the LCD gets. Frames are drawn as the
// person sees them; flushToLcd does the panel's flip.

// Sample stops for the picker in snapshot mode (no network needed).
static std::vector<BusStopInfo> sampleStops() {
    const char *names[] = {"Princes Street", "Lothian Road", "Haymarket Station", "George Street",
                           "North Bridge", "Morrison Street", "Tollcross"};
    const int metres[] = {630, 880, 1240, 1510, 2100, 2350, 2790};
    std::vector<BusStopInfo> out;
    for (int i = 0; i < 7; ++i) {
        BusStopInfo s;
        s.stopName = names[i];
        s.stopID = "sample" + std::to_string(i);
        s.distance_m = metres[i];
        out.push_back(s);
    }
    return out;
}

// Second line of a stop row: "630m - into city", or just the direction if the distance is unknown.
static std::string stopDetail(const BusStopInfo &s, StopDirection dir) {
    std::string d = dir == StopDirection::Into ? "into city" : "out of city";
    if (s.distance_m < 0) return d;
    std::string dist = s.distance_m < 1000
        ? std::to_string(s.distance_m) + "m"
        : std::to_string(s.distance_m / 1000) + "." + std::to_string(s.distance_m % 1000 / 100) + "km";
    return dist + " - " + d;   // the baked fonts are ASCII only
}

// True if a stop with this ATCO code is in the list.
static bool hasStop(const std::vector<BusStopInfo> &list, const std::string &stopID) {
    return std::any_of(list.begin(), list.end(),
                       [&](const BusStopInfo &s) { return s.stopID == stopID; });
}

// Seconds since the board's data was fetched, or -1 if there is none. The error
// screen shows it when it is recent.
static long staleAgeSecs(const StopBoard &board, time_t now) {
    return board.fetchedAt > 0 ? static_cast<long>(now - board.fetchedAt) : -1;
}

static void drawFrame(gfx::Canvas &frame, Screen screen, StopDirection dir, ErrorReason reason,
                      const StopBoard &board, const std::vector<BusStopInfo> &list,
                      time_t now, long staleSecs, int retrySecs) {
    switch (screen) {
    case Screen::Board:     drawMainScreen(frame, board, now, dir); break;
    case Screen::Boot:      drawBootScreen(frame, "Finding stops..."); break;
    case Screen::Loading:   drawLoadingScreen(frame, "Finding stops...", now); break;
    case Screen::StopList: {
        std::vector<std::string> names, details;
        for (const auto &s : list) {
            names.push_back(s.stopName);
            details.push_back(stopDetail(s, dir));
        }
        drawStopListScreen(frame, dir == StopDirection::Into ? "Into city" : "Out of city",
                           names, details, now);
        break;
    }
    case Screen::Error:     drawErrorScreen(frame, reason, retrySecs, staleSecs, now); break;
    case Screen::Empty:     drawEmptyScreen(frame, board.stopName, now); break;
    case Screen::Asleep:    drawAsleepScreen(frame); break;
    }
}

// ---------------------------------------------------------------------------
// Snapshot mode

static bool parseScreenName(const std::string &name, Screen &screen, ErrorReason &reason) {
    reason = ErrorReason::None;
    if      (name == "main")            screen = Screen::Board;
    else if (name == "boot")            screen = Screen::Boot;
    else if (name == "loading")         screen = Screen::Loading;
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

    // --age shows the same board as if its data were that many seconds old
    const time_t frameNow = board.fetchedAt > 0 ? board.fetchedAt + o.ageSecs : now;
    const long staleSecs = o.ageSecs > 0 ? o.ageSecs : -1;

    const int w = ST7796_WIDTH, h = ST7796_HEIGHT;
    uint16_t *buffer = static_cast<uint16_t *>(malloc(sizeof(uint16_t) * w * h));
    if (!buffer) { std::cerr << "Out of memory\n"; return 1; }
    gfx::Canvas frame(buffer, w, h);

    StopDirection dir = StopDirection::None;
    if (o.direction == "into") dir = StopDirection::Into;
    else if (o.direction == "out") dir = StopDirection::Out;
    drawFrame(frame, screen, dir, reason, board, sampleStops(), frameNow, staleSecs, 42);

    bool ok = writePngRGB565(o.snapshotPath, frame.data(), w, h);
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

// Seconds until the next minute boundary (1..60): when the clock on screen changes.
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
    st7796_clear(0x0000);  // black
    ft6336u_init();

    uint16_t *buffer = static_cast<uint16_t *>(malloc(sizeof(uint16_t) * ST7796_WIDTH * ST7796_HEIGHT));
    if (!buffer) {
        std::cerr << "Failed to allocate memory for the frame" << std::endl;
        DEV_ModuleExit();
        return 1;
    }
    gfx::Canvas frame(buffer, ST7796_WIDTH, ST7796_HEIGHT);

    // What the state machine is driving, and the work it asks for
    UiState ui;
    StopBoard board;                         // the board on screen, from the refresher
    BusStopInfo boardStop;                   // the stop on the board
    std::string refreshedStopId;             // the stop the refresher is fetching
    std::vector<BusStopInfo> pick;           // stops for the direction being picked
    BoardRefresher refresher;                // fetch policy, backoff, last good data
    touch_data_t touch;
    TapTracker taps;
    long lastTouchMs = nowMs();

    // Copy the refresher's board for the stop on screen; the name comes from the stop list.
    auto takeBoard = [&]() {
        board = refresher.board(0);
        board.stopName = boardStop.stopName;
        board.distanceFromHomeM = boardStop.distance_m;
    };

    auto flush = [&]() {
        const time_t now = time(nullptr);
        drawFrame(frame, ui.screen, ui.dir, ui.reason, board, pick, now,
                  staleAgeSecs(board, now), secondsToNextMinute());
        flushToLcd(frame);
    };

    // Carry out the work an effect asks for; each result feeds back into the state machine.
    auto perform = [&](Effect e) {
        while (e.kind != Effect::None && e.kind != Effect::Sleep) {
            switch (e.kind) {
            case Effect::FetchBoard:
                // A user-asked fetch: fetch now. Same stop keeps its last good data;
                // a different stop starts fresh.
                if (refreshedStopId == boardStop.stopID) {
                    refresher.refreshNow();
                } else {
                    refresher.setStops({boardStop.stopID});
                    refreshedStopId = boardStop.stopID;
                }
                refresher.update(time(nullptr));
                takeBoard();
                std::cout << "board: " << boardStatusName(board.status)
                          << (board.statusDetail.empty() ? "" : " (" + board.statusDetail + ")")
                          << ", " << board.departures.size() << " departures" << std::endl;
                e = onBoard(ui, board.status);
                break;
            case Effect::Lookup:
                if (e.dir == StopDirection::None) {          // startup, or retry after no stops
                    std::vector<BusStopInfo> nearest;
                    bool ok = getNearestStops(address, 1, nearest) && !nearest.empty();
                    if (ok) boardStop = nearest.front();
                    // The nearest stop's own direction, so the board highlights the right button.
                    // A stop in neither group highlights neither.
                    StopDirection bd = StopDirection::None;
                    GroupedStops g;
                    if (ok && getNearestStopsGrouped(address, perGroup, g)) {
                        if (hasStop(g.intoCity, boardStop.stopID)) bd = StopDirection::Into;
                        else if (hasStop(g.outOfCity, boardStop.stopID)) bd = StopDirection::Out;
                    }
                    e = onBoot(ui, ok, ok ? 1 : 0, bd);
                } else {
                    GroupedStops g;
                    bool ok = getNearestStopsGrouped(address, perGroup, g);
                    if (ok) pick = e.dir == StopDirection::Into ? g.intoCity : g.outOfCity;
                    else pick.clear();
                    e = onLookup(ui, ok, static_cast<int>(pick.size()));
                }
                break;
            case Effect::ShowStop:
                std::cout << "picked stop row " << e.index << " of " << pick.size() << std::endl;
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
    drawBootScreen(frame, "Finding stops...");
    flushToLcd(frame);
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
                std::cout << "Tap at (" << tap.x << ", " << tap.y << "): action "
                          << static_cast<int>(action) << std::endl;
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
                    frame.clear(0);
                    flushToLcd(frame);
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
                    tapped = true;                 // redraw straight away
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
