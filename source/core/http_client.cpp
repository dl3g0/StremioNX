#include "http_client.hpp"
#include <curl/curl.h>
#include <iostream>
#include <switch.h>
#include <borealis.hpp>

HttpClient& HttpClient::getInstance() {
    static HttpClient instance;
    return instance;
}

bool HttpClient::init() {
    // socketInitializeDefault(); // Removed because switch_wrapper.c already does socketInitialize()
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
}

void HttpClient::cleanup() {
    curl_global_cleanup();
    // socketExit(); // Handled by switch_wrapper.c
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* mem = (std::string*)userp;
    mem->append((char*)contents, realsize);
    return realsize;
}

static size_t WriteBinaryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::vector<unsigned char>* mem = (std::vector<unsigned char>*)userp;
    unsigned char* data = (unsigned char*)contents;
    mem->insert(mem->end(), data, data + realsize);
    return realsize;
}

bool HttpClient::get(const std::string& url, std::string& response) {
    brls::Logger::info("HttpClient::get: starting for {}", url);
    CURL* curl = curl_easy_init();
    if (!curl) {
        brls::Logger::error("HttpClient::get: curl_easy_init failed");
        return false;
    }

    brls::Logger::info("HttpClient::get: configuring options");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    brls::Logger::info("HttpClient::get: calling curl_easy_perform()");
    CURLcode res = curl_easy_perform(curl);
    brls::Logger::info("HttpClient::get: perform finished with code {}", (int)res);
    curl_easy_cleanup(curl);
    brls::Logger::info("HttpClient::get: cleanup finished");

    return res == CURLE_OK;
}

bool HttpClient::getBinary(const std::string& url, std::vector<unsigned char>& buffer) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBinaryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/114.0.0.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // Prevent infinite hang
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);

    return res == CURLE_OK && http_code >= 200 && http_code < 300;
}
