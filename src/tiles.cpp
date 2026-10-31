// tiles.cpp: see tiles.h

#include "tiles.h"

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sys/stat.h>

#include "apiHttp.h"
#include "diskcache.h"
#include "settings.h"

using json = nlohmann::json;

static const time_t DEFAULT_LIFE_S = 7 * 24 * 3600;   // when the headers say nothing
static const size_t TILE_MAX_BYTES = 256 * 1024;
static const time_t RETRY_AFTER_FAILURE_S = 60;

bool isPng(const std::string &b) {
    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (b.size() < 8) return false;
    for (int i = 0; i < 8; ++i) {
        if (static_cast<unsigned char>(b[i]) != sig[i]) return false;
    }
    return true;
}

std::string tileUrl(const TileId &t) {
    std::string url = TILE_URL();
    auto rep = [&url](const std::string &key, const std::string &val) {
        for (size_t p; (p = url.find(key)) != std::string::npos;) url.replace(p, key.size(), val);
    };
    rep("{z}", std::to_string(t.z));
    rep("{x}", std::to_string(t.x));
    rep("{y}", std::to_string(t.y));
    return url;
}

static std::string lowerCase(std::string s) {
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

time_t tileExpiry(const std::string &cacheControl, const std::string &expires,
                  time_t fetchedAt, bool &noStore) {
    noStore = false;
    const std::string cc = lowerCase(cacheControl);
    if (cc.find("no-store") != std::string::npos) {
        noStore = true;
        return fetchedAt;
    }
    if (cc.find("no-cache") != std::string::npos) return fetchedAt;   // must revalidate every time

    const size_t ma = cc.find("max-age=");
    if (ma != std::string::npos) {
        long secs = std::atol(cc.c_str() + ma + 8);
        if (secs < 0) secs = 0;
        return fetchedAt + secs;
    }
    if (!expires.empty()) {
        std::tm tm{};
        if (strptime(expires.c_str(), "%a, %d %b %Y %H:%M:%S", &tm) != nullptr) {
            const time_t t = timegm(&tm);
            return t > fetchedAt ? t : fetchedAt;
        }
        return fetchedAt;   // unreadable Expires: treat as already stale
    }
    return fetchedAt + DEFAULT_LIFE_S;   // no headers: at least 7 days
}

// ---------------------------------------------------------------------------
// Disk: the PNG and a small meta file per tile

static std::string tileStem(const TileId &t) {
    return "tiles/" + std::to_string(t.z) + "-" + std::to_string(t.x) + "-" + std::to_string(t.y);
}

static std::string tilesDir() {
    const std::string base = diskcache::dir();
    if (base.empty()) return std::string();
    const std::string d = base + "/tiles";
    mkdir(d.c_str(), 0700);   // exists already is fine
    return d;
}

static bool readFile(const std::string &path, std::string &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

struct Meta {
    time_t expires = 0;
    std::string etag;
    std::string lastModified;
};

static Meta loadMeta(const TileId &t) {
    Meta m;
    json j;
    if (diskcache::load(tileStem(t) + ".json", j) && j.is_object()) {
        m.expires = static_cast<time_t>(j.value("expires", 0LL));
        m.etag = j.value("etag", std::string());
        m.lastModified = j.value("lastModified", std::string());
    }
    return m;
}

static void saveMeta(const TileId &t, const Meta &m) {
    json j{{"expires", static_cast<long long>(m.expires)}, {"etag", m.etag}, {"lastModified", m.lastModified}};
    diskcache::save(tileStem(t) + ".json", j);
}

static bool writePng(const std::string &path, const std::string &body) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(body.data(), static_cast<std::streamsize>(body.size()));
        f.flush();
        if (!f) { std::remove(tmp.c_str()); return false; }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Network

struct Reply {
    long code = 0;
    std::string body;
    std::string cacheControl, expires, etag, lastModified;
};

static size_t collectBody(void *contents, size_t size, size_t nmemb, void *userp) {
    std::string *s = static_cast<std::string *>(userp);
    const size_t n = size * nmemb;
    if (s->size() + n > TILE_MAX_BYTES) return 0;   // too big: abort
    s->append(static_cast<char *>(contents), n);
    return n;
}

static size_t collectHeader(char *buffer, size_t size, size_t nitems, void *userp) {
    Reply *r = static_cast<Reply *>(userp);
    const size_t n = size * nitems;
    std::string line(buffer, n);
    const size_t colon = line.find(':');
    if (colon == std::string::npos) return n;
    std::string name = lowerCase(line.substr(0, colon));
    std::string value = line.substr(colon + 1);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) value.pop_back();
    while (!value.empty() && value.front() == ' ') value.erase(0, 1);
    if (name == "cache-control") r->cacheControl = value;
    else if (name == "expires") r->expires = value;
    else if (name == "etag") r->etag = value;
    else if (name == "last-modified") r->lastModified = value;
    return n;
}

// One GET. Sends a conditional request when validators are given. Never sends
// Cache-Control or Pragma.
static bool fetch(const TileId &t, const Meta *validators, Reply &r) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;
    const std::string url = tileUrl(t);
    struct curl_slist *headers = nullptr;
    if (validators && !validators->etag.empty()) {
        headers = curl_slist_append(headers, ("If-None-Match: " + validators->etag).c_str());
    }
    if (validators && !validators->lastModified.empty()) {
        headers = curl_slist_append(headers, ("If-Modified-Since: " + validators->lastModified).c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT().c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collectBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, collectHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &r);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    const CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.code);
    curl_easy_cleanup(curl);
    if (headers) curl_slist_free_all(headers);
    return res == CURLE_OK;
}

// ---------------------------------------------------------------------------

bool tileBytes(const TileId &tile, std::string &pngOut, bool allowNetwork) {
    static std::mutex oneAtATime;                 // one request at a time
    static std::map<std::string, time_t> lastFailure;

    pngOut.clear();
    const std::string d = tilesDir();
    if (d.empty()) return false;
    const std::string png = diskcache::dir() + "/" + tileStem(tile) + ".png";

    std::string cached;
    const bool haveFile = readFile(png, cached) && isPng(cached);
    Meta meta = loadMeta(tile);
    const time_t now = std::time(nullptr);

    if (haveFile && meta.expires > now) {
        pngOut = cached;                          // fresh: no network
        return true;
    }
    if (!allowNetwork) {
        if (haveFile) pngOut = cached;            // stale is better than nothing
        return haveFile;
    }

    std::lock_guard<std::mutex> lock(oneAtATime);
    auto failed = lastFailure.find(png);
    if (failed != lastFailure.end() && now - failed->second < RETRY_AFTER_FAILURE_S) {
        if (haveFile) pngOut = cached;
        return haveFile;
    }

    Reply r;
    if (!fetch(tile, haveFile ? &meta : nullptr, r)) {
        lastFailure[png] = now;
        if (haveFile) pngOut = cached;
        return haveFile;
    }

    // 304: the cached bytes are still good; take the new expiry from the reply
    if (r.code == 304 && haveFile) {
        bool noStore = false;
        meta.expires = tileExpiry(r.cacheControl, r.expires, now, noStore);
        if (!r.etag.empty()) meta.etag = r.etag;
        if (!r.lastModified.empty()) meta.lastModified = r.lastModified;
        saveMeta(tile, meta);
        lastFailure.erase(png);
        pngOut = cached;
        return true;
    }

    if (r.code == 200 && isPng(r.body)) {
        bool noStore = false;
        const time_t expires = tileExpiry(r.cacheControl, r.expires, now, noStore);
        lastFailure.erase(png);
        pngOut = r.body;
        if (noStore) {
            // Not kept on disk; drop any older copy so it can't be served later
            std::remove(png.c_str());
            saveMeta(tile, Meta{});
            return true;
        }
        if (writePng(png, r.body)) {
            saveMeta(tile, Meta{expires, r.etag, r.lastModified});
        }
        return true;
    }

    lastFailure[png] = now;
    if (haveFile) pngOut = cached;
    return haveFile;
}
