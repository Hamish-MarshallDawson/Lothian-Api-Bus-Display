#ifndef DISKCACHE_H
#define DISKCACHE_H

// Small JSON cache files under ~/.cache/bustracker/ (or $XDG_CACHE_HOME/bustracker/).
// Lives outside the repo. Writes go to a temporary file, then rename, so a crash
// never leaves a half-written file. Contents are never logged: errors name the
// file and the failure only.

#include <string>
#include <nlohmann/json.hpp>

namespace diskcache {

// Cache directory, created if missing (mode 0700). Empty string if it can't be made.
std::string dir();

// Read a file in the cache directory. False if missing or not valid JSON.
bool load(const std::string &name, nlohmann::json &out);

// Write a file in the cache directory atomically. False on failure.
bool save(const std::string &name, const nlohmann::json &j);

// Delete cache files whose names start with prefix, except keep. Used to drop
// yesterday's day-scoped files.
void pruneOthers(const std::string &prefix, const std::string &keep);

}  // namespace diskcache

#endif // DISKCACHE_H
