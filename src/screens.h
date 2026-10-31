#ifndef SCREENS_H
#define SCREENS_H

// Screen state, touch hit areas and transitions for every screen except the
// main departures board (that is drawMainScreen). Pure logic: no drawing and no
// hardware, so it builds and tests anywhere (make test).
//
// Flow: the board shows the nearest stop. Its Into/Out buttons switch direction
// straight away (the nearest stop that way). Its header opens the stop list for
// the current direction. Error and empty screens offer Choose stop, which opens
// the same list.

#include "departures.h"
#include "../gfx/theme.h"

enum class Screen { Boot, Board, Loading, StopList, Error, Empty, Asleep };

// What a tap means, per screen.
enum class Action { None, OpenStopList, PickInto, PickOut, Back, PickStop, Retry, Choose, Wake };

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
// The board's tap zones come from theme.h, the same rects the board draws into.
// Every screen has a status bar at y 0-23, then a 48 px header at y 24-71.
constexpr int kStatusBarH = 24;
constexpr Rect kHeaderBar{theme::kStopHeader.x, theme::kStopHeader.y,
                          theme::kStopHeader.w, theme::kStopHeader.h};   // board: opens the list
constexpr Rect kBoardInto{theme::kDirIntoZone.x, theme::kDirIntoZone.y,
                          theme::kDirIntoZone.w, theme::kDirIntoZone.h};  // board: into city
constexpr Rect kBoardOut{theme::kDirOutZone.x, theme::kDirOutZone.y,
                         theme::kDirOutZone.w, theme::kDirOutZone.h};     // board: out of city
// Back: the whole left part of the top chrome, status bar included, so it is easy to hit.
constexpr Rect kBackBtn{0, 0, 120, 72};
constexpr Rect kRetryBtn{16, 336, 288, 56};
constexpr Rect kChooseBtn{16, 408, 288, 56};
constexpr int kStopRowsY = 72;
constexpr int kStopRowH = 64;
constexpr int kMaxStopRows = 6;   // (480 - 72) / 64 rows fit
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
    StopDirection dir;   // Lookup: nearest stops (None, at startup) or grouped by direction
    int index;           // ShowStop: row in the stop list
    Effect(Kind k = None, StopDirection d = StopDirection::None, int i = -1)
        : kind(k), dir(d), index(i) {}
};

struct UiState {
    Screen screen = Screen::Boot;
    Screen prev = Screen::Board;              // where Back on the stop list returns to
    Screen target = Screen::Board;            // where a finished Loading goes: Board or StopList
    StopDirection dir = StopDirection::None;  // direction on the board, last picked
    ErrorReason reason = ErrorReason::None;
    int stopCount = 0;                        // rows in the current stop list
};

// A tap on the screen. arg is the stop row for PickStop.
Effect step(UiState &st, Action a, int arg);

// Results of the work the loop starts. Each returns what to do next.
// nearest stops at startup. dir is the direction that stop was classified as
// (None when it is in neither group), and the board highlights that.
Effect onBoot(UiState &st, bool ok, int count, StopDirection dir = StopDirection::None);
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
