#ifndef HIDDEN_ROUTES_H
#define HIDDEN_ROUTES_H

// Routes the user has hidden from the board (one global set), saved in
// ~/.config/bustracker/hidden-routes, one route per line. A missing file means
// nothing is hidden.

#include <set>
#include <string>
#include <vector>

#include "departures.h"

using RouteSet = std::set<std::string>;

RouteSet parseHiddenRoutes(const std::string &text);
std::string formatHiddenRoutes(const RouteSet &routes);

// $HOME/.config/bustracker/hidden-routes
std::string hiddenRoutesPath();

// A missing file is an empty set (returns true). Returns false only if the file exists and cannot be read.
bool loadHiddenRoutes(const std::string &path, RouteSet &out);
// Creates the folder if needed. Returns false if the file cannot be written.
bool saveHiddenRoutes(const std::string &path, const RouteSet &routes);

// The board's routes in the order they first appear, from departures with a time.
// These are the chips: hidden routes stay here so they can be shown again.
std::vector<std::string> boardRoutes(const StopBoard &board);

// The rows the board shows, in order: departures with a time, minus hidden routes.
std::vector<const Departure *> visibleRows(const StopBoard &board, const RouteSet &hidden);

#endif // HIDDEN_ROUTES_H
