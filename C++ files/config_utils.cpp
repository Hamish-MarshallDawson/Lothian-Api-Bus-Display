#include "config_utils.h"

#include <fstream>
#include <algorithm>
#include <cctype>

static inline std::string &ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    return s;
}
static inline std::string &rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    return s;
}
static inline std::string &trim(std::string &s) { return ltrim(rtrim(s)); }

void parseConfigFile(const std::string &path, std::string &out_user_agent, int &out_num_stops, std::string &out_address) {
    std::ifstream f(path);
    if(!f.is_open()) return;
    std::string line;
    while(std::getline(f, line)) {
        // strip comments (# or //)
        auto posHash = line.find('#');
        auto posSlash = line.find("//");
        size_t cut = std::string::npos;
        if(posHash != std::string::npos) cut = posHash;
        if(posSlash != std::string::npos) cut = (cut==std::string::npos ? posSlash : std::min(cut, posSlash));
        if(cut != std::string::npos) line = line.substr(0, cut);
        trim(line);
        if(line.empty()) continue;
        auto eq = line.find('=');
        if(eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq+1);
        trim(key); trim(val);
        for(auto &c: key) c = std::tolower((unsigned char)c);
        if(key == "user_agent") {
            out_user_agent = val;
        } else if(key == "num_stops" || key == "number_of_stops") {
            try { out_num_stops = std::stoi(val); } catch(...) {}
        } else if(key == "address") {
            out_address = val;
        }
    }
}
