#pragma once

extern "C" {
#include "../C/lib/GUI/GUI_Paint.h"
}

#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "busStopInfo.h"
#include "openmappull.h"

// Shared UI state and display API
enum class DirectionChoice { None, IntoCity, OutOfCity };

extern DirectionChoice g_dirChoice;
extern GroupedStops g_groupedStops;
extern std::vector<BusStopInfo> g_dropdownStops;
extern bool g_showDirectionModal;
extern std::string g_selectedStopID;

// Display functions implemented in renderDisplay.cpp
void displayBuffer(UWORD *image);
void drawBusInfo(const std::string& routeNum, const std::string& destination,
                 const std::string& timeStr, bool isLive, int yPos);
void drawDirectionChoiceModal();
void drawDropdownFromGrouped();
// Draw the main bus times screen into the provided Paint image buffer.
// - image: Paint buffer allocated in busTrack (`BlackImage`)
// - currentStop: the BusStopInfo currently selected
// - busData: parsed JSON response from the live-times API
void drawMainScreen(UWORD *image, const BusStopInfo &currentStop, const nlohmann::json &busData);
