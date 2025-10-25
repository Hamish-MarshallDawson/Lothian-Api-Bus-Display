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

// Include the bus stop data from busStopInfo.cpp
#include "busStopInfo.cpp"  
// file kept private to keep data and API key from public :)

// Forward declaration of st7796 functions from C library
extern "C" {
    void st7796_set_windows(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end);
}

// Helper function to display the Paint buffer on the LCD
void displayBuffer(UWORD *image) {
    // Set the display window to full screen
    st7796_set_windows(0, 0, ST7796_WIDTH - 1, ST7796_HEIGHT - 1);
    
    // Send data line by line to the display, set to datamode
    LCD_DC_1;  
    
    for(uint16_t y = 0; y < ST7796_HEIGHT; y++) {
        // Send one row at a time
        DEV_SPI_Write_nByte((uint8_t*)&image[y * ST7796_WIDTH], ST7796_WIDTH * 2);
    }
}


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

void drawBusInfo(const std::string& routeNum, const std::string& destination, 
                 const std::string& timeStr, bool isLive, int yPos) {
    
    // was used to print to terminal for debug
    // std::cout << "Drawing at yPos=" << yPos 
    //           << " route=" << routeNum 
    //           << " dest=" << destination 
    //           << " time=" << timeStr << std::endl;
    
    // Check if position is valid on display
    if(yPos > 430) {  // Leave room for text height
        std::cout << "WARNING: yPos too large, skipping" << std::endl;
        return;
    }
    
    // Truncate destination name to fit screen
    std::string shortDest = destination;
    if(shortDest.length() > 20) {  // Adjust based on font size
        shortDest = shortDest.substr(0, 17) + "...";
    }
}

int main() {
    // Intro message on terminal
    std::cout << "Lothian Bus Display Starting" << std::endl;
    
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

    // Create bus stop instance
    BusStopInfo busStop;
    
   // std::cout << "Bus stop: " << busStop.stopName << " (ID: " << busStop.stopID << ")" << std::endl;
    
    // Main loop
    while(true) {
       // std::cout << "\n=== Fetching bus data ===" << std::endl;
        
        // Fetch bus data using func defined above
        std::string apiResponse = fetchLiveBusTimes(busStop.stopID);

        // std::string apiResponse = fetchLiveBusTimes(busStop.stopName);
        
        if(!apiResponse.empty()) {
           // std::cout << "API response received (" << apiResponse.length() << " bytes), parsing..." << std::endl;
            
            // Debug: Print first 200 characters of response
            //std::cout << "Response preview: " << apiResponse.substr(0, 200) << "..." << std::endl;
            
            // Parse JSON
            try {
                json busData = json::parse(apiResponse);
                
                // Debug: Show how many routes we got
                // std::cout << "Parsed JSON: " << busData.size() << " routes found" << std::endl;
                
                // std::cout << "Clearing screen and drawing header" << std::endl;
                
                // Clear Paint buffer (not the physical screen)
                //this also sets background colour
                Paint_Clear(GRAY);
                


                //get current time to see if we are past midnight and to display time on screen
                                
                time_t now = time(nullptr);
                struct tm* currentTime = localtime(&now);
                int currentMinutes = currentTime->tm_hour * 60 + currentTime->tm_min;
                

                //yPosition for all elements
                //basically acts like a container, changing this value moves everything up or down screen
                //lower value up, higher value down
                int yPos = 20;

                // Burger menu dropdown - draw chevron down arrow
                // Draw a simple down arrow using lines (coordinates adjusted for MIRROR_HORIZONTAL)
                Paint_DrawLine(145, yPos, 160, yPos + 15, WHITE, DOT_PIXEL_4X4, LINE_STYLE_SOLID);  // Left diagonal
                Paint_DrawLine(160, yPos + 15, 175, yPos, WHITE, DOT_PIXEL_4X4, LINE_STYLE_SOLID);  // Right diagonal

                yPos += 30;
                Paint_DrawString_EN(5, yPos, "------------------", &Font24, WHITE, WHITE);
                

                // Draw header - adjust X coordinates for mirroring
                std::cout << "Drawing header..." << std::endl;
                Paint_DrawString_EN(30, yPos+=20, busStop.stopName.c_str(), &Font20, BLACK, WHITE);



                //need to make this say Route to then destination
                Paint_DrawString_EN(100, yPos+20, busStop.destination.c_str(), &Font12, GRAY, BLACK);
                

                yPos += 40;                
                Paint_DrawString_EN(30, yPos, "Current Time: ", &Font20, WHITE, WHITE);


                std::cout << "Starting to draw bus times..." << std::endl;
                
                // Step 1: Collect all departures from all routes into a single vector
                struct BusDeparture {
                    std::string routeName;
                    std::string destination;
                    std::string time;
                    int minutesSinceMidnight;  // For proper time sorting
                };
                
                //Collects all depatures
                std::vector<BusDeparture> allDepartures;
                

                for(const auto& route : busData) {
                    std::string routeName = route["routeName"];
                    // std::cout << "Processing route: " << routeName << std::endl;
                    
                    // Check if departures exists and is an array
                    if(!route.contains("departures")) {
                        std::cout << "  No 'departures' key found!" << std::endl;
                        continue;
                    }
                    
                    // Verify it's an array and has come through correctly
                    if(!route["departures"].is_array()) {
                        std::cout << "  'departures' is not an array!" << std::endl;
                        continue;
                    }
                    
                    // Get departures array
                    auto departures = route["departures"];
                    //std::cout << "  Found " << departures.size() << " departures" << std::endl;
                    
                    // Collect all departures from this route
                    for(const auto& dep : departures) {
                        BusDeparture busDep;
                        busDep.routeName = routeName;
                        busDep.destination = dep.value("destination", "Unknown");
                        
                        // Use ineoUTCTime field (despite the name, this appears to be local time)
                        // was a weird naming choice by Lothian API staff but figured it out
                        std::string localTime = dep.value("ineoUTCTime", "");
                        
                        // Extract just HH:MM for display (seconds would be overkill and painful to program)
                        if(localTime.length() >= 5) {
                            busDep.time = localTime.substr(0, 5);
                            
                            // Convert to minutes since midnight for proper sorting
                            // this is so if a bus comes at 00:15 it is after 23:50 bus not before
                            int hour = std::stoi(localTime.substr(0, 2));
                            int minute = std::stoi(localTime.substr(3, 2));
                            busDep.minutesSinceMidnight = hour * 60 + minute;
                            
                            // Handle midnight wraparound: if time is past midnight but before 6am,
                            // and current time is evening (after 6pm), treat it as "tomorrow"
                            //weird logic but it works
                            //COULD REFACTOR LATER TO MAKE MORE EFFICENT
                            if(busDep.minutesSinceMidnight < 360 && currentMinutes > 1080) {
                                busDep.minutesSinceMidnight += 1440;  // Add 24 hours
                            }
                        } else {
                            busDep.time = "??:??";
                            busDep.minutesSinceMidnight = 9999;  // Put errors at the end
                        }
                        
                        allDepartures.push_back(busDep);
                    }
                }
                //display current time under header, makes bus times easier to read
                char timeString[10];
                snprintf(timeString, sizeof(timeString), "%02d:%02d", 
                         currentTime->tm_hour, currentTime->tm_min);
                Paint_DrawString_EN(220, yPos, timeString, &Font20, WHITE, WHITE);
                
                // std::cout << "Collected " << allDepartures.size() << " total departures" << std::endl;
                // std::cout << "Current time: " << currentTime->tm_hour << ":" 
                        //   << std::setfill('0') << std::setw(2) << currentTime->tm_min 
                        //   << " (" << currentMinutes << " minutes since midnight)" << std::endl;
                
                // Step 2: Sort all departures by time
                std::sort(allDepartures.begin(), allDepartures.end(), 
                    [](const BusDeparture& a, const BusDeparture& b) {
                        return a.minutesSinceMidnight < b.minutesSinceMidnight;
                    });
                
                // std::cout << "Departures sorted by time" << std::endl;
                
                // Step 3: Display the first 5 buses
                yPos= yPos + 60;
                //change this value for more or less busses
                int maxBuses = 4;
                int busCount = 0;
                
                for(const auto& bus : allDepartures) {
                    if(busCount >= maxBuses) break;
                    
                    //WILL PLAY AROUND WITH THIS, not certain it does right job just yet
                    // Truncate long destination names to prevent overflow
                    std::string dest = bus.destination;
                    // if(dest.length() > 10) {
                    //     dest = dest.substr(0, 8) + "..";
                    // }
                    
                    // Truncate route name if too long
                    // incase bus code is super long like "X12345"
                    // think most edinburgh bus codes are 3 characters max
                    // might be redundent but better safe than sorry
                    std::string shortRoute = bus.routeName;
                    if(shortRoute.length() > 3) {
                        shortRoute = shortRoute.substr(0, 3);
                    }
                    
                    // Debug output
                    // std::cout << "  Drawing: Route " << bus.routeName 
                    //           << " to " << dest 
                    //           << " at " << bus.time 
                    //           << " (yPos=" << yPos << ")" << std::endl;
                    
                    // Draw with coordinates adjusted for MIRROR_HORIZONTAL
                    // every second bus displayed should have the label be blue for visual distinction
                    if(busCount % 2 == 0){
                        Paint_DrawString_EN(10, yPos, shortRoute.c_str(), &Font48, RED, WHITE);
                    } else {
                        Paint_DrawString_EN(10, yPos, shortRoute.c_str(), &Font48, BLUE, WHITE);
                    }


                    // Destination (middle) - split into two lines
                    //makes it look nicer if destination is long
                    size_t spacePos = dest.find(' ');
                    if(spacePos != std::string::npos) {
                        // Found a space, split the destination
                        std::string firstWord = dest.substr(0, spacePos);
                        std::string restOfWords = dest.substr(spacePos + 1);
                        
                        Paint_DrawString_EN(70, yPos + 4, firstWord.c_str(), &Font20, WHITE, WHITE);
                        Paint_DrawString_EN(70, yPos + 22, restOfWords.c_str(), &Font20, WHITE, WHITE);
                    } else {
                        // No space found, just draw on one line
                        Paint_DrawString_EN(70, yPos + 12, dest.c_str(), &Font20, WHITE, WHITE);
                    }
                    
                    // Time (right side)
                    Paint_DrawString_EN(160, yPos + 4, bus.time.c_str(), &Font48, WHITE, WHITE);
                    
                    //moves down for next line
                    yPos += 50;

                    //VISUAL DIVIDER DO NOT TOUCH
                    Paint_DrawString_EN(5, yPos, "------------------", &Font24, WHITE, WHITE);
                    //moves down for next line
                    yPos += 20;
                    busCount++;
                }
            
                // std::cout << "Display updated successfully! Displayed " << busCount << " buses" << std::endl;
                // std::cout << "Pushing image buffer to LCD..." << std::endl;
                displayBuffer(BlackImage);  // Push the buffer to the physical display
            }
            catch(const json::exception& e) {
                std::cerr << "JSON error: " << e.what() << std::endl;
                
                // Show error on display
                st7796_clear(BLACK);
                Paint_DrawString_EN(10, 200, "API Error", &Font24, RED, BLACK);
            }
        } else {
            std::cerr << "No API response received" << std::endl;
            
            // Show error on display
            st7796_clear(BLACK);
            Paint_DrawString_EN(10, 200, "No Data", &Font24, RED, BLACK);
        }
        
        // Wait 60 seconds before next update, but check for touch input during the wait
        std::cout << "Waiting 60 seconds before next update (touch screen to refresh now)..." << std::endl;
        
        // Poll for 60 seconds in small increments to allow touch detection
        bool forceRefresh = false;
        for(int i = 0; i < 600; i++) {  // 600 iterations * 100ms = 60 seconds
            if (get_touch_data(&touch_data)) {
                int touchX = touch_data.coords[0].x;
                int touchY = touch_data.coords[0].y;
                
                std::cout << "Touch detected at (" << touchX << ", " << touchY << ") - refreshing now!" << std::endl;
                forceRefresh = true;

                if(touchY<50)
                {
                    std::cout << "Dropdown touched" << std::endl;
                }
                
                // Small delay to debounce the touch
                DEV_Delay_ms(500);
                break;  // Exit the wait loop and refresh immediately
            }
            DEV_Delay_ms(100);  // Check every 100ms
        }
        
        if(forceRefresh) {
            std::cout << "Manual refresh triggered by touch" << std::endl;
        }
    }
    
    // Cleanup
    free(BlackImage);
    DEV_ModuleExit();
    return 0;
}
