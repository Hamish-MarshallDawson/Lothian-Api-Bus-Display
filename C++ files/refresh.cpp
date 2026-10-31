// refresh.cpp: see refresh.h for the policy.

#include "refresh.h"

#include <algorithm>
#include <ctime>
#include <iostream>

static bool isGood(BoardStatus s) {
    return s == BoardStatus::Ok || s == BoardStatus::NoDepartures;
}

// base * 2^(n-1), capped
static int backoff(int base, int cap, int failures) {
    long long w = base;
    for (int i = 1; i < failures && w < cap; ++i) w *= 2;
    return static_cast<int>(std::min<long long>(w, cap));
}

BoardRefresher::BoardRefresher(RefreshPolicy policy, BoardFetcher fetch)
    : policy_(policy), fetch_(fetch ? fetch : BoardFetcher(fetchStopBoards)) {}

void BoardRefresher::setStops(const std::vector<std::string> &atcocodes) {
    atcocodes_ = atcocodes;
    good_.clear();
    for (const auto &a : atcocodes_) {
        StopBoard b;
        b.atcocode = a;
        good_.push_back(b);
    }
    status_ = BoardStatus::Loading;
    detail_.clear();
    failures_ = 0;
    nextAt_ = 0;
}

bool BoardRefresher::update(time_t now) {
    if (atcocodes_.empty() || now < nextAt_) return false;

    std::vector<StopBoard> got = fetch_(atcocodes_, now);
    if (got.size() != atcocodes_.size()) {
        got.assign(atcocodes_.size(), StopBoard());
        for (auto &b : got) b.status = BoardStatus::NetworkError;
        got[0].statusDetail = "fetcher returned the wrong number of boards";
    }

    status_ = got[0].status;
    detail_ = got[0].statusDetail;
    for (size_t i = 0; i < got.size(); ++i) {
        if (isGood(got[i].status)) good_[i] = got[i];
    }

    int wait;
    if (isGood(status_)) {
        failures_ = 0;
        wait = policy_.normalSecs;
    } else {
        failures_++;
        switch (status_) {
            case BoardStatus::RateLimited:
                wait = backoff(policy_.rateLimitSecs, policy_.maxRateSecs, failures_);
                break;
            case BoardStatus::KeyRejected:
                wait = policy_.keySecs;
                break;
            default:  // network error
                wait = backoff(policy_.errorSecs, policy_.maxErrorSecs, failures_);
                break;
        }
        std::cerr << "refresh: " << boardStatusName(status_) << " (" << detail_
                  << "), next try in " << wait << " s" << std::endl;
    }
    nextAt_ = now + wait;
    return true;
}

StopBoard BoardRefresher::board(size_t i) const {
    StopBoard b = good_.at(i);
    b.status = status_;
    b.statusDetail = detail_;
    if (atcocodes_.empty()) b.status = BoardStatus::Loading;
    return b;
}

long BoardRefresher::ageSeconds(size_t i, time_t now) const {
    const StopBoard &b = good_.at(i);
    if (b.fetchedAt == 0) return -1;
    return static_cast<long>(now - b.fetchedAt);
}
