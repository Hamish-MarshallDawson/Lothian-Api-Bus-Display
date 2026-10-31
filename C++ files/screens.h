#ifndef SCREENS_H
#define SCREENS_H

// Screen state, touch hit areas and transitions for every screen except the
// main departures board (that is drawMainScreen). Pure logic: no drawing and no
// hardware, so it builds and tests anywhere (make test).

#include "departures.h"

enum class Screen { Boot, Board, Direction, Loading, StopList, Error, Empty, Asleep };

// What a tap means. Board's header opens Direction; the rest are per-screen.
enum class Action { None, OpenDirection, Back, PickInto, PickOut, PickStop, Retry, Choose, Wake };

enum class StopDirection { None, Into, Out };

enum class ErrorReason { None, NoNetwork, KeyRejected, RateLimited, NoStops };

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Layout, in screen pixels (320 x 480 portrait). Drawing and hit testing both use
// these constants, so a button cannot drift away from its touch area.
constexpr int kScreenW = 320;
constexpr int kScreenH = 480;
constexpr int kMinTouch = 48;          // smallest touch target, either side
constexpr int kHeaderH = 64;           // theme::HEADER_H, the band every screen header uses
constexpr Rect kHeaderBar{0, 0, 320, kHeaderH};
constexpr Rect kBackBtn{0, 0, 80, kHeaderH};
constexpr Rect kDirInto{16, 96, 288, 96};
constexpr Rect kDirOut{16, 216, 288, 96};
constexpr Rect kRetryBtn{16, 336, 288, 56};
constexpr Rect kChooseBtn{16, 408, 288, 56};
constexpr int kStopRowsY = kHeaderH;
constexpr int kStopRowH = 56;
constexpr int kMaxStopRows = 7;
constexpr Rect stopRow(int i) { return Rect{0, kStopRowsY + kStopRowH * i, 320, kStopRowH}; }

struct Hit { Rect rect; Action action; int arg; };

// Every hit area on a screen, in priority order. stopCount limits the stop rows
// on StopList. Returns how many were written (at most max).
int listHits(Screen s, int stopCount, Hit *out, int max);

// The action for a tap at (x, y), or Action::None. arg gets the stop row index.
Action hitTest(Screen s, int stopCount, int x, int y, int &arg);

// Everything the loop needs to do after a transition.
struct Effect {
    enum Kind { None, Lookup, FetchBoard, ShowStop, Sleep };
    Kind kind;
    StopDirection dir;   // Lookup: nearest stops (None) or grouped
    int index;           // ShowStop: row in the stop list
    Effect(Kind k = None, StopDirection d = StopDirection::None, int i = -1)
        : kind(k), dir(d), index(i) {}
};

struct UiState {
    Screen screen = Screen::Boot;
    Screen prev = Screen::Board;              // where Back and Choose return to
    StopDirection dir = StopDirection::None;  // last direction picked
    ErrorReason reason = ErrorReason::None;
    int stopCount = 0;                        // rows in the current stop list
};

// A tap on the screen. arg is the stop row for PickStop.
Effect step(UiState &st, Action a, int arg);

// Results of the work the loop starts. Each returns what to do next.
Effect onBoot(UiState &st, bool ok, int count);            // nearest stops at startup
Effect onLookup(UiState &st, bool ok, int count);          // stops for a direction
Effect onBoard(UiState &st, BoardStatus status);           // a StopEvents fetch finished
Effect onMinute(UiState &st);                              // the once-a-minute refresh
Effect onIdle(UiState &st);                                // 5 minutes without a touch

// Turns raw touch polls into taps. A tap is a finger that goes down and comes
// up within kTapMaxMs without moving more than kTapSlop pixels. Taps fire on
// lift-off, so a held finger fires once, not on every poll.
constexpr long kTapMaxMs = 800;
constexpr int kTapSlop = 20;

struct Tap { int x, y; };

class TapTracker {
public:
    // Call once per touch poll. touching is false when no finger is down.
    // Returns true when a tap has just finished; out holds where it landed.
    bool update(bool touching, int x, int y, long nowMs, Tap &out);

private:
    bool down_ = false;
    bool moved_ = false;
    int x0_ = 0, y0_ = 0;
    long t0_ = 0;
};

#endif // SCREENS_H
