// Self-check for screens.cpp, hidden_routes.cpp and pairing.cpp: hit areas,
// transitions, the tap tracker, hidden routes and paired stops.
// Run with: make test   (no display or network needed)

#include <cstdio>
#include <cstdlib>

#include "hidden_routes.h"
#include "pairing.h"
#include "screens.h"

static int failures = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static bool overlaps(const Rect &a, const Rect &b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// A board state with chips and rows, for hit tests. The zones stand in for what
// busTrack takes from gfx::chipLayout and theme::rowRect: chips 44 x 32 in a row at
// y 124, rows 52 px from y 184.
static UiState boardState(int chips, int rows) {
    UiState st;
    st.screen = Screen::Board;
    st.chipCount = chips;
    st.rowCount = rows;
    for (int i = 0; i < kMaxChips; ++i) st.chipZones[i] = Rect{12 + i * 50, 124, 44, 32};
    for (int i = 0; i < kBoardMaxRows; ++i) st.rowZones[i] = Rect{0, 184 + 52 * i, 320, 52};
    return st;
}

static void testLayout() {
    // Every touch target on every screen: inside the panel, no overlaps. Chips are the
    // one exception to 48 px: 44 x 32, as the spec allows (>= 40 x 32).
    UiState states[] = {boardState(kMaxChips, kBoardMaxRows), UiState(), UiState(), UiState(), UiState(), UiState()};
    states[1].screen = Screen::BusDetail;
    states[2].screen = Screen::StopList;
    states[2].stopCount = kMaxStopRows;
    states[3].screen = Screen::Error;
    states[4].screen = Screen::Empty;
    states[5].screen = Screen::Asleep;
    for (const UiState &st : states) {
        Hit hits[kMaxChips + kBoardMaxRows + 4];
        const int n = listHits(st, hits, kMaxChips + kBoardMaxRows + 4);
        for (int i = 0; i < n; ++i) {
            const Rect &r = hits[i].rect;
            if (st.screen != Screen::Asleep) {          // Asleep is the whole panel on purpose
                if (hits[i].action == Action::ToggleRoute) {
                    CHECK(r.w >= 40 && r.h >= 32);
                } else {
                    CHECK(r.w >= kMinTouch && r.h >= kMinTouch);
                }
            }
            CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= kScreenW && r.y + r.h <= kScreenH);
            for (int j = i + 1; j < n; ++j) {
                if (st.screen == Screen::Asleep) continue;
                CHECK(!overlaps(r, hits[j].rect));
            }
        }
    }
    // Chips and rows sit below the Into/Out buttons (y 72-119) and above the rows
    CHECK(boardState(1, 1).chipZones[0].y >= 120 && boardState(1, 1).chipZones[0].y + 32 <= 160);
    CHECK(boardState(1, 1).rowZones[0].y >= 160 + 24);
}

static void testBoardHitTest() {
    const UiState st = boardState(3, 4);
    int arg = -1;
    CHECK(hitTest(st, 160, 40, arg) == Action::OpenStopList);
    CHECK(hitTest(st, 60, 96, arg) == Action::PickInto);
    CHECK(hitTest(st, 250, 96, arg) == Action::PickOut);
    CHECK(hitTest(st, 160, 10, arg) == Action::None);       // status bar

    // Chips: the second chip is at x 62..105, y 124..155
    CHECK(hitTest(st, 80, 140, arg) == Action::ToggleRoute && arg == 1);
    CHECK(hitTest(st, 80, 170, arg) == Action::None);       // under the chip, above the rows
    CHECK(hitTest(boardState(1, 4), 30, 140, arg) == Action::ToggleRoute && arg == 0);   // chip 0 at x 12-55
    CHECK(hitTest(boardState(1, 4), 80, 140, arg) == Action::None);                       // chip 1 not shown

    // Rows: row 2 is y 288..339; rows past rowCount do nothing
    CHECK(hitTest(st, 100, 300, arg) == Action::OpenDetail && arg == 2);
    CHECK(hitTest(st, 100, 60 + 56 * 4 + 200, arg) == Action::None);
    CHECK(hitTest(boardState(3, 2), 100, 300, arg) == Action::None);   // only 2 rows
    CHECK(hitTest(boardState(3, 9), 100, 184 + 52 * 4 + 5, arg) == Action::OpenDetail && arg == 4);
    CHECK(hitTest(boardState(3, 9), 100, 184 + 52 * 5 + 5, arg) == Action::None);   // 5 rows fit
}

static void testStopListAndErrorHitTest() {
    UiState list;
    list.screen = Screen::StopList;
    list.stopCount = 3;
    int arg = -1;
    CHECK(hitTest(list, 100, 72 + 64 * 2 + 5, arg) == Action::PickStop && arg == 2);
    CHECK(hitTest(list, 100, 340, arg) == Action::None);
    CHECK(hitTest(list, 20, 40, arg) == Action::Back);
    CHECK(hitTest(list, 110, 60, arg) == Action::Back);     // whole left corner
    CHECK(hitTest(list, 20, 10, arg) == Action::Back);      // status bar too
    CHECK(hitTest(list, 200, 40, arg) == Action::None);     // title is not a button

    UiState err;
    err.screen = Screen::Error;
    CHECK(hitTest(err, 160, 360, arg) == Action::Retry);
    CHECK(hitTest(err, 160, 430, arg) == Action::Choose);
    CHECK(hitTest(err, 20, 40, arg) == Action::Back);
    UiState empty;
    empty.screen = Screen::Empty;
    CHECK(hitTest(empty, 20, 40, arg) == Action::Back);

    UiState asleep;
    asleep.screen = Screen::Asleep;
    CHECK(hitTest(asleep, 5, 470, arg) == Action::Wake);
}

static void testBusDetailHitTest() {
    UiState st;
    st.screen = Screen::BusDetail;
    int arg = -1;
    CHECK(hitTest(st, 20, 40, arg) == Action::Back);
    CHECK(hitTest(st, 160, 150, arg) == Action::None);     // the map and info take no taps
    CHECK(hitTest(st, 160, 400, arg) == Action::None);
}

static void testTapTracker() {
    TapTracker t;
    Tap tap{-1, -1};
    CHECK(!t.update(true, 100, 100, 0, tap));
    CHECK(!t.update(true, 101, 99, 50, tap));
    CHECK(t.update(false, 0, 0, 100, tap));           // lift: a tap at the press point
    CHECK(tap.x == 100 && tap.y == 100);

    // Held finger: only the lift fires, once
    CHECK(!t.update(true, 50, 50, 1000, tap));
    CHECK(!t.update(true, 50, 50, 1100, tap));
    CHECK(!t.update(true, 50, 50, 1200, tap));
    CHECK(t.update(false, 0, 0, 1300, tap));

    // Drag past the slop: no tap
    CHECK(!t.update(true, 100, 100, 2000, tap));
    CHECK(!t.update(true, 150, 100, 2050, tap));
    CHECK(!t.update(false, 0, 0, 2100, tap));

    // Held too long: no tap
    CHECK(!t.update(true, 100, 100, 3000, tap));
    CHECK(!t.update(false, 0, 0, 3000 + kTapMaxMs + 1, tap));

    // Lift with no press: nothing
    CHECK(!t.update(false, 0, 0, 4000, tap));
}

static void testBoardFlow() {
    Effect e;
    UiState st;
    st.screen = Screen::Boot;

    // Startup: the nearest stop's own direction; neither when it is in neither group
    e = onBoot(st, true, 8, StopDirection::Out);
    CHECK(st.screen == Screen::Board && st.dir == StopDirection::Out && e.kind == Effect::FetchBoard);
    UiState neither;
    onBoot(neither, true, 8, StopDirection::None);
    CHECK(neither.dir == StopDirection::None);
    UiState failed;
    failed.dir = StopDirection::Into;
    onBoot(failed, false, 0, StopDirection::Into);
    CHECK(failed.screen == Screen::Error && failed.dir == StopDirection::None);

    // Into keeps the stop on screen: a paired lookup, then the stop it picks is shown
    st.chipCount = 3;
    st.rowCount = 4;
    e = step(st, Action::PickInto, 0);
    CHECK(st.screen == Screen::Loading && e.kind == Effect::Lookup && e.dir == StopDirection::Into);
    CHECK(e.pair);
    e = onLookup(st, true, 6);
    CHECK(st.screen == Screen::Board && e.kind == Effect::ShowStop && e.index == 0);
    CHECK(st.dir == StopDirection::Into);

    // Out the same way
    e = step(st, Action::PickOut, 0);
    CHECK(e.kind == Effect::Lookup && e.dir == StopDirection::Out && e.pair);
    onLookup(st, true, 6);
    CHECK(st.dir == StopDirection::Out);

    // The header still opens the plain list (no pairing)
    e = step(st, Action::OpenStopList, 0);
    CHECK(e.kind == Effect::Lookup && !e.pair && e.dir == StopDirection::Out);
    onLookup(st, true, 5);
    CHECK(st.screen == Screen::StopList && st.stopCount == 5);
    e = step(st, Action::PickStop, 2);
    CHECK(st.screen == Screen::Board && e.kind == Effect::ShowStop && e.index == 2);

    // A chip tap asks the loop to toggle that chip's route; the screen does not change
    e = step(st, Action::ToggleRoute, 1);
    CHECK(st.screen == Screen::Board && e.kind == Effect::ToggleRoute && e.index == 1);

    // A row tap opens the bus detail; Back returns to the board
    e = step(st, Action::OpenDetail, 3);
    CHECK(st.screen == Screen::BusDetail && st.detailRow == 3 && e.kind == Effect::None);
    e = step(st, Action::OpenDetail, 9);      // a row that is not there is ignored
    CHECK(st.screen == Screen::BusDetail);
    step(st, Action::Back, 0);
    CHECK(st.screen == Screen::Board && st.detailRow == -1);

    // Lookup failures and no stops
    step(st, Action::PickInto, 0);
    onLookup(st, true, 0);
    CHECK(st.screen == Screen::Error && st.reason == ErrorReason::NoStops);
    e = step(st, Action::Retry, 0);
    CHECK(st.screen == Screen::Loading && e.kind == Effect::Lookup && e.dir == StopDirection::Into);
    onLookup(st, false, 0);
    CHECK(st.screen == Screen::Error && st.reason == ErrorReason::NoNetwork);

    // Back on an error goes to the board with no fetch
    e = step(st, Action::Back, 0);
    CHECK(st.screen == Screen::Board && e.kind == Effect::None);

    // Board fetch statuses map to screens
    onBoard(st, BoardStatus::Ok);
    CHECK(st.screen == Screen::Board);
    onBoard(st, BoardStatus::KeyRejected);
    CHECK(st.screen == Screen::Error && st.reason == ErrorReason::KeyRejected);
    onBoard(st, BoardStatus::NoDepartures);
    CHECK(st.screen == Screen::Empty);
    onBoard(st, BoardStatus::NetworkError);
    CHECK(st.screen == Screen::Error && st.reason == ErrorReason::NoNetwork);
}

static void testDetailRefreshAndSleep() {
    UiState st;
    st.screen = Screen::Board;
    st.rowCount = 3;
    step(st, Action::OpenDetail, 1);

    // The detail refreshes on its own timer; the board's minute tick does nothing there
    Effect e = onDetailTick(st);
    CHECK(st.screen == Screen::BusDetail && e.kind == Effect::FetchBoard);
    e = onMinute(st);
    CHECK(e.kind == Effect::None);
    CHECK(kDetailRefreshS >= 15 && kDetailRefreshS <= 20);

    // The sleep timer still applies on the detail; any touch wakes to the board
    e = onIdle(st);
    CHECK(st.screen == Screen::Asleep && e.kind == Effect::Sleep);
    e = step(st, Action::Wake, 0);
    CHECK(st.screen == Screen::Board && e.kind == Effect::FetchBoard);

    // The tick does nothing off the detail screen
    e = onDetailTick(st);
    CHECK(e.kind == Effect::None);
}

static void testHiddenRoutes() {
    // Parsing: one route per line, blanks and comments ignored, spaces trimmed
    RouteSet r = parseHiddenRoutes("44\n\n  8  \r\n# a comment\n44\nN22\n");
    CHECK(r.size() == 3 && r.count("44") && r.count("8") && r.count("N22"));
    CHECK(parseHiddenRoutes("").empty());

    // Round trip
    CHECK(parseHiddenRoutes(formatHiddenRoutes(r)) == r);

    // Filtering: hidden routes leave the rows but stay as chips
    StopBoard board;
    auto dep = [](const char *service, bool hasMin) {
        Departure d;
        d.service = service;
        d.destination = "X";
        d.hasMinutes = hasMin;
        d.minutes = 5;
        return d;
    };
    board.departures = {dep("44", true), dep("8", true), dep("44", true), dep("22", false), dep("14", true)};
    RouteSet hidden = {"44"};
    const auto rows = visibleRows(board, hidden);
    CHECK(rows.size() == 2);
    CHECK(rows[0]->service == "8" && rows[1]->service == "14");
    const auto chips = boardRoutes(board);
    CHECK(chips.size() == 3 && chips[0] == "44" && chips[1] == "8" && chips[2] == "14");   // 22 has no time
    CHECK(visibleRows(board, RouteSet()).size() == 4);

    // Save and load through a real file
    const std::string path = "test-hidden-routes.tmp";
    std::remove(path.c_str());
    RouteSet none;
    CHECK(loadHiddenRoutes(path, none) && none.empty());      // missing file = nothing hidden
    CHECK(saveHiddenRoutes(path, hidden));
    RouteSet back;
    CHECK(loadHiddenRoutes(path, back) && back == hidden);
    CHECK(saveHiddenRoutes(path, RouteSet()));                 // showing all again empties the file
    CHECK(loadHiddenRoutes(path, back) && back.empty());
    std::remove(path.c_str());

    CHECK(hiddenRoutesPath().find(".config/bustracker/hidden-routes") != std::string::npos);
}

static void testPairing() {
    // Two sides of the road ~20 m apart, and a stop 2 km away
    PairStop here{"JB-IN", "Leith Street", 55.9560, -3.1860};
    std::vector<PairStop> out = {
        {"FAR", "Far Away", 55.9760, -3.1860},          // ~2.2 km
        {"OPP", "Leith Street", 55.9562, -3.1861},     // ~25 m, same name
        {"NEAR", "Another Name", 55.9561, -3.1860},     // ~11 m, different name
    };
    // Same name wins over the nearer stop with another name
    CHECK(pairedIndex(out, here) == 1);

    // The stop itself, if it is in the list
    std::vector<PairStop> withSelf = {{"X", "Y", 0, 0}, {here.id, here.name, here.lat, here.lon}};
    CHECK(pairedIndex(withSelf, here) == 1);

    // Nothing within 300 m: -1 (the caller falls back)
    std::vector<PairStop> far = {{"FAR", "Far", 55.9760, -3.1860}};
    CHECK(pairedIndex(far, here) == -1);

    // Nearest within range when no name matches
    std::vector<PairStop> noName = {{"A", "Alpha", 55.9570, -3.1860}, {"B", "Beta", 55.9561, -3.1860}};
    CHECK(pairedIndex(noName, here) == 1);

    // distanceM sanity: zero at the same point, about 111 km per degree of latitude
    CHECK(distanceM(55.9, -3.2, 55.9, -3.2) < 0.01);
    CHECK(distanceM(55.0, 0.0, 56.0, 0.0) > 110000 && distanceM(55.0, 0.0, 56.0, 0.0) < 112000);
}

// While the screen is asleep the app makes no API calls at all. Only a wake (one
// fetch, which is refreshNow on the same stop) reaches the network again.
static void testAsleepMakesNoCalls() {
    for (Screen from : {Screen::Board, Screen::BusDetail}) {
        UiState st;
        st.screen = Screen::Board;
        st.rowCount = 2;
        if (from == Screen::BusDetail) step(st, Action::OpenDetail, 0);
        Effect e = onIdle(st);
        CHECK(st.screen == Screen::Asleep && e.kind == Effect::Sleep);

        // Every timer and result that could fetch is a no-op while asleep
        CHECK(onMinute(st).kind == Effect::None);
        CHECK(onDetailTick(st).kind == Effect::None);
        CHECK(onBoard(st, BoardStatus::Ok).kind == Effect::None);
        CHECK(onBoard(st, BoardStatus::NetworkError).kind == Effect::None);
        CHECK(onLookup(st, true, 3).kind == Effect::None);
        CHECK(step(st, Action::Retry, 0).kind == Effect::None);
        CHECK(step(st, Action::PickInto, 0).kind == Effect::None);
        CHECK(step(st, Action::ToggleRoute, 0).kind == Effect::None);
        CHECK(onIdle(st).kind == Effect::None);                 // already asleep: no second sleep
        CHECK(st.screen == Screen::Asleep);

        // Only the wake fetches, and only once
        e = step(st, Action::Wake, 0);
        CHECK(st.screen == Screen::Board && e.kind == Effect::FetchBoard);
        CHECK(step(st, Action::Wake, 0).kind == Effect::None);  // a second wake does nothing
        CHECK(onDetailTick(st).kind == Effect::None);           // back on the board: no detail timer
    }
}

int main() {
    testAsleepMakesNoCalls();
    testLayout();
    testBoardHitTest();
    testStopListAndErrorHitTest();
    testBusDetailHitTest();
    testTapTracker();
    testBoardFlow();
    testDetailRefreshAndSleep();
    testHiddenRoutes();
    testPairing();
    if (failures) {
        std::fprintf(stderr, "test_screens: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_screens: all checks passed\n");
    return 0;
}
