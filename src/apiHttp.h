#ifndef API_HTTP_H
#define API_HTTP_H

// Small header-only helper for talking to the Trapeze / Novus Open API.
// Header-only on purpose so the Makefile does not need a new object file.

#include <string>
#include <iostream>
#include <curl/curl.h>

#include "settings.h"

inline size_t apiHttpWriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string *)userp)->append((char *)contents, size * nmemb);
    return size * nmemb;
}

// GET a URL with the APIM subscription key attached.
// Returns true only on a transport-level success AND an HTTP 2xx.
// httpCodeOut is filled in either way so the caller can tell 401 from 429.
// With no key set, returns false and httpCodeOut stays 0.
inline bool apiHttpGet(const std::string &url, std::string &bodyOut, long &httpCodeOut) {
    bodyOut.clear();
    httpCodeOut = 0;

    if (API_KEY().empty()) {
        std::cerr << "apiHttpGet: no subscription key. Set BUSTRACKER_KEY in the environment." << std::endl;
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, ("Ocp-Apim-Subscription-Key: " + API_KEY()).c_str());
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, apiHttpWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bodyOut);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT().c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    // Empty string: send Accept-Encoding for every encoding curl supports and decode
    // the gzip reply. Without this the body arrives compressed and JSON parsing fails.
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCodeOut);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "apiHttpGet transport error: " << curl_easy_strerror(res)
                  << " for " << url << std::endl;
        bodyOut.clear();
        return false;
    }

    if (httpCodeOut == 401 || httpCodeOut == 403) {
        std::cerr << "apiHttpGet: HTTP " << httpCodeOut
                  << " - subscription key rejected or not subscribed to this product." << std::endl;
        bodyOut.clear();
        return false;
    }
    if (httpCodeOut == 429) {
        std::cerr << "apiHttpGet: HTTP 429 - rate limit or quota exceeded. Back off." << std::endl;
        bodyOut.clear();
        return false;
    }
    if (httpCodeOut < 200 || httpCodeOut >= 300) {
        std::cerr << "apiHttpGet: HTTP " << httpCodeOut << " for " << url << std::endl;
        bodyOut.clear();
        return false;
    }

    return true;
}

#endif // API_HTTP_H