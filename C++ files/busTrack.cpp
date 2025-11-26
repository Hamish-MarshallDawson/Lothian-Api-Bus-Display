/*
 * Bus Tracking Program for Lothian Buses
 * Displays real-time bus times for Heriot-Watt University
 */


extern "C" {
    #include "../C/lib/Config/DEV_Config.h"
    #include "../C/lib/lcd/st7796.h"
    #include "../C/lib/GUI/GUI_Paint.h"
    #include "../C/lib/Fonts/fonts.h"
    #include "../C/lib/lcd/ft6336u.h"
}

#include <iostream>
#include <string>      
// For storing multiple bus stops/services  
#include <vector>  
// For std::sort     
#include <algorithm>    
#include <iomanip>
// For malloc/free
#include <cstdlib> 
// For uint32_t     
#include <cstdint>
 // For time functions      
#include <ctime> 
// For HTTP API calls (install libcurl later)      
#include <curl/curl.h>   
// For JSON parsing
#include <nlohmann/json.hpp>   

//making it easier to use nlohmann json library
using json = nlohmann::json;

// Use shared headers for stop info and lookup
#include "busStopInfo.h"
#include "openmappull.h"    // for GroupedStops, getNearestStopsGrouped
#include "config.h"         // access to CONFIG_ADDRESS(), CITY_CENTER_LAT/LON if needed

// Display API and shared UI state (implemented in renderDisplay.cpp)
#include "renderDisplay.h"


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
//to terminal, debug purpose primarily.
// void displayBusStopInfo(const BusStopInfo& busStop) {
//     std::cout << "Bus Stop Info" << std::endl;
//     std::cout << "----------------------------------------" << std::endl;
//     std::cout << "Stop ID:    " << busStop.stopID << std::endl;
//     std::cout << "Stop Name:  " << busStop.stopName << std::endl;
//     std::cout << "Direction:  " << busStop.direction << std::endl;
//     std::cout << "Location:   (" << busStop.latitude << ", " << busStop.longitude << ")" << std::endl;
//     std::cout << "Destination: " << busStop.destination << std::endl;
//     std::cout << "Services:   " << busStop.services << std::endl;
//     std::cout << "----------------------------------------\n" << std::endl;
// }

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

        // Use the global API config
        BusAPIConfig &apiConf = API_CONFIG();

        // build URL for live times
        std::string url = buildAPIUrl(apiConf, apiConf.liveBusTimesEndpoint, stopID);

        //tells curl what url to use 
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // points to my write callback function
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        // stores the data in response string
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // set user agent and actually do the call
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT().c_str());
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

// draw functions (implemented in renderDisplay.cpp)


int main() {
    std::cout << "Lothian Bus Display Starting" << std::endl;
    // dynamic stops returned by openmappull -> now use BusStopInfo
    std::vector<BusStopInfo> dynStops;
    bool haveDynamicStops = false;

    // Option A: use CONFIG_ADDRESS() and CONFIG_NUM_STOPS()
    std::string address = CONFIG_ADDRESS();
    int wanted = CONFIG_NUM_STOPS();

    if(!address.empty()) {
        std::cout << "Fetching nearest stops for address: " << address << std::endl;
        if(getNearestStops(address, wanted, dynStops) && !dynStops.empty()) {
            haveDynamicStops = true;
            std::cout << "Got " << dynStops.size() << " dynamic stops\n";
        } else {
            std::cerr << "Failed to fetch dynamic stops, falling back to static list\n";
        }
    } else {
        std::cerr << "No address in config; not fetching dynamic stops\n";
    }

    // Convert dynStops to the BusStopInfo / local struct busTrack expects,
    // or change busTrack to use BusStop (preferred).
    std::vector<BusStopInfo> busStops;
    if(haveDynamicStops) {
        busStops = dynStops; // already BusStopInfo
    } else {
        // Fallback to built-in static stops
        busStops = getAllBusStops();
    }

    // Index of current stop shown on screen
    int currentBusStopIndex = 0;

    // Choose the nearest stop at startup (preferred behavior)
    {
        std::vector<BusStopInfo> nearest;
        if (getNearestStops(CONFIG_ADDRESS(), 1, nearest) && !nearest.empty()) {
            g_selectedStopID = nearest.front().stopID;
            for (size_t i = 0; i < busStops.size(); ++i) {
                if (busStops[i].stopID == g_selectedStopID) {
                    currentBusStopIndex = i;
                    break;
                }
            }
        }
    }

    std::cout << "Loaded " << busStops.size() << " bus stops" << std::endl;
    // std::cout << "Current stop: " << busStops[currentBusStopIndex].stopName << std::endl;
    

    // Start display
    if(DEV_ModuleInit() != 0) {
        std::cerr << "Failed to initialize display" << std::endl;
        return 1;
    }
    

    //std::cout << "Display module initialized" << std::endl;
    
    //intialize display
    st7796_init();
    st7796_clear(BLACK);
    //initialize touch display
    ft6336u_init();

    // Prefetch nearest/grouped stops to warm caches and geocoding so the
    // first dropdown/modal interaction is fast. This calls into openmappull
    // which in turn populates its internal stops cache on first use.
    {
        int perGroup = CONFIG_NUM_STOPS();
        GroupedStops pre;
        std::cerr << "Prefetching grouped stops (perGroup=" << perGroup << ")..." << std::endl;
        bool ok = getNearestStopsGrouped(CONFIG_ADDRESS(), perGroup, pre);
        if (ok) {
            std::cerr << "Prefetch complete: into=" << pre.intoCity.size() << " out=" << pre.outOfCity.size() << std::endl;
            // store into the shared global so future calls are instant
            g_groupedStops = pre;
        } else {
            std::cerr << "Prefetch failed or network unavailable; will fetch on demand." << std::endl;
        }
    }

    touch_data_t touch_data;
    
    // Initialize Paint library with image buffer
    // Display is 320x480, allocate memory for it
    UWORD *BlackImage;
    // Use 32-bit to avoid overflow, caused issue in past
    uint32_t Imagesize = ((uint32_t)ST7796_WIDTH) * ((uint32_t)ST7796_HEIGHT);  
    if((BlackImage = (UWORD *)malloc(Imagesize * sizeof(UWORD))) == NULL) {
        std::cerr << "Failed to allocate memory for image buffer" << std::endl;
        DEV_ModuleExit();
        return 1;
    }
    //more debugging stuff
    // std::cout << "Image buffer allocated (" << Imagesize << " pixels = " 
    //           << (Imagesize * sizeof(UWORD) / 1024) << " KB)" << std::endl;
    
    // Initialize Paint with the buffer
    Paint_NewImage(BlackImage, ST7796_WIDTH, ST7796_HEIGHT, 0, BLACK, 16);
    // Fix text orientation
    Paint_SetMirroring(MIRROR_HORIZONTAL);  
    Paint_Clear(BLACK);
    
    //
    // std::cout << "Paint library initialized (Width: " << ST7796_WIDTH 
    //           << ", Height: " << ST7796_HEIGHT << ", Mirrored)" << std::endl;
    
    // Display welcome message
    std::cout << "Showing welcome message..." << std::endl;
    Paint_DrawString_EN(70, 200, "Bus Tracker", &Font24, WHITE, WHITE);
    Paint_DrawString_EN(70, 230, "Starting...", &Font20, WHITE, WHITE);
    displayBuffer(BlackImage);
    DEV_Delay_ms(1000);

    
   // std::cout << "Bus stop: " << busStop.stopName << " (ID: " << busStop.stopID << ")" << std::endl;

   //decides whether to show dropdown menu
   bool showDropDownMenu = false;
   int autoTurnOff = 0;  // Track idle time across loop iterations
   
    // Main loop
    while(true) {

        if(!showDropDownMenu) {
            std::cout << "\n=== Fetching bus data for stop: " << busStops[currentBusStopIndex].stopName 
                      << " (ID: " << busStops[currentBusStopIndex].stopID << ") ===" << std::endl;

            // Fetch bus data using func defined above
            std::string apiResponse = fetchLiveBusTimes(busStops[currentBusStopIndex].stopID);

             // std::string apiResponse = fetchLiveBusTimes(busStop.stopName);
        
        if(!apiResponse.empty()) {
            std::cout << "API response received (" << apiResponse.length() << " bytes), parsing..." << std::endl;
            
            // Debug: Print first 200 characters of response
            std::cout << "Response preview: " << apiResponse.substr(0, std::min(size_t(200), apiResponse.length())) << "..." << std::endl;
            
            // Parse JSON
            try {
                json busData = json::parse(apiResponse);
                std::cout << "JSON parsed successfully, calling drawMainScreen..." << std::endl;
                // Delegate drawing to renderDisplay
                drawMainScreen(BlackImage, busStops[currentBusStopIndex], busData);
            } catch(const json::exception& e) {
                std::cerr << "JSON error: " << e.what() << std::endl;
                // Show error on display
                st7796_clear(BLACK);
                Paint_DrawString_EN(10, 200, "API Error", &Font24, RED, BLACK);
                displayBuffer(BlackImage);
            }
        } else {
            std::cerr << "No API response received" << std::endl;
            
            // Show error on display
            st7796_clear(BLACK);
            Paint_DrawString_EN(10, 200, "No Data", &Font24, RED, BLACK);
        }
        }

            // If menu should be shown, draw it on top
        if(showDropDownMenu) {
            // Clear and redraw with menu
            Paint_Clear(GRAY);

            // Compute how many rows fit on screen (start Y=50, row height=50)
            const int rowY0 = 50;
            const int rowH = 50;
            size_t maxRows = busStops.size();
            if (ST7796_HEIGHT > rowY0) {
                size_t fit = (ST7796_HEIGHT - rowY0) / rowH;
                if (fit < maxRows) maxRows = fit;
            } else {
                maxRows = 0;
            }

            // If a direction modal is requested, show that instead of the list
            if (g_showDirectionModal) {
                drawDirectionChoiceModal();
                displayBuffer(BlackImage);
            } else {
                // Draw menu items (limited to what fits)
                bool usingGrouped = !g_dropdownStops.empty();
                size_t sourceSize = usingGrouped ? g_dropdownStops.size() : busStops.size();
                size_t rowsToDraw = std::min(maxRows, sourceSize);

                if (rowsToDraw == 0) {
                    Paint_DrawString_EN(20, rowY0 + 4, "No stops found", &Font16, RED, GRAY);
                } else {
                    for(size_t i = 0; i < rowsToDraw; i++) {
                        int itemY = rowY0 + (i * rowH);
                        const std::string &name = usingGrouped ? g_dropdownStops[i].stopName : busStops[i].stopName;

                        UWORD bgColor = (static_cast<size_t>(currentBusStopIndex) == i && !usingGrouped) ? BLUE : WHITE;
                        Paint_DrawString_EN(20, itemY + 12,
                                            name.c_str(),
                                            &Font16, BLACK, bgColor);
                    }
                }
                displayBuffer(BlackImage);
            }
        }
        // Calculate wait time to sync with the next minute boundary
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        int secondsIntoMinute = timeinfo->tm_sec;
        int secondsToWait = (60 - secondsIntoMinute);
        if (secondsToWait <= 0) secondsToWait = 60;  // Safety check
        int iterations = secondsToWait * 10;  // 10 iterations per second (100ms each)
        
        std::cout << "Waiting " << secondsToWait << " seconds until next refresh" << std::endl;
        
    // Poll until the next minute, checking for touch input
        for(int i = 0; i < iterations && i < 600; i++) {  // Cap at 60 seconds max
            
            if (get_touch_data(&touch_data)) {
                int touchX = touch_data.coords[0].x;
                int touchY = touch_data.coords[0].y;
                
                std::cout << "Touch detected at (" << touchX << ", " << touchY << ")" << std::endl;

                // If the user tapped the top area, open the dropdown + direction modal
                if (touchY < 50) {
                    std::cout << "Dropdown touched" << std::endl;
                    // show the dropdown UI and ask for direction first
                    g_showDirectionModal = true;
                    g_dirChoice = DirectionChoice::None;
                    g_groupedStops.intoCity.clear();
                    g_groupedStops.outOfCity.clear();
                    g_dropdownStops.clear();
                    showDropDownMenu = true;
                    // debounce slightly and refresh immediately
                    DEV_Delay_ms(15);
                    break;
                }

                // If the direction modal is visible, handle taps on its buttons
                if (g_showDirectionModal) {
                    // Modal layout uses fixed Y positions: into_y=60, out_y=300
                    int x0 = 40, x1 = 280;
                    int btn_x = x0 + 10;
                    int btn_w = x1 - x0 - 20;
                    const int into_y = 60;
                    const int out_y = 300;
                    const int btn_h = 36;

                    // Into city button hit test
                    if (touchX >= btn_x && touchX <= btn_x + btn_w && touchY >= into_y && touchY <= into_y + btn_h) {
                        g_dirChoice = DirectionChoice::IntoCity;
                    }
                    // Out of city button hit test
                    else if (touchX >= btn_x && touchX <= btn_x + btn_w && touchY >= out_y && touchY <= out_y + btn_h) {
                        g_dirChoice = DirectionChoice::OutOfCity;
                    }

                    if (g_dirChoice != DirectionChoice::None) {
                        // Fetch grouped stops for the chosen direction
                        int perGroup = CONFIG_NUM_STOPS();
                        // std::cerr << "Direction chosen: " << (g_dirChoice == DirectionChoice::IntoCity ? "IntoCity" : "OutOfCity") << std::endl;
                        bool ok = getNearestStopsGrouped(CONFIG_ADDRESS(), perGroup, g_groupedStops);
                        if (!ok) {
                            // fallback: use nearest stops
                            std::vector<BusStopInfo> fallback;
                            if (getNearestStops(CONFIG_ADDRESS(), perGroup, fallback)) {
                                g_dropdownStops = fallback;
                            } else {
                                g_dropdownStops.clear();
                            }
                            std::cerr << "getNearestStopsGrouped failed, using fallback size=" << g_dropdownStops.size() << std::endl;
                        } else {
                            if (g_dirChoice == DirectionChoice::IntoCity) g_dropdownStops = g_groupedStops.intoCity;
                            else g_dropdownStops = g_groupedStops.outOfCity;
                            // std::cerr << "Grouped sizes -> into=" << g_groupedStops.intoCity.size() << " out=" << g_groupedStops.outOfCity.size() << std::endl;
                        }

                        g_showDirectionModal = false;
                        showDropDownMenu = true;
                    }
                }
                // If dropdown is visible, handle row selection (either grouped or static)
                else if (showDropDownMenu && touchY >= 50) {
                    int rowY0 = 50;
                    int rowH = 50;
                    int selectedIndex = (touchY - rowY0) / rowH;

                    // Determine which source we're selecting from
                    bool usingGrouped = !g_dropdownStops.empty();
                    size_t sourceSize = usingGrouped ? g_dropdownStops.size() : busStops.size();

                    if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < sourceSize) {
                        // std::cout << "Selected stop index: " << selectedIndex << std::endl;
                        BusStopInfo chosen = usingGrouped ? g_dropdownStops[selectedIndex] : busStops[selectedIndex];
                        // set selected stop ID and update current index if possible
                        g_selectedStopID = chosen.stopID;
                        for (size_t i = 0; i < busStops.size(); ++i) {
                            if (busStops[i].stopID == g_selectedStopID) {
                                currentBusStopIndex = i;
                                break;
                            }
                        }
                        showDropDownMenu = false; // close menu
                        g_dropdownStops.clear();
                        autoTurnOff = 0;  // Reset idle timer on interaction
                    }
                }


                DEV_Delay_ms(5);
                
                autoTurnOff = 0;  // Reset idle timer on any touch
                break;  // Exit the wait loop and refresh immediately
            }
            
            DEV_Delay_ms(100);  // Check every 100ms

            autoTurnOff++;  // Increment by 1 each 100ms (consistent timing)
            if(autoTurnOff >= 600){ // After 60 seconds of idle (600 * 100ms), turn off display
                std::cout << "No activity detected, turning off display to save power." << std::endl;
                Paint_Clear(BLACK);  // Clear the buffer first
                displayBuffer(BlackImage);  // Then display the black buffer

                //wait for touch to turn back on
                bool wokenUp = false;
                while(!wokenUp){
                    if (get_touch_data(&touch_data)) {
                        std::cout << "Touch detected, turning display back on." << std::endl;
                        DEV_Delay_ms(50); //debounce
                        autoTurnOff = 0;
                        wokenUp = true;
                    }
                    DEV_Delay_ms(100);  // Check every 100ms while screen is off
                }
                // Break out of the timing loop to refresh display immediately
                break;
            }
        }
        
      
    }
    
    // Cleanup
    free(BlackImage);
    DEV_ModuleExit();
    return 0;
}

// End of program
    // (no further top-level code)


    // no further actions
