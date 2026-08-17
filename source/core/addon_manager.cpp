#include "addon_manager.hpp"
#include "http_client.hpp"
#include "file_paths.hpp"
#include <nlohmann/json.hpp>
#include <SDL2/SDL_image.h>
#include <iostream>
#include "logger.hpp"

#include <fstream>
#include <atomic>
#include "task_queue.hpp"

using json = nlohmann::json;

// The Switch's GPU tops out at 1080p; drop any stream that advertises a
// higher resolution so the applet never tries to decode 4K/8K.
static bool isResolutionOver1080p(const std::string& name, const std::string& title) {
    std::string hay = name + " " + title;
    std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
    // Token list covers the common markers used by Torrentio/MediaFusion/
    // StremThru/Progreso Latino ("4K", "2160p", "8K", "4320p", "UHD", "3840").
    static const char* tokens[] = {"2160", "4320", "8k", "4k", "uhd", "3840", "7680"};
    for (const char* tok : tokens) {
        if (hay.find(tok) != std::string::npos)
            return true;
    }
    return false;
}

// Normalizes a manifest URL. Returns an empty string if the URL is unusable.
// Some browsers append HTML/binary junk to the form value, so we cut at
// "manifest.json" and only accept well-formed http(s) URLs.
static std::string sanitizeManifestUrl(std::string url) {
    if (url.empty()) return "";
    size_t mj = url.find("manifest.json");
    if (mj != std::string::npos) {
        url = url.substr(0, mj) + "manifest.json";
    } else {
        size_t sp = url.find_first_of(" \t\r\n<\"");
        if (sp != std::string::npos) url = url.substr(0, sp);
    }
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return "";
    return url;
}

AddonManager& AddonManager::getInstance() {
    static AddonManager instance;
    return instance;
}

AddonManager::AddonManager() {
    loadAddons();
}

AddonManager::~AddonManager() {
    saveAddons();
}

void AddonManager::addAddon(const std::string& url) {
    std::string clean = sanitizeManifestUrl(url);
    if (clean.empty()) {
        LOG("[AddonManager] ERROR: invalid addon URL rejected: " + url);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(addons_mutex);
        if (std::find(installed_addons.begin(), installed_addons.end(), clean) == installed_addons.end()) {
            installed_addons.push_back(clean);
            saveAddons();
        }
    }
    fetchManifest(); // Refresh outside lock
    notifyCatalogsChanged();
}

void AddonManager::removeAddon(const std::string& url) {
    {
        std::lock_guard<std::mutex> lock(addons_mutex);
        auto it = std::find(installed_addons.begin(), installed_addons.end(), url);
        if (it != installed_addons.end()) {
            installed_addons.erase(it);
            saveAddons();
        }
    }
    fetchManifest(); // Refresh outside lock
    notifyCatalogsChanged();
}

void AddonManager::loadAddons() {
    std::lock_guard<std::mutex> lock(addons_mutex);
    FilePaths::ensureDataDir();
    std::ifstream file(FilePaths::kAddonsFile);
    if (file.is_open()) {
        try {
            json j;
            file >> j;
            if (j.is_array()) {
                installed_addons.clear();
                for (auto& addon : j) {
                    std::string url = addon.get<std::string>();
                    std::string clean = sanitizeManifestUrl(url);
                    if (!clean.empty() &&
                        std::find(installed_addons.begin(), installed_addons.end(), clean) == installed_addons.end()) {
                        installed_addons.push_back(clean);
                    }
                }
            }
        } catch(...) {}
    }
    
    // Default addons if empty
    if (installed_addons.empty()) {
        installed_addons.push_back("https://v3-cinemeta.strem.io/manifest.json");
    }
    
    loadCatalogPrefs();
}

void AddonManager::saveAddons() {
    std::ofstream file(FilePaths::kAddonsFile);
    if (file.is_open()) {
        json j = installed_addons;
        file << j.dump(4);
    }
}

void AddonManager::loadCatalogPrefs() {
    std::lock_guard<std::mutex> lock(prefs_mutex);
    std::ifstream file(FilePaths::kCatalogPrefsFile);
    if (file.is_open()) {
        try {
            json j;
            file >> j;
            if (j.contains("hidden_catalogs") && j["hidden_catalogs"].is_array()) {
                hidden_catalogs.clear();
                for (auto& item : j["hidden_catalogs"]) {
                    hidden_catalogs.push_back(item.get<std::string>());
                }
            }
            if (j.contains("catalog_order") && j["catalog_order"].is_array()) {
                catalog_order.clear();
                for (auto& item : j["catalog_order"]) {
                    catalog_order.push_back(item.get<std::string>());
                }
            }
        } catch(...) {}
    }
}

void AddonManager::saveCatalogPrefs() {
    std::lock_guard<std::mutex> lock(prefs_mutex);
    std::ofstream file(FilePaths::kCatalogPrefsFile);
    if (file.is_open()) {
        json j;
        j["hidden_catalogs"] = hidden_catalogs;
        j["catalog_order"] = catalog_order;
        file << j.dump(4);
    }
}

bool AddonManager::isCatalogHidden(const std::string& key) {
    std::lock_guard<std::mutex> lock(prefs_mutex);
    return std::find(hidden_catalogs.begin(), hidden_catalogs.end(), key) != hidden_catalogs.end();
}

void AddonManager::toggleCatalogHidden(const std::string& key) {
    {
        std::lock_guard<std::mutex> lock(prefs_mutex);
        auto it = std::find(hidden_catalogs.begin(), hidden_catalogs.end(), key);
        if (it != hidden_catalogs.end()) {
            hidden_catalogs.erase(it);
        } else {
            hidden_catalogs.push_back(key);
        }
    }
    saveCatalogPrefs();
    reorderCatalogs();
}

void AddonManager::moveCatalogUp(const std::string& key) {
    {
        std::lock_guard<std::mutex> lock(prefs_mutex);
        auto it = std::find(catalog_order.begin(), catalog_order.end(), key);
        if (it != catalog_order.end() && it != catalog_order.begin()) {
            std::iter_swap(it, it - 1);
        }
    }
    saveCatalogPrefs();
    reorderCatalogs();
}

void AddonManager::moveCatalogDown(const std::string& key) {
    {
        std::lock_guard<std::mutex> lock(prefs_mutex);
        auto it = std::find(catalog_order.begin(), catalog_order.end(), key);
        if (it != catalog_order.end() && it + 1 != catalog_order.end()) {
            std::iter_swap(it, it + 1);
        }
    }
    saveCatalogPrefs();
    reorderCatalogs();
}

void AddonManager::reorderCatalogs() {
    // Apply the new hidden/order state to the already-downloaded catalog
    // lists without re-fetching manifests from the network.
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        
        // Rebuild available (visible) list from all_catalogs respecting hidden set.
        available_catalogs.clear();
        for (const auto& def : all_catalogs) {
            std::string key = def.type + ":" + def.id;
            if (!isCatalogHidden(key)) {
                available_catalogs.push_back(def);
            }
        }
        
        // Sort both lists by catalog_order.
        auto sortFunc = [this](const CatalogDef& a, const CatalogDef& b) {
            std::string keyA = a.type + ":" + a.id;
            std::string keyB = b.type + ":" + b.id;
            
            std::lock_guard<std::mutex> lock(prefs_mutex);
            auto itA = std::find(catalog_order.begin(), catalog_order.end(), keyA);
            auto itB = std::find(catalog_order.begin(), catalog_order.end(), keyB);
            
            return std::distance(catalog_order.begin(), itA) < std::distance(catalog_order.begin(), itB);
        };
        
        std::sort(available_catalogs.begin(), available_catalogs.end(), sortFunc);
        std::sort(all_catalogs.begin(), all_catalogs.end(), sortFunc);
    }
    
    notifyCatalogsChanged();
}

bool AddonManager::fetchManifest() {
    std::vector<std::string> addons_copy;
    {
        std::lock_guard<std::mutex> alock(addons_mutex);
        addons_copy = installed_addons;
    }
    
    // Build into temporaries first so we never hold a lock while doing
    // network I/O, and readers never see a partially-cleared catalog.
    std::vector<CatalogDef> new_available;
    std::vector<CatalogDef> new_all;
    std::vector<AddonManifest> new_manifests;
    
    bool any_success = false;
    bool needs_save = false;
    
    for (const auto& url : addons_copy) {
        std::string clean = sanitizeManifestUrl(url);
        if (clean.empty()) {
            LOG("[Network] ERROR: skipping invalid addon URL: " + url);
            continue;
        }
        std::string response;
        LOG("[Network] Fetching manifest: " + clean);
        if (!HttpClient::getInstance().get(clean, response)) {
            LOG("[Network] ERROR: Failed to fetch manifest for " + clean);
            continue;
        }
        
        try {
            json j = json::parse(response);
            if (j.contains("catalogs") && j["catalogs"].is_array()) {
                
                // Get the base url by removing manifest.json
                std::string base_url = clean;
                size_t pos = base_url.find("manifest.json");
                if (pos != std::string::npos) {
                    base_url = base_url.substr(0, pos);
                }
                
                AddonManifest manifest;
                manifest.url = clean;
                manifest.id = j.value("id", "");
                manifest.name = j.value("name", "Unknown Addon");
                manifest.version = j.value("version", "1.0.0");
                manifest.description = j.value("description", "");
                manifest.logo = j.value("logo", "");
                
                new_manifests.push_back(manifest);
                
                for (auto& cat : j["catalogs"]) {
                    CatalogDef def;
                    def.type = cat.value("type", "");
                    def.id = cat.value("id", "");
                    def.name = cat.value("name", "");
                    def.addon_url = base_url;
                    
                    std::string key = def.type + ":" + def.id;
                    
                    new_all.push_back(def);
                    
                    if (!isCatalogHidden(key)) {
                        new_available.push_back(def);
                    }
                    
                    // Add to order list if not present
                    {
                        std::lock_guard<std::mutex> plock(prefs_mutex);
                        if (std::find(catalog_order.begin(), catalog_order.end(), key) == catalog_order.end()) {
                            catalog_order.push_back(key);
                            needs_save = true;
                        }
                    }
                }
                any_success = true;
            }
        } catch (...) {
            LOG("[Network] ERROR: Failed to parse manifest for " + url);
        }
    }
    
    if (needs_save) {
        saveCatalogPrefs();
    }
    
    // Sort both lists by catalog_order
    auto sortFunc = [this](const CatalogDef& a, const CatalogDef& b) {
        std::string keyA = a.type + ":" + a.id;
        std::string keyB = b.type + ":" + b.id;
        
        std::lock_guard<std::mutex> lock(prefs_mutex);
        auto itA = std::find(catalog_order.begin(), catalog_order.end(), keyA);
        auto itB = std::find(catalog_order.begin(), catalog_order.end(), keyB);
        
        return std::distance(catalog_order.begin(), itA) < std::distance(catalog_order.begin(), itB);
    };
    
    std::sort(new_available.begin(), new_available.end(), sortFunc);
    std::sort(new_all.begin(), new_all.end(), sortFunc);
    
    // Commit atomically under the lock
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        available_catalogs.swap(new_available);
        all_catalogs.swap(new_all);
    }
    {
        std::lock_guard<std::mutex> alock(addons_mutex);
        installed_manifests.swap(new_manifests);
    }
    
    return any_success;
}

AddonManager::CatalogsChangedToken AddonManager::addCatalogsChangedCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(cb_mutex);
    CatalogsChangedToken token = next_cb_token++;
    catalogs_changed_cbs.emplace_back(token, std::move(cb));
    return token;
}

void AddonManager::removeCatalogsChangedCallback(CatalogsChangedToken token) {
    std::lock_guard<std::mutex> lock(cb_mutex);
    for (auto it = catalogs_changed_cbs.begin(); it != catalogs_changed_cbs.end(); ++it) {
        if (it->first == token) {
            catalogs_changed_cbs.erase(it);
            break;
        }
    }
}

void AddonManager::notifyCatalogsChanged() {
    std::vector<std::function<void()>> callbacks;
    {
        std::lock_guard<std::mutex> lock(cb_mutex);
        for (auto& entry : catalogs_changed_cbs) {
            callbacks.push_back(entry.second);
        }
    }
    for (auto& cb : callbacks) {
        if (cb) cb();
    }
}

void AddonManager::setActiveCatalog(const std::string& type, const std::string& id) {
    std::lock_guard<std::mutex> lock(catalog_mutex);
    active_type = type;
    active_id = id;
    
    // Find matching catalog to get the correct addon URL
    for (const auto& cat : available_catalogs) {
        if (cat.type == type && cat.id == id) {
            active_addon_url = cat.addon_url;
            break;
        }
    }
    
    current_skip = 0;
}

bool AddonManager::fetchCurrentCatalog(bool append) {
    // Serialize catalog item fetches: on reorder/hide the background reload
    // can trigger item fetches while the user is still navigating, and
    // concurrent curl + shared state access crashed the applet.
    std::lock_guard<std::mutex> fetchLock(fetch_mutex);
    if (active_type.empty() || active_id.empty()) return false;
    is_loading = true;
    
    if (append) {
        current_skip += 50;
    } else {
        current_skip = 0;
    }
    
    std::string url = active_addon_url + "catalog/" + active_type + "/" + active_id + "/skip=" + std::to_string(current_skip) + ".json";
    std::string response;

    LOG("[Network] Fetching catalog: " + url);
    if (!HttpClient::getInstance().get(url, response)) {
        LOG("[Network] ERROR: Failed to fetch catalog");
        return false;
    }

    try {
        LOG("[Network] Parsing catalog JSON...");
        json j = json::parse(response);
        if (j.contains("metas") && j["metas"].is_array()) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (!append) {
                current_catalog.clear();
            }
            for (auto& meta : j["metas"]) {
                MetaItem item;
                item.id = meta.value("id", "");
                item.type = meta.value("type", "");
                item.name = meta.value("name", "");
                item.poster_url = meta.value("poster", "");
                item.logo_url = meta.value("logo", "");
                item.description = meta.value("description", "");
                
                item.year = meta.value("year", "");
                item.runtime = meta.value("runtime", "");
                item.imdbRating = meta.value("imdbRating", "");
                item.background_url = meta.value("background", "");
                
                if (meta.contains("genre") && meta["genre"].is_array()) {
                    for (auto& g : meta["genre"]) item.genre.push_back(g.get<std::string>());
                }
                if (meta.contains("cast") && meta["cast"].is_array()) {
                    for (auto& c : meta["cast"]) item.cast.push_back(c.get<std::string>());
                }
                if (meta.contains("director") && meta["director"].is_array()) {
                    for (auto& d : meta["director"]) item.director.push_back(d.get<std::string>());
                }
                
                current_catalog.push_back(item);
            }
            LOG("[Network] Catalog parsed successfully. Items: " + std::to_string(current_catalog.size()));
            is_loading = false;
            return true;
        }
    } catch (const std::exception& e) {
        LOG(std::string("[Network] JSON parse ERROR: ") + e.what());
        std::cerr << "JSON parse error: " << e.what() << std::endl;
    }
    
    is_loading = false;
    return false;
}

void AddonManager::fetchStreams(const std::string& type, const std::string& id, std::function<void(const std::vector<StreamItem>&, bool loading)> callback) {
    std::vector<std::string> addons_copy;
    {
        std::lock_guard<std::mutex> alock(addons_mutex);
        addons_copy = installed_addons;
    }

    if (addons_copy.empty()) {
        if (callback) callback({}, false);
        return;
    }

    auto results = std::make_shared<std::vector<StreamItem>>();
    auto results_mutex = std::make_shared<std::mutex>();
    auto remaining = std::make_shared<std::atomic<int>>(addons_copy.size());
    
    for (const auto& addon : addons_copy) {
        std::string stream_url = addon;
        size_t pos = stream_url.find("manifest.json");
        if (pos != std::string::npos) {
            stream_url = stream_url.substr(0, pos) + "stream/" + type + "/" + id + ".json";
        } else {
            stream_url = stream_url + "/stream/" + type + "/" + id + ".json";
        }
        
        TaskQueue::getInstance().push([stream_url, addon, results, results_mutex, remaining, callback]() {
            std::string response;
            if (HttpClient::getInstance().get(stream_url, response)) {
                try {
                    json j = json::parse(response);
                    if (j.contains("streams") && j["streams"].is_array()) {
                        std::vector<StreamItem> temp;
                        for (auto& s : j["streams"]) {
                            StreamItem item;
                            item.name = s.value("name", "");
                            item.title = s.value("title", "");
                            item.description = s.value("description", "");
                            item.url = s.value("url", "");
                            // Prefer the explicit behaviorHints.cached flag when the
                            // addon provides it (Torrentio/MediaFusion/StremThru do).
                            // Otherwise fall back to the lightning bolt (U+26A1,
                            // UTF-8: E2 9A A1) marker in the stream name.
                            bool cached = false;
                            bool hasBehaviorHint = false;
                            if (s.contains("behaviorHints") && s["behaviorHints"].is_object() && s["behaviorHints"].contains("cached") && s["behaviorHints"]["cached"].is_boolean()) {
                                cached = s["behaviorHints"]["cached"].get<bool>();
                                hasBehaviorHint = true;
                            } else {
                                cached = item.name.find("\xE2\x9A\xA1") != std::string::npos;
                            }
                            item.cached = cached;
                            LOG("[Network] stream cached=" + std::string(cached ? "true" : "false") + " hint=" + std::string(hasBehaviorHint ? "yes" : "no") + " name=" + item.name);
                            item.infoHash = s.value("infoHash", "");
                            if (s.contains("fileIdx")) {
                                if (s["fileIdx"].is_number()) item.fileIdx = std::to_string(s["fileIdx"].get<int>());
                                else if (s["fileIdx"].is_string()) item.fileIdx = s["fileIdx"].get<std::string>();
                            }
                            item.ytId = s.value("ytId", "");
                            item.externalUrl = s.value("externalUrl", "");
                            item.addonName = addon; 
                            if (isResolutionOver1080p(item.name, item.title)) {
                                LOG("[Network] Skipping stream >1080p from " + addon + ": " + item.name);
                                continue;
                            }
                            temp.push_back(item);
                        }
                        
                        std::lock_guard<std::mutex> lock(*results_mutex);
                        for (auto& it : temp) {
                            bool dup = false;
                            for (auto& existing : *results) {
                                if (existing.url == it.url && existing.name == it.name) {
                                    dup = true;
                                    break;
                                }
                            }
                            if (!dup)
                                results->push_back(it);
                        }
                        // Report progress after each addon so the sidebar fills
                        // up as results arrive instead of staying blank while
                        // the slowest addon is still loading.
                        if (callback) callback(*results, true);
                    }
                } catch (...) {
                    LOG("[Network] ERROR parsing streams from: " + stream_url);
                }
            }
            
            int count = --(*remaining);
            if (count == 0) {
                if (callback) callback(*results, false);
            }
        });
    }
}

void AddonManager::fetchSeriesMeta(const std::string& id, std::function<void(const std::vector<EpisodeItem>&)> callback) {
    std::vector<std::string> addons_copy;
    {
        std::lock_guard<std::mutex> alock(addons_mutex);
        addons_copy = installed_addons;
    }

    if (addons_copy.empty()) {
        if (callback) callback({});
        return;
    }

    auto results = std::make_shared<std::vector<EpisodeItem>>();
    auto results_mutex = std::make_shared<std::mutex>();
    auto remaining = std::make_shared<std::atomic<int>>(addons_copy.size());

    for (const auto& addon : addons_copy) {
        std::string meta_url = addon;
        size_t pos = meta_url.find("manifest.json");
        if (pos != std::string::npos) {
            meta_url = meta_url.substr(0, pos) + "meta/series/" + id + ".json";
        } else {
            meta_url = meta_url + "/meta/series/" + id + ".json";
        }

        TaskQueue::getInstance().push([meta_url, addon, results, results_mutex, remaining, callback]() {
            std::string response;
            if (HttpClient::getInstance().get(meta_url, response)) {
                try {
                    json j = json::parse(response);
                    if (j.contains("meta") && j["meta"].is_object() && j["meta"].contains("videos") && j["meta"]["videos"].is_array()) {
                        std::vector<EpisodeItem> temp;
                        for (auto& v : j["meta"]["videos"]) {
                            EpisodeItem ep;
                            ep.id = v.value("id", "");
                            ep.name = v.value("name", "");
                            ep.season = v.value("season", 1);
                            ep.episode = v.value("episode", 1);
                            ep.overview = v.value("overview", "");
                            if (!ep.id.empty())
                                temp.push_back(ep);
                        }

                        if (!temp.empty()) {
                            LOG("[Network] Series meta episodes from " + addon + ": " + std::to_string(temp.size()));
                            std::lock_guard<std::mutex> lock(*results_mutex);
                            for (auto& it : temp) {
                                bool dup = false;
                                for (auto& existing : *results) {
                                    if (existing.id == it.id) {
                                        dup = true;
                                        break;
                                    }
                                }
                                if (!dup)
                                    results->push_back(it);
                            }
                        }
                    }
                } catch (...) {
                    LOG("[Network] ERROR parsing series meta from: " + meta_url);
                }
            }

            int count = --(*remaining);
            if (count == 0) {
                if (callback) callback(*results);
            }
        });
    }
}

std::string AddonManager::getCinemetaLogo() {
    std::lock_guard<std::mutex> alock(addons_mutex);
    for (const auto& m : installed_manifests) {
        if (m.url.find("cinemeta") != std::string::npos || m.id.find("cinemeta") != std::string::npos) {
            if (!m.logo.empty()) return m.logo;
        }
    }
    return "";
}


