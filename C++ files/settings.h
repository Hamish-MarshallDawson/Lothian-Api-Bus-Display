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

inline const std::string &USER_AGENT() {
    static const std::string v = settingEnv("BUS_USER_AGENT", "LothianBusDisplay/1.0");
    return v;
}

// Home address, geocoded to find the nearest stops.
inline const std::string &CONFIG_ADDRESS() {
    static const std::string v = settingEnv("BUS_ADDRESS");
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
