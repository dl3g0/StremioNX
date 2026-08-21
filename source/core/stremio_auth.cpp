#include "stremio_auth.hpp"
#include "http_client.hpp"
#include "file_paths.hpp"
#include "logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace {
const char* kApiBase = "https://api.strem.io";
const char* kLinkBase = "https://link.stremio.com";

std::string jstr(const json& obj, const char* key) {
    if (obj.contains(key) && obj[key].is_string()) return obj[key].get<std::string>();
    return "";
}
}

StremioAuth& StremioAuth::getInstance() {
    static StremioAuth instance;
    return instance;
}

StremioAuth::StremioAuth() {
    load();
}

StremioAuth::~StremioAuth() {}

bool StremioAuth::isLoggedIn() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !auth_key_.empty();
}

std::string StremioAuth::getAuthKey() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return auth_key_;
}

StremioUser StremioAuth::getUser() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return user_;
}

void StremioAuth::logout() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auth_key_.clear();
        user_ = StremioUser();
    }
    save();
    LOG("[StremioAuth] Sesión cerrada");
}

void StremioAuth::load() {
    FilePaths::ensureDataDir();
    std::ifstream file(FilePaths::kAuthFile);
    if (file.is_open()) {
        try {
            json j;
            file >> j;
            std::lock_guard<std::mutex> lock(mutex_);
            if (j.contains("authKey") && j["authKey"].is_string()) {
                auth_key_ = j["authKey"].get<std::string>();
            }
            if (j.contains("user") && j["user"].is_object()) {
                const json& u = j["user"];
                user_.id = jstr(u, "_id");
                user_.email = jstr(u, "email");
                user_.avatar = jstr(u, "avatar");
            }
        } catch (...) {}
    }
}

void StremioAuth::save() {
    FilePaths::ensureDataDir();
    std::ofstream file(FilePaths::kAuthFile);
    if (file.is_open()) {
        json j;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            j["authKey"] = auth_key_;
            json u;
            u["_id"] = user_.id;
            u["email"] = user_.email;
            u["avatar"] = user_.avatar;
            j["user"] = u;
        }
        file << j.dump(2);
    }
}

bool StremioAuth::createDeviceLink(StremioDeviceLink& out) {
    std::string response;
    if (!HttpClient::getInstance().get(std::string(kLinkBase) + "/api/create", response)) {
        LOG("[StremioAuth] ERROR: createDeviceLink request failed");
        return false;
    }
    try {
        json j = json::parse(response);
        if (j.contains("result") && j["result"].is_object()) {
            j = j["result"];
        }
        out.code = jstr(j, "code");
        out.link = jstr(j, "link");
        out.qrcode = jstr(j, "qrcode");
        return !out.code.empty();
    } catch (...) {
        LOG("[StremioAuth] ERROR: createDeviceLink parse failed");
    }
    return false;
}

LinkPollStatus StremioAuth::pollDeviceLink(const std::string& code, std::string& authKeyOut) {
    std::string response;
    if (!HttpClient::getInstance().get(std::string(kLinkBase) + "/api/read?code=" + code, response)) {
        LOG("[StremioAuth] ERROR: pollDeviceLink request failed");
        return LinkPollStatus::Error;
    }
    try {
        json j = json::parse(response);
        if (j.contains("result") && j["result"].is_object() &&
            j["result"].contains("authKey") && j["result"]["authKey"].is_string()) {
            authKeyOut = j["result"]["authKey"].get<std::string>();
            return LinkPollStatus::Authorized;
        }
    } catch (...) {
        LOG("[StremioAuth] ERROR: pollDeviceLink parse failed");
        return LinkPollStatus::Error;
    }
    return LinkPollStatus::Pending;
}

bool StremioAuth::loginWithToken(const std::string& token) {
    json body;
    body["type"] = "LoginWithToken";
    body["token"] = token;
    std::string response;
    if (!HttpClient::getInstance().post(std::string(kApiBase) + "/api/loginWithToken", body.dump(), response)) {
        LOG("[StremioAuth] ERROR: loginWithToken request failed");
        return false;
    }
    try {
        json j = json::parse(response);
        if (j.contains("error")) {
            LOG("[StremioAuth] ERROR: loginWithToken returned an error");
            return false;
        }
        if (!j.contains("result") || !j["result"].is_object()) return false;
        const json& r = j["result"];
        if (!r.contains("authKey") || !r["authKey"].is_string()) return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auth_key_ = r["authKey"].get<std::string>();
            if (r.contains("user") && r["user"].is_object()) {
                const json& u = r["user"];
                user_.id = jstr(u, "_id");
                user_.email = jstr(u, "email");
                user_.avatar = jstr(u, "avatar");
            }
        }
        save();
        LOG("[StremioAuth] Login OK, user: " + user_.email);
        return true;
    } catch (...) {
        LOG("[StremioAuth] ERROR: loginWithToken parse failed");
    }
    return false;
}

bool StremioAuth::fetchAddons(std::vector<std::string>& manifestUrls) {
    std::string key = getAuthKey();
    if (key.empty()) return false;
    json body;
    body["type"] = "AddonCollectionGet";
    body["authKey"] = key;
    body["update"] = true;
    std::string response;
    if (!HttpClient::getInstance().post(std::string(kApiBase) + "/api/addonCollectionGet", body.dump(), response)) {
        LOG("[StremioAuth] ERROR: addonCollectionGet request failed");
        return false;
    }
    try {
        json j = json::parse(response);
        if (j.contains("error")) {
            LOG("[StremioAuth] ERROR: addonCollectionGet returned an error");
            return false;
        }
        if (!j.contains("result") || !j["result"].is_object()) return false;
        const json& r = j["result"];
        if (!r.contains("addons") || !r["addons"].is_array()) return false;
        manifestUrls.clear();
        for (const auto& addon : r["addons"]) {
            if (!addon.is_object()) continue;
            std::string transport = jstr(addon, "transportUrl");
            if (transport.empty()) {
                if (addon.contains("manifest") && addon["manifest"].is_object()) {
                    transport = jstr(addon["manifest"], "url");
                }
            }
            if (transport.empty()) continue;
            LOG("[StremioAuth] Account addon URL: " + transport);
            manifestUrls.push_back(transport);
        }
        LOG("[StremioAuth] Total addons found in account: " + std::to_string(manifestUrls.size()));
        return true;
    } catch (...) {
        LOG("[StremioAuth] ERROR: addonCollectionGet parse failed");
    }
    return false;
}