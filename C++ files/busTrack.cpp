/*
 * Bus Tracking Program for Lothian Buses
 * Displays real-time bus times for Heriot-Watt University
 */

#include <iostream>
#include <string>        
#include <vector>       // For storing multiple bus stops/services
#include <iomanip>      
//#include <curl/curl.h>   // For HTTP API calls (install libcurl later)
//#include <json/json.h>   // For JSON parsing (install jsoncpp later)

#include "busStopInfo.cpp"  // Include the bus stop data from busStopInfo.cpp
// file kept private to keep data and API key quiet :)


// Function to display bus stop information
void displayBusStopInfo(const BusStopInfo& busStop) {
    std::cout << "Bus Stop Info" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Stop ID:    " << busStop.stopID << std::endl;
    std::cout << "Stop Name:  " << busStop.stopName << std::endl;
    std::cout << "Direction:  " << busStop.direction << std::endl;
    std::cout << "Location:   (" << busStop.latitude << ", " << busStop.longitude << ")" << std::endl;
    std::cout << "Destination: " << busStop.destination << std::endl;
    std::cout << "Services:   " << busStop.services << std::endl;
    std::cout << "----------------------------------------\n" << std::endl;
}

// TODO: Function to fetch live bus times from API
/*
void getLiveTimes(const std::string& stopID) {
}
*/

// TODO: Function to parse JSON response
/*
void parseAPIResponse(const std::string& jsonResponse) {
}
*/

// TODO: Function to display bus times on screen
/*
void displayScreenInfo(const BusStopLiveTimes& liveTimes) {
}
*/

int main() {
    std::cout << "Lothian Bus Display - Starting..." << std::endl;
    std::cout << "---------------------------------\n" << std::endl;

    // Create an instance of your bus stop (Angle Park Terrace)
    BusStopInfo busStop;

    // Display the bus stop information
    displayBusStopInfo(busStop);

    // Just showing when the data is loaded
    std::cout << "Bus stop data loaded!!!" << std::endl;
    std::cout << "Monitoring services:" << busStop.services << std::endl;
    std::cout << "Heading to:" << busStop.destination << std::endl;
    
    // Next steps (Mock data)
    // test with fake data so I can make sure its actually working 
    // before we do the real API calls
    std::cout << "\nAdd mock bus arrival data" << std::endl;
    std::cout << "  Example: Service 34 - 5 minutes" << std::endl;
    std::cout << "           Service 35 - 12 minutes" << std::endl;
    
    // trying display but can't test till with actual pi hardware
    std::cout << "\n Integrate with 3.5\" display" << std::endl;
    std::cout << "  Requires: Raspberry Pi hardware and display drivers" << std::endl;

    return 0;
}