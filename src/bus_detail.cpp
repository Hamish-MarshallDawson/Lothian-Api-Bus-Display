#include "bus_detail.h"

#include <algorithm>
#include <cmath>

#include "../gfx/fonts/faces.h"
#include "../gfx/text.h"
#include "../gfx/theme.h"
#include "../gfx/widgets.h"

using gfx::Canvas;
using gfx::Color;
using gfx::FontFace;
using gfx::Rect;
using namespace theme;
namespace F = gfx::fonts;

namespace {

// Lateness chip colours: late (WARN on a warm tint), on time (green), early (blue)
Color lateInk(int late) { return late > 1 ? WARN : (late < -1 ? ALERT_FG : rgb(0x2E, 0x7D, 0x32)); }
Color lateFill(int late) { return late > 1 ? rgb(0xFB, 0xF0, 0xDC) : (late < -1 ? ALERT_BG : rgb(0xE8, 0xF5, 0xE9)); }

std::string lateText(int late) {
    if (late > 1) return std::to_string(late) + " min late";
    if (late < -1) return std::to_string(-late) + " min early";
    return "On time";
}

void drawHeader(Canvas &c, const BusDetailView &v) {
    // Back zone: 48x48 at the left, chevron in it
    drawChevron(c, 24 - 3, 48, ACCENT, false);
    gfx::drawRoutePill(c, Rect{56, 37, PILL_W, PILL_H}, v.service.substr(0, 3), v.routeFill, F::kBold16);
    const FontFace &f = F::kBold18;
    const std::string to = v.destination.empty() ? "" : "to " + v.destination;
    const int x = 56 + PILL_W + 10;
    gfx::drawText(c, f, x, gfx::centredBaseline(24, 48, f), gfx::fitText(f, to, SCREEN_W - MARGIN - x), TEXT);
}

}  // namespace

gfx::MapScene detailMapScene(const BusDetailView &v) {
    gfx::MapScene scene;
    scene.rect = Rect{0, 72, SCREEN_W, 200};

    std::vector<gfx::GeoPoint> pts;
    for (const auto &s : v.stops) pts.push_back(gfx::GeoPoint{s.lat, s.lon});
    if (v.hasBus) pts.push_back(gfx::GeoPoint{v.busLat, v.busLon});
    if (pts.empty()) pts.push_back(gfx::GeoPoint{55.9533, -3.1883});  // city centre, nothing else known
    gfx::fitScene(scene, pts, 18);

    // The trip's route, in order
    if (v.stops.size() > 1) {
        gfx::MapPath path;
        path.color = ACCENT;
        path.width = 4;
        for (const auto &s : v.stops) path.points.push_back(gfx::GeoPoint{s.lat, s.lon});
        scene.paths.push_back(path);
    }
    // Stops: small rings; mine is larger, the next one filled
    for (size_t i = 0; i < v.stops.size(); ++i) {
        const auto &s = v.stops[i];
        gfx::MapMarker m{gfx::GeoPoint{s.lat, s.lon}, BG, ACCENT, 4, ""};
        if (static_cast<int>(i) == v.myStop) m = gfx::MapMarker{m.at, BG, ACCENT, 8, ""};
        if (static_cast<int>(i) == v.nextStop) m = gfx::MapMarker{m.at, ACCENT, ACCENT, 6, ""};
        scene.markers.push_back(m);
    }
    // The bus: route-coloured disc with a white border and its number
    if (v.hasBus) {
        const std::string label = v.service.size() <= 3 ? v.service : "";
        scene.markers.push_back(gfx::MapMarker{gfx::GeoPoint{v.busLat, v.busLon}, v.routeFill,
                                               rgb(0xFF, 0xFF, 0xFF), 10, label});
    }
    return scene;
}

namespace {

void drawMapPanel(Canvas &c, const BusDetailView &v, gfx::TileStore &tiles) {
    gfx::drawMap(c, detailMapScene(v), tiles);
}

void drawInfo(Canvas &c, const BusDetailView &v) {
    const int top = 272;
    std::string head, sub;
    switch (v.state) {
    case DetailState::NotStarted:
        head = "Not started yet";
        sub = "Leaves " + v.originName + " at " + v.originTime;
        break;
    case DetailState::Finished:
        head = "Gone past this stop";
        break;
    case DetailState::Unavailable:
        head = "Bus position unavailable";
        break;
    default:
        head = v.nextStop >= 0 && v.nextStop < static_cast<int>(v.stops.size())
                   ? "Approaching " + v.stops[v.nextStop].name
                   : "On its way";
        if (v.stopsAway >= 0)
            sub = std::to_string(v.stopsAway) + (v.stopsAway == 1 ? " stop away" : " stops away");
        if (v.dueMin >= 0) sub += (sub.empty() ? "" : "  -  ") + std::string("due in ") + std::to_string(v.dueMin) + " min";
        break;
    }
    const FontFace &hf = F::kBold18;
    gfx::drawText(c, hf, MARGIN, top + 20, gfx::fitText(hf, head, SCREEN_W - 2 * MARGIN), TEXT);
    if (!sub.empty())
        gfx::drawText(c, F::kRegular15, MARGIN, top + 38, gfx::fitText(F::kRegular15, sub, SCREEN_W - 2 * MARGIN), TEXT_MUTED);

    // Third line: lateness chip on the left; updated / stuck on the right
    const bool live = v.state == DetailState::Live;
    if (live || v.state == DetailState::Stale) {
        const std::string t = lateText(v.lateMin);
        const FontFace &cf = F::kSemiBold14;
        const int w = gfx::textWidth(cf, t) + 18;
        const Rect chip{MARGIN, top + 46, w, 22};
        c.fillRoundRect(chip, 11, lateFill(v.lateMin));
        gfx::drawTextIn(c, cf, chip, gfx::centredBaseline(chip.y, chip.h, cf), gfx::Align::Center, t, lateInk(v.lateMin));
    }
    const FontFace &rf = F::kRegular13;
    if (v.stuck) {
        const std::string t = "Not moved for " + std::to_string(std::max<long>(1, v.stuckForS / 60)) + " min";
        gfx::drawTextIn(c, rf, Rect{0, 0, SCREEN_W - MARGIN, 0}, top + 61, gfx::Align::Right, t, DANGER);
    } else if (v.state == DetailState::Stale) {
        const std::string t = "Last seen " + std::to_string(std::max<long>(0, v.lastUpdateAgeS) / 60) + " min ago";
        gfx::drawTextIn(c, rf, Rect{0, 0, SCREEN_W - MARGIN, 0}, top + 61, gfx::Align::Right, t, TEXT_MUTED);
    } else if (live && !v.lateTrend.empty()) {
        // The lateness history: "late 4 -> 6 min over 3 min"
        gfx::drawTextIn(c, rf, Rect{0, 0, SCREEN_W - MARGIN, 0}, top + 61, gfx::Align::Right, v.lateTrend, TEXT_MUTED);
    } else if (live && v.lastUpdateAgeS >= 0) {
        const std::string t = "Updated " + std::to_string(v.lastUpdateAgeS) + " s ago";
        gfx::drawTextIn(c, rf, Rect{0, 0, SCREEN_W - MARGIN, 0}, top + 61, gfx::Align::Right, t, TEXT_MUTED);
    }
}

void drawNextStops(Canvas &c, const BusDetailView &v) {
    const int top = 344;
    c.hline(0, SCREEN_W - 1, top - 1, DIVIDER);
    if (v.stops.empty() || v.myStop < 0) return;

    // From the approaching stop (or the origin) to my stop; cut to fit with the last one kept
    const int from = std::max(0, (v.nextStop >= 0 ? v.nextStop : 0) - 1);  // one stop back, for context
    std::vector<int> idx;
    for (int i = from; i <= v.myStop && i < static_cast<int>(v.stops.size()); ++i) idx.push_back(i);
    const int rowH = 26, maxRows = 5;
    if (static_cast<int>(idx.size()) > maxRows) {
        std::vector<int> cut(idx.begin(), idx.begin() + (maxRows - 1));
        cut.push_back(idx.back());
        idx = cut;
    }

    const int x = 22;
    const int firstY = top + 13, lastY = firstY + rowH * (static_cast<int>(idx.size()) - 1);
    if (idx.size() > 1) c.fillRect(Rect{x - 1, firstY, 3, lastY - firstY}, ACCENT);  // the line

    for (size_t k = 0; k < idx.size(); ++k) {
        const int i = idx[k];
        const int cy = firstY + static_cast<int>(k) * rowH;
        const bool mine = i == v.myStop;
        if (mine) c.fillRect(Rect{0, cy - 12, SCREEN_W, 24}, ACCENT_SOFT);
        const bool passed = v.state != DetailState::NotStarted && i < v.nextStop;
        if (i == v.nextStop) c.fillRoundRect(Rect{x - 5, cy - 5, 10, 10}, 5, ACCENT);
        else c.fillRoundRect(Rect{x - 4, cy - 4, 8, 8}, 4, passed ? TEXT_MUTED : ACCENT);
        if (!passed && i != v.nextStop) c.fillRoundRect(Rect{x - 2, cy - 2, 4, 4}, 2, BG);

        const FontFace &nf = mine ? F::kBold16 : F::kRegular15;
        const std::string &name = v.stops[i].name;
        const int nameMax = SCREEN_W - 36 - 12 - 60;
        gfx::drawText(c, nf, 38, cy + 5, gfx::fitText(nf, name, nameMax), mine ? TEXT : TEXT_MUTED);
        gfx::drawTextIn(c, F::kSemiBold14, Rect{0, 0, SCREEN_W - MARGIN, 0}, cy + 5, gfx::Align::Right,
                        v.stops[i].scheduled, mine ? TEXT : TEXT_MUTED);
    }
}

}  // namespace

void drawBusDetail(Canvas &c, const BusDetailView &v, const gfx::StatusBarText &status, gfx::TileStore &tiles) {
    c.clear(BG);
    gfx::drawStatusBar(c, status);
    drawHeader(c, v);
    drawMapPanel(c, v, tiles);
    drawInfo(c, v);
    drawNextStops(c, v);
}

BusDetailView detailFromTrack(const BusTrack &t, gfx::Color routeFill) {
    BusDetailView v;
    v.service = t.service;
    v.routeFill = routeFill;
    v.destination = t.destination;
    switch (t.state) {
    case TrackState::NotStarted: v.state = DetailState::NotStarted; break;
    case TrackState::Live:       v.state = DetailState::Live; break;
    case TrackState::Stale:      v.state = DetailState::Stale; break;
    case TrackState::Finished:   v.state = DetailState::Finished; break;
    case TrackState::Unavailable: v.state = DetailState::Unavailable; break;
    }
    for (const auto &s : t.stops) v.stops.push_back(DetailStop{s.name, s.scheduled, s.lat, s.lon});
    v.myStop = t.myStopIndex;
    v.nextStop = t.nextStopIndex;
    v.hasBus = t.state == TrackState::Live || t.state == TrackState::Stale;
    v.busLat = t.busLat;
    v.busLon = t.busLon;
    v.lastUpdateAgeS = t.lastUpdateAgeS;
    v.lateMin = t.lateMin;
    v.stopsAway = t.stopsAway;
    v.dueMin = t.dueMin;
    v.stuck = t.stuck;
    v.stuckForS = t.stuckForS;
    // Lateness over the last 3 minutes of observations, from the earliest point in that window
    if (t.lateHistory.size() >= 2) {
        const time_t last = t.lateHistory.back().first;
        const int lateNow = t.lateHistory.back().second;
        int lateThen = lateNow;
        for (auto it = t.lateHistory.rbegin(); it != t.lateHistory.rend() && it->first >= last - 180; ++it)
            lateThen = it->second;
        if (lateThen != lateNow)
            v.lateTrend = "late " + std::to_string(lateThen) + " -> " + std::to_string(lateNow) + " min over 3 min";
    }
    if (t.state == TrackState::NotStarted && !t.stops.empty()) {
        v.originName = t.stops.front().name;
        v.originTime = t.stops.front().scheduled;
    }
    return v;
}
