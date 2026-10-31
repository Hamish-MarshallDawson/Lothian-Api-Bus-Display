// Self-check for screens.cpp: hit areas, transitions and the tap tracker.
// Run with: make test   (no display or network needed)

#include <cstdio>

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

static void testLayout() {
    // Every touch target on every screen: at least 48 px, inside the panel, no overlap.
    const Screen all[] = {Screen::Board, Screen::StopList, Screen::Error, Screen::Empty,
                          Screen::Asleep};
    for (Screen s : all) {
        Hit hits[kMaxStopRows + 2];
        int n = listHits(s, kMaxStopRows, hits, kMaxStopRows + 2);
        for (int i = 0; i < n; ++i) {
            const Rect &r = hits[i].rect;
            if (s != Screen::Asleep) {  // Asleep is the whole panel on purpose
                CHECK(r.w >= kMinTouch && r.h >= kMinTouch);
            }
            CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= kScreenW && r.y + r.h <= kScreenH);
            for (int j = i + 1; j < n; ++j) {
                if (s == Screen::Asleep) continue;
                CHECK(!overlaps(r, hits[j].rect));
            }
        }
    }
}

static void testHitTest() {
    int arg = -1;
    // Board: header opens the stop list; the two direction buttons sit under it
    CHECK(hitTest(Screen::Board, 0, 160, 40, arg) == Action::OpenStopList);
    CHECK(hitTest(Screen::Board, 0, 160, 10, arg) == Action::None);      // status bar
    CHECK(hitTest(Screen::Board, 0, 60, 96, arg) == Action::PickInto);
    CHECK(hitTest(Screen::Board, 0, 250, 96, arg) == Action::PickOut);
    CHECK(hitTest(Screen::Board, 0, 160, 200, arg) == Action::None);   // a stop row, not a button
    CHECK(hitTest(Screen::Board, 0, 160, 150, arg) == Action::None);

    // Stop rows start under the header (y 72): row 2 is y 184..239; rows past stopCount do nothing
    CHECK(hitTest(Screen::StopList, 3, 100, 200, arg) == Action::PickStop && arg == 2);
    CHECK(hitTest(Screen::StopList, 3, 100, 340, arg) == Action::None);
    CHECK(hitTest(Screen::StopList, 9, 100, 72 + 56 * 6 + 5, arg) == Action::PickStop && arg == 6);
    CHECK(hitTest(Screen::StopList, 9, 100, 72 + 56 * 7 + 5, arg) == Action::None);  // only 7 rows
    CHECK(hitTest(Screen::StopList, 9, 20, 40, arg) == Action::Back);

    CHECK(hitTest(Screen::Error, 0, 160, 360, arg) == Action::Retry);
    CHECK(hitTest(Screen::Error, 0, 160, 430, arg) == Action::Choose);
    CHECK(hitTest(Screen::Asleep, 0, 5, 470, arg) == Action::Wake);
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

static void testFlow() {
    Effect e;
    UiState st;
    st.screen = Screen::Boot;

    // Startup finds stops, then the board fetches
    e = onBoot(st, true, 8);
    CHECK(st.screen == Screen::Board && e.kind == Effect::FetchBoard);

    // Direction button on the board: goes to loading, then straight back to the board
    // showing the nearest stop that way (row 0), no separate direction screen
    e = step(st, Action::PickOut, 0);
    CHECK(st.screen == Screen::Loading && e.kind == Effect::Lookup && e.dir == StopDirection::Out);
    e = onLookup(st, true, 6);
    CHECK(st.screen == Screen::Board && e.kind == Effect::ShowStop && e.index == 0);
    CHECK(st.dir == StopDirection::Out && st.stopCount == 6);

    // Header tap opens the stop list for the current direction
    e = step(st, Action::OpenStopList, 0);
    CHECK(st.screen == Screen::Loading && e.kind == Effect::Lookup && e.dir == StopDirection::Out);
    e = onLookup(st, true, 5);
    CHECK(st.screen == Screen::StopList && st.stopCount == 5 && e.kind == Effect::None);

    // Pick row 2 -> board shows that stop
    e = step(st, Action::PickStop, 2);
    CHECK(st.screen == Screen::Board && e.kind == Effect::ShowStop && e.index == 2);

    // Stop list Back returns to the board it came from
    step(st, Action::OpenStopList, 0);
    onLookup(st, true, 4);
    CHECK(st.screen == Screen::StopList);
    step(st, Action::Back, 0);
    CHECK(st.screen == Screen::Board);

    // Into button, then a lookup that finds no stops: error with a reason
    e = step(st, Action::PickInto, 0);
    CHECK(st.dir == StopDirection::Into);
    onLookup(st, true, 0);
    CHECK(st.screen == Screen::Error && st.reason == ErrorReason::NoStops);
    e = step(st, Action::Retry, 0);                  // NoStops: retry does the lookup again
    CHECK(st.screen == Screen::Loading && e.kind == Effect::Lookup && e.dir == StopDirection::Into);
    onLookup(st, false, 0);
    CHECK(st.screen == Screen::Error && st.reason == ErrorReason::NoNetwork);

    // Board fetch statuses map to screens
    e = onBoard(st, BoardStatus::Ok);
    CHECK(st.screen == Screen::Board);
    e = onBoard(st, BoardStatus::KeyRejected);
    CHECK(st.screen == Screen::Error && st.reason == ErrorReason::KeyRejected);
    e = onBoard(st, BoardStatus::RateLimited);
    CHECK(st.reason == ErrorReason::RateLimited);
    e = onBoard(st, BoardStatus::NoDepartures);
    CHECK(st.screen == Screen::Empty);
    e = onBoard(st, BoardStatus::NetworkError);
    CHECK(st.screen == Screen::Error && st.reason == ErrorReason::NoNetwork);

    // Choose from an error opens the stop list; Back returns to the error
    e = step(st, Action::Choose, 0);
    CHECK(st.screen == Screen::Loading && e.kind == Effect::Lookup);
    onLookup(st, true, 3);
    CHECK(st.screen == Screen::StopList);
    step(st, Action::Back, 0);
    CHECK(st.screen == Screen::Error);

    // Idle on the board sleeps; any touch wakes to the board and fetches once.
    // Idle on the stop list does nothing (the list stays up).
    onBoard(st, BoardStatus::Ok);
    e = onIdle(st);
    CHECK(st.screen == Screen::Asleep && e.kind == Effect::Sleep);
    e = step(st, Action::Wake, 0);
    CHECK(st.screen == Screen::Board && e.kind == Effect::FetchBoard);

    step(st, Action::OpenStopList, 0);
    onLookup(st, true, 3);
    e = onIdle(st);
    CHECK(st.screen == Screen::StopList && e.kind == Effect::None);

    // Minute refresh on the board fetches; while the list is open it does nothing
    step(st, Action::Back, 0);
    e = onMinute(st);
    CHECK(e.kind == Effect::FetchBoard);
    step(st, Action::OpenStopList, 0);
    onLookup(st, true, 3);
    e = onMinute(st);
    CHECK(e.kind == Effect::None);

    // A tap that is not on a button does nothing
    UiState s2;
    s2.screen = Screen::StopList;
    s2.stopCount = 3;
    e = step(s2, Action::None, 0);
    CHECK(s2.screen == Screen::StopList && e.kind == Effect::None);

    // A stop row beyond the list does nothing
    e = step(s2, Action::PickStop, 5);
    CHECK(s2.screen == Screen::StopList && e.kind == Effect::None);

    // Startup with no stops goes to an error, and Retry looks up nearest stops
    UiState s3;
    e = onBoot(s3, false, 0);
    CHECK(s3.screen == Screen::Error && s3.reason == ErrorReason::NoStops);
    e = step(s3, Action::Retry, 0);
    CHECK(e.kind == Effect::Lookup && e.dir == StopDirection::None);
}

int main() {
    testLayout();
    testHitTest();
    testTapTracker();
    testFlow();
    if (failures) {
        std::fprintf(stderr, "test_screens: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_screens: all checks passed\n");
    return 0;
}
