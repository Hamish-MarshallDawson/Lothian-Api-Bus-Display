# Lothian-Api-Bus-Display
Takes advantage of the lothian bus api to allow users to display up to 10 local bus stops and their current status on a 3.5inch display

# Software/API used
Main language used is C++, main reasoning behind this choice is for the efficiency of the language and memory management.

The user provides their address in a config.h file and how many bus stops they want to load in their local area.

The address is then sent to Open Street Map https://www.openstreetmap.org/. the program then takes the resulting longitude and latitude and compare it to the longitude and latitude of bus stops stored on the lothian bus api.

Then doing a short calculation the software sorts the bus stops by closest to the users address.

# Hardware used 
Can be found in notes folder


