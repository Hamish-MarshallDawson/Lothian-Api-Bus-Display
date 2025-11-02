// openmappull_main.cpp
// Thin CLI wrapper that calls getNearestStops() implemented in openmappull.cpp

#include "config.h"
#include "config_utils.h"
#include "openmappull.h"

#include <curl/curl.h>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    // Allow config.txt to override defaults in config.h
    parseConfigFile("config.txt", USER_AGENT(), CONFIG_NUM_STOPS(), CONFIG_ADDRESS());

    std::string address = CONFIG_ADDRESS();
    if(argc > 1) {
        std::ostringstream ss;
        for(int i=1;i<argc;i++) { if(i>1) ss << ' '; ss << argv[i]; }
        address = ss.str();
    }

    if(address.empty()) {
        std::cerr << "Address is empty (set 'address=' in config.txt or pass an address on the CLI)\n";
        curl_global_cleanup();
        return 3;
    }

    std::vector<BusStopInfo> stops;
    if(!getNearestStops(address, CONFIG_NUM_STOPS(), stops)) {
        std::cerr << "Failed to get nearest stops\n";
        curl_global_cleanup();
        return 4;
    }

    std::cout << "Nearest " << stops.size() << " stops:\n";
    for(size_t i=0;i<stops.size();++i) {
        std::cout << (i+1) << ") " << stops[i].stopName << " (stop_id=" << stops[i].stopID << ")\n";
        std::cout << "    coords: " << stops[i].latitude << ", " << stops[i].longitude << "\n";
    }

    curl_global_cleanup();
    return 0;
}
