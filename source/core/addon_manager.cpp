#include "addon_manager.hpp"
#include "http_client.hpp"
#include "file_paths.hpp"
#include <nlohmann/json.hpp>
#include <SDL2/SDL_image.h>
#include <iostream>
#include "logger.hpp"

#include <fstream>
#include <atomic>
#include <cstdio>
#include <pthread.h>
#include "task_queue.hpp"

using json = nlohmann::json;

// Normalizes a manifest URL. Returns an empty string if the URL is unusable.
// Some browsers append HTML/binary junk to the form value, so we cut at
// "manifest.json" and only accept well-formed http(s) URLs.
static std::string sanitizeManifestUrl(std::string url) {
    if (url.empty()) return "";
    // Normalize stremio:// to https://
    if (url.rfind("stremio://", 0) == 0) {
        url = "https://" + url.substr(10);
    }
    // Trim whitespace and special characters
    size_t start = url.find_first_not_of(" \t\r\n<\"'");
    if (start != std::string::npos) url = url.substr(start);
    size_t end = url.find_last_not_of(" \t\r\n>\"'");
    if (end != std::string::npos) url = url.substr(0, end + 1);

    size_t mj = url.rfind("manifest.json");
    if (mj != std::string::npos) {
        url = url.substr(0, mj) + "manifest.json";
    } else {
        if (!url.empty() && url.back() != '/') url += "/";
        url += "manifest.json";
    }
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return "";
    return url;
}

// Minimal RFC 3986 percent-encoding for search queries (spaces, accents,
// punctuation...). Unreserved characters are passed through untouched.
static std::string urlEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    char buf[4];
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

static std::string jsonStr(const json& obj, const char* key, const std::string& def = "") {
    if (obj.contains(key) && obj[key].is_string()) return obj[key].get<std::string>();
    return def;
}

static int jsonInt(const json& obj, const char* key, int def) {
    if (obj.contains(key) && obj[key].is_number_integer()) return obj[key].get<int>();
    return def;
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
    removeAddons(std::vector<std::string>{url});
}

void AddonManager::removeAddons(const std::vector<std::string>& urls) {
    if (urls.empty()) return;

    std::vector<std::string> toRemove;
    {
        std::lock_guard<std::mutex> lock(addons_mutex);
        for (const auto& url : urls) {
            auto it = std::find(installed_addons.begin(), installed_addons.end(), url);
            if (it != installed_addons.end()) {
                installed_addons.erase(it);
                toRemove.push_back(url);
            }
        }
        if (!toRemove.empty()) saveAddons();
    }
    if (toRemove.empty()) return;

    // Base URLs (without "manifest.json") used to match catalogs.
    std::vector<std::string> base_urls;
    for (const auto& url : toRemove) {
        std::string base_url = url;
        size_t pos = base_url.find("manifest.json");
        if (pos != std::string::npos) {
            base_url = base_url.substr(0, pos);
        }
        base_urls.push_back(base_url);
    }

    {
        std::lock_guard<std::mutex> lock(addons_mutex);
        installed_manifests.erase(
            std::remove_if(installed_manifests.begin(), installed_manifests.end(),
                           [&](const AddonManifest& m) {
                               return std::find(toRemove.begin(), toRemove.end(), m.url) != toRemove.end();
                           }),
            installed_manifests.end());
    }
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        auto fromRemovedAddon = [&](const CatalogDef& c) {
            return std::find(base_urls.begin(), base_urls.end(), c.addon_url) != base_urls.end();
        };
        available_catalogs.erase(
            std::remove_if(available_catalogs.begin(), available_catalogs.end(), fromRemovedAddon),
            available_catalogs.end());
        all_catalogs.erase(
            std::remove_if(all_catalogs.begin(), all_catalogs.end(), fromRemovedAddon),
            all_catalogs.end());
        if (std::find(base_urls.begin(), base_urls.end(), active_addon_url) != base_urls.end()) {
            active_addon_url.clear();
        }
    }

    notifyCatalogsChanged();
}

int AddonManager::installAddons(const std::vector<std::string>& urls) {
    int added = 0;
    {
        std::lock_guard<std::mutex> lock(addons_mutex);
        for (const auto& url : urls) {
            std::string clean = sanitizeManifestUrl(url);
            if (clean.empty()) continue;
            if (std::find(installed_addons.begin(), installed_addons.end(), clean) == installed_addons.end()) {
                installed_addons.push_back(clean);
                added++;
            }
        }
        if (added > 0) saveAddons();
    }
    if (added > 0) {
        fetchManifest();
        notifyCatalogsChanged();
    }
    return added;
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
        std::vector<std::string> order_copy;
        {
            std::lock_guard<std::mutex> plock(prefs_mutex);
            order_copy = catalog_order;
        }
        
        // Rebuild available (visible) list from all_catalogs respecting hidden set.
        available_catalogs.clear();
        for (const auto& def : all_catalogs) {
            std::string key = def.type + ":" + def.id;
            if (!isCatalogHidden(key)) {
                available_catalogs.push_back(def);
            }
        }
        
        // Sort both lists by catalog_order.
        auto sortFunc = [&order_copy](const CatalogDef& a, const CatalogDef& b) {
            std::string keyA = a.type + ":" + a.id;
            std::string keyB = b.type + ":" + b.id;
            
            auto itA = std::find(order_copy.begin(), order_copy.end(), keyA);
            auto itB = std::find(order_copy.begin(), order_copy.end(), keyB);
            
            return std::distance(order_copy.begin(), itA) < std::distance(order_copy.begin(), itB);
        };
        
        std::sort(available_catalogs.begin(), available_catalogs.end(), sortFunc);
        std::sort(all_catalogs.begin(), all_catalogs.end(), sortFunc);
    }
    
    notifyCatalogsChanged();
}

bool AddonManager::fetchManifest() {
    // Serialize manifest refreshes: addAddon/removeAddon run it synchronously
    // on the UI thread while background reload threads (catalog tab) may run
    // it at the same time; concurrent curl calls exhausted the applet heap
    // and crashed inside curl.
    std::lock_guard<std::mutex> fetchLock(fetch_mutex);
    
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
                manifest.id = jsonStr(j, "id");
                manifest.name = jsonStr(j, "name", "Unknown Addon");
                manifest.version = jsonStr(j, "version", "1.0.0");
                manifest.description = jsonStr(j, "description");
                manifest.logo = jsonStr(j, "logo");
                
                new_manifests.push_back(manifest);
                
                for (auto& cat : j["catalogs"]) {
                    CatalogDef def;
                    def.type = jsonStr(cat, "type");
                    def.id = jsonStr(cat, "id");
                    def.name = jsonStr(cat, "name");
                    def.addon_url = base_url;
                    def.searchable = false;
                    if (cat.contains("extra") && cat["extra"].is_array()) {
                        for (auto& e : cat["extra"]) {
                            if (e.is_object() && jsonStr(e, "name") == "search") {
                                def.searchable = true;
                                break;
                            }
                        }
                    }
                    
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
                item.id = jsonStr(meta, "id");
                item.type = jsonStr(meta, "type");
                item.name = jsonStr(meta, "name");
                item.poster_url = jsonStr(meta, "poster");
                item.logo_url = jsonStr(meta, "logo");
                item.description = jsonStr(meta, "description");
                
                item.year = jsonStr(meta, "year");
                item.runtime = jsonStr(meta, "runtime");
                item.imdbRating = jsonStr(meta, "imdbRating");
                item.background_url = jsonStr(meta, "background");
                
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
    auto remaining = std::make_shared<std::atomic<int>>((int)addons_copy.size());

    auto finish = [results, remaining, callback]() {
        if (--(*remaining) != 0) return;
        if (callback) callback(*results, false);
    };

    for (const auto& addon : addons_copy) {
        std::string stream_url = addon;
        size_t pos = stream_url.rfind("manifest.json");
        if (pos != std::string::npos) {
            stream_url = stream_url.substr(0, pos) + "stream/" + type + "/" + id + ".json";
        } else {
            if (!stream_url.empty() && stream_url.back() != '/') stream_url += "/";
            stream_url = stream_url + "stream/" + type + "/" + id + ".json";
        }

        // Each fetch runs on its own 2MB-stack thread: the default TaskQueue
        // worker stack is too small for nlohmann::json::parse on the large
        // stream responses (crash after a couple of details screens).
        struct ThreadData {
            std::string url;
            std::string addon;
            std::shared_ptr<std::vector<StreamItem>> results;
            std::shared_ptr<std::mutex> results_mutex;
            std::function<void()> finish;
            std::function<void(const std::vector<StreamItem>&, bool)> callback;
        };
        ThreadData* data = new ThreadData{stream_url, addon, results, results_mutex, finish, callback};

        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);

        auto threadFunc = [](void* arg) -> void* {
            ThreadData* d = static_cast<ThreadData*>(arg);
            std::string response;
            if (HttpClient::getInstance().get(d->url, response)) {
                try {
                    json j = json::parse(response);
                    if (j.contains("streams") && j["streams"].is_array()) {
                        std::vector<StreamItem> temp;
                        for (auto& s : j["streams"]) {
                            StreamItem item;
                            item.name = jsonStr(s, "name");
                            item.title = jsonStr(s, "title");
                            item.description = jsonStr(s, "description");
                            item.url = jsonStr(s, "url");
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
                            item.infoHash = jsonStr(s, "infoHash");
                            if (s.contains("fileIdx")) {
                                if (s["fileIdx"].is_number()) item.fileIdx = std::to_string(s["fileIdx"].get<int>());
                                else if (s["fileIdx"].is_string()) item.fileIdx = s["fileIdx"].get<std::string>();
                            }
                            item.ytId = jsonStr(s, "ytId");
                            item.externalUrl = jsonStr(s, "externalUrl");
                            item.addonName = d->addon;
                            if (s.contains("sources") && s["sources"].is_array()) {
                                for (const auto& src : s["sources"]) {
                                    if (src.is_string()) {
                                        item.sources.push_back(src.get<std::string>());
                                    }
                                }
                            }
                            temp.push_back(item);
                        }

                        LOG("[Network] fetchStreams parsed " + std::to_string(temp.size()) + " streams from: " + d->url);

                        std::lock_guard<std::mutex> lock(*d->results_mutex);
                        for (auto& it : temp) {
                            std::string key = it.url.empty() ? it.infoHash : it.url;
                            if (key.empty()) {
                                d->results->push_back(it);
                                continue;
                            }
                            bool dup = false;
                            for (auto& existing : *d->results) {
                                std::string ekey = existing.url.empty() ? existing.infoHash : existing.url;
                                if (!ekey.empty() && ekey == key) {
                                    dup = true;
                                    break;
                                }
                            }
                            if (!dup)
                                d->results->push_back(it);
                        }
                        // Report progress after each addon so the sidebar fills
                        // up as results arrive instead of staying blank while
                        // the slowest addon is still loading.
                        if (d->callback) d->callback(*d->results, true);
                    }
                } catch (...) {
                    LOG("[Network] ERROR parsing streams from: " + d->url);
                }
            } else {
                LOG("[Network] fetchStreams GET request failed for: " + d->url);
            }

            d->finish();
            delete d;
            return nullptr;
        };

        pthread_create(&thread, &attr, threadFunc, data);
        pthread_detach(thread);
        pthread_attr_destroy(&attr);
    }
}

void AddonManager::fetchSubtitles(const std::string& type, const std::string& id, std::function<void(const std::vector<SubtitleItem>&)> callback) {
    std::vector<std::string> addons_copy;
    {
        std::lock_guard<std::mutex> alock(addons_mutex);
        addons_copy = installed_addons;
    }

    if (addons_copy.empty()) {
        if (callback) callback({});
        return;
    }

    auto results = std::make_shared<std::vector<SubtitleItem>>();
    auto results_mutex = std::make_shared<std::mutex>();
    auto remaining = std::make_shared<std::atomic<int>>((int)addons_copy.size());

    auto finish = [results, remaining, callback]() {
        if (--(*remaining) != 0) return;
        if (callback) callback(*results);
    };

    for (const auto& addon : addons_copy) {
        std::string sub_url = addon;
        size_t pos = sub_url.find("manifest.json");
        if (pos != std::string::npos) {
            sub_url = sub_url.substr(0, pos) + "subtitles/" + type + "/" + id + ".json";
        } else {
            sub_url = sub_url + "/subtitles/" + type + "/" + id + ".json";
        }

        // Same 2MB-stack rationale as fetchStreams(): the JSON parse on the
        // subtitle responses can overflow the small TaskQueue worker stack.
        struct ThreadData {
            std::string url;
            std::string addon;
            std::shared_ptr<std::vector<SubtitleItem>> results;
            std::shared_ptr<std::mutex> results_mutex;
            std::function<void()> finish;
        };
        ThreadData* data = new ThreadData{sub_url, addon, results, results_mutex, finish};

        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);

        auto threadFunc = [](void* arg) -> void* {
            ThreadData* d = static_cast<ThreadData*>(arg);
            std::string response;
            if (HttpClient::getInstance().get(d->url, response)) {
                try {
                    json j = json::parse(response);
                    if (j.contains("subtitles") && j["subtitles"].is_array()) {
                        std::vector<SubtitleItem> temp;
                        for (auto& s : j["subtitles"]) {
                            SubtitleItem item;
                            item.id = jsonStr(s, "id");
                            item.url = jsonStr(s, "url");
                            item.lang = jsonStr(s, "lang");
                            item.name = jsonStr(s, "name");
                            item.encoding = jsonStr(s, "encoding");
                            item.addonName = d->addon;
                            if (item.url.empty()) continue;
                            temp.push_back(item);
                        }

                        std::lock_guard<std::mutex> lock(*d->results_mutex);
                        for (auto& it : temp) {
                            bool dup = false;
                            for (auto& existing : *d->results) {
                                if (existing.url == it.url) {
                                    dup = true;
                                    break;
                                }
                            }
                            if (!dup)
                                d->results->push_back(it);
                        }
                    }
                } catch (...) {
                    LOG("[Network] ERROR parsing subtitles from: " + d->url);
                }
            }
            d->finish();
            delete d;
            return nullptr;
        };

        pthread_create(&thread, &attr, threadFunc, data);
        pthread_detach(thread);
        pthread_attr_destroy(&attr);
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
    auto remaining = std::make_shared<std::atomic<int>>((int)addons_copy.size());

    auto finish = [results, remaining, callback]() {
        if (--(*remaining) != 0) return;
        if (callback) callback(*results);
    };

    for (const auto& addon : addons_copy) {
        std::string meta_url = addon;
        size_t pos = meta_url.rfind("manifest.json");
        if (pos != std::string::npos) {
            meta_url = meta_url.substr(0, pos) + "meta/series/" + id + ".json";
        } else {
            if (!meta_url.empty() && meta_url.back() != '/') meta_url += "/";
            meta_url = meta_url + "meta/series/" + id + ".json";
        }

        // Same 2MB-stack rationale as fetchStreams(): large meta responses
        // overflow the TaskQueue worker stack during json::parse.
        struct ThreadData {
            std::string url;
            std::string addon;
            std::shared_ptr<std::vector<EpisodeItem>> results;
            std::shared_ptr<std::mutex> results_mutex;
            std::function<void()> finish;
            std::function<void(const std::vector<EpisodeItem>&)> callback;
        };
        ThreadData* data = new ThreadData{meta_url, addon, results, results_mutex, finish, callback};

        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);

        auto threadFunc = [](void* arg) -> void* {
            ThreadData* d = static_cast<ThreadData*>(arg);
            std::string response;
            if (HttpClient::getInstance().get(d->url, response)) {
                try {
                    json j = json::parse(response);
                    if (j.contains("meta") && j["meta"].is_object() && j["meta"].contains("videos") && j["meta"]["videos"].is_array()) {
                        std::vector<EpisodeItem> temp;
                        for (auto& v : j["meta"]["videos"]) {
                            EpisodeItem ep;
                            ep.id = jsonStr(v, "id");
                            ep.name = jsonStr(v, "name");
                            ep.season = jsonInt(v, "season", 1);
                            ep.episode = jsonInt(v, "episode", 1);
                            ep.overview = jsonStr(v, "overview");
                            if (!ep.id.empty())
                                temp.push_back(ep);
                        }

                        if (!temp.empty()) {
                            LOG("[Network] Series meta episodes from " + d->addon + ": " + std::to_string(temp.size()));
                            std::lock_guard<std::mutex> lock(*d->results_mutex);
                            for (auto& it : temp) {
                                bool dup = false;
                                for (auto& existing : *d->results) {
                                    if (existing.id == it.id) {
                                        dup = true;
                                        break;
                                    }
                                }
                                if (!dup)
                                    d->results->push_back(it);
                            }
                        }
                    }
                } catch (...) {
                    LOG("[Network] ERROR parsing series meta from: " + d->url);
                }
            }

            d->finish();
            delete d;
            return nullptr;
        };

        pthread_create(&thread, &attr, threadFunc, data);
        pthread_detach(thread);
        pthread_attr_destroy(&attr);
    }
}

void AddonManager::fetchMeta(const std::string& type, const std::string& id,
                             std::function<void(const MetaItem&)> callback) {
    std::string base;
    {
        std::lock_guard<std::mutex> alock(addons_mutex);
        // Prefer Cinemeta (the canonical metadata provider) when installed.
        for (const auto& a : installed_addons) {
            if (a.find("cinemeta") != std::string::npos) {
                base = a;
                break;
            }
        }
        if (base.empty() && !installed_addons.empty()) base = installed_addons[0];
    }
    if (base.empty()) {
        if (callback) callback(MetaItem{});
        return;
    }

    std::string meta_url = base;
    size_t pos = meta_url.find("manifest.json");
    if (pos != std::string::npos) {
        meta_url = meta_url.substr(0, pos) + "meta/" + type + "/" + id + ".json";
    } else {
        meta_url = meta_url + "/meta/" + type + "/" + id + ".json";
    }

    // 2MB-stack thread: same rationale as fetchStreams()/searchCatalog().
    struct ThreadData {
        std::string url;
        std::function<void(const MetaItem&)> callback;
    };
    ThreadData* data = new ThreadData{meta_url, callback};

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);

    auto threadFunc = [](void* arg) -> void* {
        ThreadData* d = static_cast<ThreadData*>(arg);
        MetaItem item;
        std::string response;
        if (HttpClient::getInstance().get(d->url, response)) {
            try {
                json j = json::parse(response);
                if (j.contains("meta") && j["meta"].is_object()) {
                    auto& meta = j["meta"];
                    item.id = jsonStr(meta, "id");
                    item.type = jsonStr(meta, "type");
                    item.name = jsonStr(meta, "name");
                    item.poster_url = jsonStr(meta, "poster");
                    item.logo_url = jsonStr(meta, "logo");
                    item.description = jsonStr(meta, "description");
                    item.year = jsonStr(meta, "year");
                    item.runtime = jsonStr(meta, "runtime");
                    item.imdbRating = jsonStr(meta, "imdbRating");
                    item.background_url = jsonStr(meta, "background");
                    if (meta.contains("genre") && meta["genre"].is_array()) {
                        for (auto& g : meta["genre"]) item.genre.push_back(g.get<std::string>());
                    }
                    if (meta.contains("cast") && meta["cast"].is_array()) {
                        for (auto& c : meta["cast"]) item.cast.push_back(c.get<std::string>());
                    }
                    if (meta.contains("director") && meta["director"].is_array()) {
                        for (auto& d : meta["director"]) item.director.push_back(d.get<std::string>());
                    }
                }
            } catch (...) {
                LOG("[Network] ERROR parsing meta from: " + d->url);
            }
        }
        if (d->callback) d->callback(item);
        delete d;
        return nullptr;
    };

    pthread_create(&thread, &attr, threadFunc, data);
    pthread_detach(thread);
    pthread_attr_destroy(&attr);
}

void AddonManager::searchCatalog(const std::string& type, const std::string& query,
                                 std::function<void(const std::vector<MetaItem>&)> callback) {
    std::vector<std::string> types;
    if (type == "all" || type.empty()) {
        types = {"movie", "series"};
    } else {
        types = {type};
    }

    if (query.empty()) {
        if (callback) callback({});
        return;
    }

    // Collect one search endpoint per addon+type, preferring the catalogs that
    // declare a searchable `extra` (Stremio convention). Add-ons without one
    // fall back to the pseudo "search" catalog id.
    struct Endpoint {
        std::string base;
        std::string id;
        std::string type;
    };
    std::vector<Endpoint> endpoints;

    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        for (const auto& t : types) {
            for (const auto& cat : all_catalogs) {
                if (cat.type != t) continue;
                bool searchable = cat.searchable || cat.id == "search";
                if (!searchable) continue;
                bool dup = false;
                for (const auto& e : endpoints) {
                    if (e.base == cat.addon_url && e.type == t) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) endpoints.push_back({cat.addon_url, cat.id, t});
            }
        }
    }

    if (endpoints.empty()) {
        std::vector<std::string> addons;
        {
            std::lock_guard<std::mutex> alock(addons_mutex);
            addons = installed_addons;
        }
        for (const auto& a : addons) {
            std::string base = a;
            size_t pos = base.find("manifest.json");
            if (pos != std::string::npos) base = base.substr(0, pos);
            for (const auto& t : types) endpoints.push_back({base, "search", t});
        }
    }

    if (endpoints.empty()) {
        if (callback) callback({});
        return;
    }

    auto results = std::make_shared<std::vector<MetaItem>>();
    auto results_mutex = std::make_shared<std::mutex>();
    auto remaining = std::make_shared<std::atomic<int>>((int)endpoints.size());
    // Ensure the callback fires exactly once, even if an endpoint never replies.
    auto fired = std::make_shared<std::atomic<bool>>(false);
    std::string encoded = urlEncode(query);

    auto finish = [results, remaining, fired, callback]() {
        if (--(*remaining) != 0) return;
        bool expected = false;
        if (fired->compare_exchange_strong(expected, true)) {
            if (callback) callback(*results);
        }
    };

    for (const auto& ep : endpoints) {
        std::string url = ep.base + "catalog/" + ep.type + "/" + ep.id + "/search=" + encoded + ".json";

        // Each fetch runs on its own 2MB-stack thread: the default TaskQueue
        // worker stack is too small for nlohmann::json::parse on some
        // responses and crashed the applet right after the request finished.
        struct ThreadData {
            std::string url;
            std::shared_ptr<std::vector<MetaItem>> results;
            std::shared_ptr<std::mutex> results_mutex;
            std::function<void()> finish;
        };
        ThreadData* data = new ThreadData{url, results, results_mutex, finish};

        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);

        auto threadFunc = [](void* arg) -> void* {
            ThreadData* d = static_cast<ThreadData*>(arg);
            std::string response;
            if (HttpClient::getInstance().get(d->url, response)) {
                try {
                    json j = json::parse(response);
                    if (j.contains("metas") && j["metas"].is_array()) {
                        std::vector<MetaItem> temp;
                        for (auto& meta : j["metas"]) {
                            MetaItem item;
                            item.id = jsonStr(meta, "id");
                            item.type = jsonStr(meta, "type");
                            item.name = jsonStr(meta, "name");
                            item.poster_url = jsonStr(meta, "poster");
                            item.logo_url = jsonStr(meta, "logo");
                            item.description = jsonStr(meta, "description");
                            item.year = jsonStr(meta, "year");
                            item.runtime = jsonStr(meta, "runtime");
                            item.imdbRating = jsonStr(meta, "imdbRating");
                            item.background_url = jsonStr(meta, "background");

                            if (meta.contains("genre") && meta["genre"].is_array()) {
                                for (auto& g : meta["genre"]) item.genre.push_back(g.get<std::string>());
                            }
                            if (meta.contains("cast") && meta["cast"].is_array()) {
                                for (auto& c : meta["cast"]) item.cast.push_back(c.get<std::string>());
                            }
                            if (meta.contains("director") && meta["director"].is_array()) {
                                for (auto& d : meta["director"]) item.director.push_back(d.get<std::string>());
                            }

                            temp.push_back(item);
                        }

                        std::lock_guard<std::mutex> lock(*d->results_mutex);
                        for (auto& it : temp) {
                            bool dup = false;
                            for (auto& existing : *d->results) {
                                if (existing.id == it.id) {
                                    dup = true;
                                    break;
                                }
                            }
                            if (!dup) d->results->push_back(it);
                        }
                    }
                } catch (...) {
                    LOG("[Network] ERROR parsing search results from: " + d->url);
                }
            }
            d->finish();
            delete d;
            return nullptr;
        };

        pthread_create(&thread, &attr, threadFunc, data);
        pthread_detach(thread);
        pthread_attr_destroy(&attr);
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


