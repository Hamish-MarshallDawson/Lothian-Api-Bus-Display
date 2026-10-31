#ifndef SETTINGS_H
#define SETTINGS_H

// Per-install settings come from environment variables (on the Pi, the
// systemd EnvironmentFile or ~/.config/bustracker/env). Nothing personal is
// compiled in. See config.example.env.

#include <cstdlib>
#include <string>

inline std::string settingEnv(const char *name, const char *fallback = "") {
    const char *v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string(fallback);
}

// Novus Open API subscription key. Never print or log it.
inline const std::string &API_KEY() {
    static const std::string v = settingEnv("BUSTRACKER_KEY");
    return v;
}

inline const std::string &API_BASE() {
    static const std::string v =
        "https://apim-public.trapezegroupazure.co.uk/edinburgh-cec-openapi/generic/v1.2";
    return v;
}

// Sent on every request (APIs, geocoder, tiles). Never curl's default. Names the app
// and where to find it, as the OSM usage policies ask.
inline const std::string &USER_AGENT() {
    static const std::string v = settingEnv(
        "BUS_USER_AGENT",
        "LothianBusDisplay/1.0 (+https://github.com/Hamish-MarshallDawson/Lothian-Api-Bus-Display)");
    return v;
}

// Home position, in decimal degrees. When both are set, the home address is never
// sent anywhere: no geocoder call at all. Set once on the device.
inline bool HOME_POSITION(double &lat, double &lon) {
    static const bool have = !settingEnv("BUS_HOME_LAT").empty() && !settingEnv("BUS_HOME_LON").empty();
    static const double la = have ? std::atof(settingEnv("BUS_HOME_LAT").c_str()) : 0.0;
    static const double lo = have ? std::atof(settingEnv("BUS_HOME_LON").c_str()) : 0.0;
    lat = la;
    lon = lo;
    return have && la != 0.0 && lo != 0.0;
}

// Home address: the fallback for geocoding, used only when BUS_HOME_LAT/LON are not
// set. With a home position set and no address, this is a label that is never sent.
inline const std::string &CONFIG_ADDRESS() {
    static const std::string v = [] {
        std::string a = settingEnv("BUS_ADDRESS");
        if (a.empty()) {
            double la, lo;
            if (HOME_POSITION(la, lo)) a = "home position";
        }
        return a;
    }();
    return v;
}

// Geocoder endpoint (Nominatim by default). Used only when no home position is set.
inline const std::string &GEOCODER_URL() {
    static const std::string v = settingEnv("BUS_GEOCODER_URL", "https://nominatim.openstreetmap.org/search");
    return v;
}

// Tile URL template. {z}, {x} and {y} are replaced.
inline const std::string &TILE_URL() {
    static const std::string v = settingEnv("BUS_TILE_URL", "https://tile.openstreetmap.org/{z}/{x}/{y}.png");
    return v;
}

inline int CONFIG_NUM_STOPS() {
    static const int v = std::atoi(settingEnv("BUS_NUM_STOPS", "8").c_str());
    return v > 0 ? v : 8;
}

inline int CONFIG_LOOKAHEAD_MINS() { return 60; }
inline int CONFIG_MAX_EVENTS() { return 12; }

// Edinburgh city centre, used to tell "into city" from "out of city".
inline double CITY_CENTER_LAT() { return 55.9533; }
inline double CITY_CENTER_LON() { return -3.1883; }

#endif // SETTINGS_H
