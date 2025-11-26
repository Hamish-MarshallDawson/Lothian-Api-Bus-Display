#ifndef CONFIG_UTILS_H
#define CONFIG_UTILS_H

#include <string>

// Parse config file with simple key=value lines.
// Supported keys: user_agent, num_stops, address
void parseConfigFile(const std::string &path, std::string &out_user_agent, int &out_num_stops, std::string &out_address);

#endif // CONFIG_UTILS_H
