#include "screens.h"

#include <cstdlib>

namespace {

bool isBoardLike(Screen s) { return s == Screen::Board || s == Screen::Error || s == Screen::Empty; }

// Refresh the board, or look the stops up again when the stop list is what failed.
Effect refresh(UiState &st) {
    if (st.reason == ErrorReason::NoStops) {
        st.screen = Screen::Loading;
        return Effect{Effect::Lookup, st.dir, -1};
    }
    return Effect{Effect::FetchBoard, StopDirection::None, -1};
}

Effect toError(UiState &st, ErrorReason r) {
    st.screen = Screen::Error;
    st.reason = r;
    return Effect{};
}

} // namespace

int listHits(Screen s, int stopCount, Hit *out, int max) {
    int n = 0;
    auto add = [&](Rect r, Action a, int arg) {
        if (n < max) out[n] = Hit{r, a, arg};
        ++n;
    };
    switch (s) {
    case Screen::Board:
        add(kHeaderBar, Action::OpenDirection, 0);
        break;
    case Screen::Direction:
        add(kBackBtn, Action::Back, 0);
        add(kDirInto, Action::PickInto, 0);
        add(kDirOut, Action::PickOut, 0);
        break;
    case Screen::StopList: {
        add(kBackBtn, Action::Back, 0);
        int rows = stopCount < kMaxStopRows ? stopCount : kMaxStopRows;
        for (int i = 0; i < rows; ++i) add(stopRow(i), Action::PickStop, i);
        break;
    }
    case Screen::Error:
    case Screen::Empty:
        add(kRetryBtn, Action::Retry, 0);
        add(kChooseBtn, Action::Choose, 0);
        break;
    case Screen::Asleep:
        add(Rect{0, 0, kScreenW, kScreenH}, Action::Wake, 0);
        break;
    case Screen::Boot:
    case Screen::Loading:
        break;
    }
    return n < max ? n : max;
}

Action hitTest(Screen s, int stopCount, int x, int y, int &arg) {
    Hit hits[kMaxStopRows + 2];
    int n = listHits(s, stopCount, hits, kMaxStopRows + 2);
    for (int i = 0; i < n; ++i) {
        if (hits[i].rect.contains(x, y)) {
            arg = hits[i].arg;
            return hits[i].action;
        }
    }
    return Action::None;
}

Effect step(UiState &st, Action a, int arg) {
    switch (st.screen) {
    case Screen::Board:
        if (a == Action::OpenDirection) {
            st.prev = Screen::Board;
            st.screen = Screen::Direction;
        }
        break;

    case Screen::Error:
    case Screen::Empty:
        if (a == Action::Retry) return refresh(st);
        if (a == Action::Choose) {
            st.prev = st.screen;
            st.screen = Screen::Direction;
        }
        break;

    case Screen::Direction:
        if (a == Action::Back) {
            st.screen = st.prev;
        } else if (a == Action::PickInto || a == Action::PickOut) {
            st.dir = a == Action::PickInto ? StopDirection::Into : StopDirection::Out;
            st.screen = Screen::Loading;
            return Effect{Effect::Lookup, st.dir, -1};
        }
        break;

    case Screen::StopList:
        if (a == Action::Back) {
            st.screen = Screen::Direction;
        } else if (a == Action::PickStop && arg >= 0 && arg < st.stopCount) {
            st.screen = Screen::Board;
            st.reason = ErrorReason::None;
            return Effect{Effect::ShowStop, StopDirection::None, arg};
        }
        break;

    case Screen::Asleep:
        if (a == Action::Wake) {
            st.screen = Screen::Board;
            return Effect{Effect::FetchBoard, StopDirection::None, -1};
        }
        break;

    case Screen::Boot:
    case Screen::Loading:
        break;
    }
    return Effect{};
}

Effect onBoot(UiState &st, bool ok, int count) {
    if (ok && count > 0) {
        st.screen = Screen::Board;
        st.stopCount = count;
        return Effect{Effect::FetchBoard, StopDirection::None, -1};
    }
    st.dir = StopDirection::None;
    return toError(st, ErrorReason::NoStops);
}

Effect onLookup(UiState &st, bool ok, int count) {
    if (st.screen != Screen::Loading) return Effect{};
    if (!ok) return toError(st, ErrorReason::NoNetwork);
    if (count <= 0) return toError(st, ErrorReason::NoStops);
    st.screen = Screen::StopList;
    st.stopCount = count;
    st.reason = ErrorReason::None;
    return Effect{};
}

Effect onBoard(UiState &st, BoardStatus status) {
    if (!isBoardLike(st.screen)) return Effect{};
    switch (status) {
    case BoardStatus::Ok:
    case BoardStatus::Loading:
        st.screen = Screen::Board;
        st.reason = ErrorReason::None;
        break;
    case BoardStatus::NoDepartures:
        st.screen = Screen::Empty;
        st.reason = ErrorReason::None;
        break;
    case BoardStatus::NetworkError:
        return toError(st, ErrorReason::NoNetwork);
    case BoardStatus::KeyRejected:
        return toError(st, ErrorReason::KeyRejected);
    case BoardStatus::RateLimited:
        return toError(st, ErrorReason::RateLimited);
    }
    return Effect{};
}

Effect onMinute(UiState &st) {
    if (!isBoardLike(st.screen)) return Effect{};
    return refresh(st);
}

Effect onIdle(UiState &st) {
    if (!isBoardLike(st.screen)) return Effect{};   // menus stay up until the user leaves them
    st.screen = Screen::Asleep;
    return Effect{Effect::Sleep, StopDirection::None, -1};
}

bool TapTracker::update(bool touching, int x, int y, long nowMs, Tap &out) {
    if (touching && !down_) {
        down_ = true;
        moved_ = false;
        x0_ = x;
        y0_ = y;
        t0_ = nowMs;
        return false;
    }
    if (touching) {
        if (std::abs(x - x0_) > kTapSlop || std::abs(y - y0_) > kTapSlop) moved_ = true;
        return false;
    }
    if (!down_) return false;
    down_ = false;
    if (moved_ || nowMs - t0_ > kTapMaxMs) return false;
    out = Tap{x0_, y0_};
    return true;
}
