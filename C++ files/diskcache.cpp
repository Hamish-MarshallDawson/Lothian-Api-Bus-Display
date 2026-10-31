// diskcache.cpp: see diskcache.h

#include "diskcache.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace diskcache {

static bool makeDir(const std::string &path) {
    if (mkdir(path.c_str(), 0700) == 0) return true;
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string dir() {
    static std::string cached;
    static bool tried = false;
    if (tried) return cached;
    tried = true;

    std::string base;
    const char *xdg = std::getenv("XDG_CACHE_HOME");
    if (xdg && *xdg) {
        base = xdg;
    } else {
        const char *home = std::getenv("HOME");
        if (!home || !*home) return cached;
        base = std::string(home) + "/.cache";
        makeDir(base);  // may already exist
    }
    if (!makeDir(base + "/bustracker")) {
        std::cerr << "diskcache: cannot create the cache directory" << std::endl;
        return cached;
    }
    cached = base + "/bustracker";
    return cached;
}

bool load(const std::string &name, nlohmann::json &out) {
    const std::string d = dir();
    if (d.empty()) return false;
    std::ifstream f(d + "/" + name);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        out = nlohmann::json::parse(ss.str());
        return true;
    } catch (const nlohmann::json::exception &) {
        std::cerr << "diskcache: " << name << " is not valid JSON; ignoring it" << std::endl;
        return false;
    }
}

bool save(const std::string &name, const nlohmann::json &j) {
    const std::string d = dir();
    if (d.empty()) return false;
    const std::string path = d + "/" + name;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << j.dump();
        f.flush();
        if (!f) return false;
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        std::cerr << "diskcache: could not replace " << name << std::endl;
        return false;
    }
    return true;
}

void pruneOthers(const std::string &prefix, const std::string &keep) {
    const std::string d = dir();
    if (d.empty()) return;
    DIR *h = opendir(d.c_str());
    if (!h) return;
    while (struct dirent *e = readdir(h)) {
        const std::string n = e->d_name;
        if (n.compare(0, prefix.size(), prefix) == 0 && n != keep) {
            std::remove((d + "/" + n).c_str());
        }
    }
    closedir(h);
}

}  // namespace diskcache
