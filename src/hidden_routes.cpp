#include "hidden_routes.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string &s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

RouteSet parseHiddenRoutes(const std::string &text) {
    RouteSet out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string r = trim(line);
        if (!r.empty() && r[0] != '#') out.insert(r);
    }
    return out;
}

std::string formatHiddenRoutes(const RouteSet &routes) {
    std::string out;
    for (const auto &r : routes) out += r + "\n";
    return out;
}

std::string hiddenRoutesPath() {
    const char *home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/bustracker/hidden-routes";
}

bool loadHiddenRoutes(const std::string &path, RouteSet &out) {
    out.clear();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return true;
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = parseHiddenRoutes(ss.str());
    return true;
}

bool saveHiddenRoutes(const std::string &path, const RouteSet &routes) {
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << formatHiddenRoutes(routes);
    return static_cast<bool>(f);
}

std::vector<std::string> boardRoutes(const StopBoard &board) {
    std::vector<std::string> out;
    for (const auto &d : board.departures) {
        if (!d.hasMinutes) continue;
        if (std::find(out.begin(), out.end(), d.service) == out.end()) out.push_back(d.service);
    }
    return out;
}

std::vector<const Departure *> visibleRows(const StopBoard &board, const RouteSet &hidden) {
    std::vector<const Departure *> rows;
    for (const auto &d : board.departures)
        if (d.hasMinutes && hidden.count(d.service) == 0) rows.push_back(&d);
    return rows;
}
