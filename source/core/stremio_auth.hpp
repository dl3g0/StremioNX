#pragma once
#include <string>
#include <vector>
#include <mutex>

struct StremioUser {
    std::string id;
    std::string email;
    std::string avatar;
};

struct StremioDeviceLink {
    std::string code;
    std::string link;
    std::string qrcode;
};

enum class LinkPollStatus { Pending, Authorized, Error };

// Handles Stremio account login (device/link activation flow) and the
// synchronization of the user's add-on collection via the Stremio HTTP API.
class StremioAuth {
public:
    static StremioAuth& getInstance();

    // link.stremio.com
    bool createDeviceLink(StremioDeviceLink& out);
    LinkPollStatus pollDeviceLink(const std::string& code, std::string& authKeyOut);

    // api.strem.io
    bool loginWithToken(const std::string& token);
    bool fetchAddons(std::vector<std::string>& manifestUrls);

    bool isLoggedIn() const;
    std::string getAuthKey() const;
    StremioUser getUser() const;
    void logout();

private:
    StremioAuth();
    ~StremioAuth();
    void load();
    void save();

    std::string auth_key_;
    StremioUser user_;
    mutable std::mutex mutex_;
};