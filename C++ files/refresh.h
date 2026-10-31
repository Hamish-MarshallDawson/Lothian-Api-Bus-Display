#ifndef REFRESH_H
#define REFRESH_H

// Refresh policy for the departure boards. The display loop calls update(now)
// often; update() fetches only when a fetch is due. It never blocks except
// during a fetch itself (apiHttp times out after 5 s to connect, 15 s total).
//
// Policy:
//   - success (ok or no departures): next fetch in normalSecs
//   - network error: next fetch after errorSecs, doubling each failure, capped at maxErrorSecs
//   - 429 rate limited: next fetch after rateLimitSecs, doubling, capped at maxRateSecs
//   - 401/403 or no key: next fetch after keySecs (a bad key does not fix itself)
// The last good departures are kept for every stop. A failure never clears them;
// board() reports the newest status and ageSeconds() says how old the data is.

#include <functional>
#include <string>
#include <vector>

#include "departures.h"

struct RefreshPolicy {
    int normalSecs = 60;
    int errorSecs = 30;
    int maxErrorSecs = 300;
    int rateLimitSecs = 120;
    int maxRateSecs = 900;
    int keySecs = 600;
};

// Returns one board per atcocode, in the same order (see fetchStopBoards).
using BoardFetcher =
    std::function<std::vector<StopBoard>(const std::vector<std::string> &, time_t)>;

class BoardRefresher {
public:
    explicit BoardRefresher(RefreshPolicy policy = RefreshPolicy(), BoardFetcher fetch = BoardFetcher());

    // Set the stops to show (for example the main stop, or both directions).
    // Clears any board data, and the next update() fetches at once.
    void setStops(const std::vector<std::string> &atcocodes);

    // Fetches if one is due. Returns true if a request was made.
    bool update(time_t now);

    // Board for stop i. departures/name come from the last good fetch; status,
    // statusDetail and fetchedAt describe the newest attempt's outcome, and
    // status is Loading until the first fetch finishes.
    StopBoard board(size_t i) const;

    size_t count() const { return atcocodes_.size(); }

    // Seconds since stop i last had good data, or -1 if it never has.
    long ageSeconds(size_t i, time_t now) const;

    // When the next fetch is due (0 = now).
    time_t nextFetchAt() const { return nextAt_; }

    int consecutiveFailures() const { return failures_; }

private:
    RefreshPolicy policy_;
    BoardFetcher fetch_;
    std::vector<std::string> atcocodes_;
    std::vector<StopBoard> good_;   // last ok/no-departures board per stop
    BoardStatus status_ = BoardStatus::Loading;
    std::string detail_;
    int failures_ = 0;
    time_t nextAt_ = 0;
};

#endif // REFRESH_H
