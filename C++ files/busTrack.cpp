/*
 * Bus Tracking Program for Lothian Buses
 * Displays real-time bus times for Heriot-Watt University
 */

#include <iostream>
#include <string>        
#include <vector>       // For storing multiple bus stops/services
#include <iomanip>      
#include <curl/curl.h>   // For HTTP API calls (install libcurl later)
#include <nlohmann/json.hpp>   // For JSON parsing (install jsoncpp later)

//making it easier to use nlohmann json library
using json = nlohmann::json;

#include "busStopInfo.cpp"  // Include the bus stop data from busStopInfo.cpp
// file kept private to keep data and API key quiet :)


//function called when curl fetches data
//contents is data that was fetched
//size is how many bytes arrived
//nmemb is number of such bytes
//userp is pointer to string where we will store data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

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

std::string buildAPIUrl(const BusAPIConfig& config, const std::string& endpoint, const std::string& parameter =""){
    return config.baseURL + endpoint + parameter;
}

std::string fetchLiveBusTimes(const std::string& stopID){
    //points to curl object
    CURL* curl; 
    //Result from curl, error or not
    CURLcode res;
    //where response is stored
    std::string response;

    //initialize curl
    curl = curl_easy_init();

    //works like bool, if curl initialized properly
    if(curl) {
    

        BusAPIConfig apiConf;

        //gets live bus time from func above
        std::string url = buildAPIUrl(apiConf, apiConf.liveBusTimesEndpoint, stopID);

        //tells curl what url to use 
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // points to my write callback function
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        // stores the data in response string
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // actually does the call
        res = curl_easy_perform(curl);

        // error checking
        if(res != CURLE_OK) {
            //if error happens prints to console what issues happened
            std::cerr << "Error fetching bus times: " << curl_easy_strerror(res) << std::endl;
        }

        // frees up memory
        curl_easy_cleanup(curl);
    }

    return response;
}



// TODO: Function to parse JSON response
/*
void parseAPIResponse(const std::string& jsonResponse) {
}
*/

void parseAPIResponse(const std::string& jsonResponse) {
    try {

        json busData = json::parse(jsonResponse);
        // The API returns an array of routes
        for(auto& route : busData) {
            std::string routeName = route["routeName"];
            std::cout << "Route: " << routeName << std::endl;
            
            // Each route has departures
            for(auto& departure : route["departures"]) {
                std::string destination = departure["destination"];
                std::string displayTime = departure["displayTime"];
                bool isLive = departure["isLive"];
                
                std::cout << "  -> " << destination 
                          << " at " << displayTime
                          << " (Live GPS: " << (isLive ? "Yes" : "No") << ")"
                          << std::endl;
            }
        }
    }
    catch(json::exception& e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
    }
}
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

    // Test API call
    std::cout << "\n=== Testing Live API Call ===" << std::endl;
    std::cout << "Fetching live bus times for stop: " << busStop.stopID << std::endl;

    std::string apiResponse = fetchLiveBusTimes(busStop.stopID);

    if(!apiResponse.empty()) {
        std::cout << "\nAPI Response received! Parsing data...\n" << std::endl;
        parseAPIResponse(apiResponse);
    } else {
        std::cout << "No response from API (check network connection)" << std::endl;
    }

    return 0;
}