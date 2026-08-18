#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <functional>

struct MetaItem {
    std::string id;
    std::string type;
    std::string name;
    std::string poster_url;
    std::string logo_url;
    std::string description;
    
    // Extra Metadata
    std::string year;
    std::string runtime;
    std::string imdbRating;
    std::string background_url;
    std::vector<std::string> genre;
    std::vector<std::string> cast;
    std::vector<std::string> director;
};

struct CatalogDef {
    std::string type;
    std::string id;
    std::string name;
    std::string addon_url;
    bool searchable = false;
};

struct AddonManifest {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    std::string logo;
    std::string url;
};

struct StreamItem {
    std::string name;
    std::string title;
    std::string description;
    std::string url;
    std::string infoHash;
    std::string fileIdx;
    std::string ytId;
    std::string externalUrl;
    std::string addonName;
    bool cached = false;
};

// A single episode of a series, parsed from the Cinemeta (or any addon) meta
// endpoint. `id` is the full Stremio episode id ("<seriesId>:<season>:<episode>")
// that must be passed to fetchStreams() to obtain the episode's sources.
struct EpisodeItem {
    std::string id;
    std::string name;
    int season = 1;
    int episode = 1;
    std::string overview;
};

class AddonManager {
public:
    static AddonManager& getInstance();

    // Descarga el Manifest
    bool fetchManifest();

    // Setea el catálogo actual
    void setActiveCatalog(const std::string& type, const std::string& id);

    // Descarga el catálogo actual de Cinemeta
    bool fetchCurrentCatalog(bool append = false);



    // Convierte las descargas terminadas a Texturas SDL (DEBE SER LLAMADO EN MAIN THREAD)
    // No longer needed for Borealis
    // void processTextures(SDL_Renderer* renderer);

    // Fetch streams for a specific movie or series. The callback is invoked
    // once per addon with the streams gathered so far; `loading` is true until
    // every addon has responded. This lets the UI show a loader and populate
    // the list progressively instead of waiting for the slowest addon.
    void fetchStreams(const std::string& type, const std::string& id, std::function<void(const std::vector<StreamItem>&, bool loading)> callback);

    // Fetch the episode list of a series (from the addon meta endpoint,
    // e.g. Cinemeta). The callback fires once with every episode of every
    // season, grouped/ordered as returned by the addon.
    void fetchSeriesMeta(const std::string& id, std::function<void(const std::vector<EpisodeItem>&)> callback);

    // Search movies/series across the installed add-ons using their searchable
    // catalogs. `type` is "movie", "series" or "all" (both). The callback fires
    // once with the merged, de-duplicated results.
    void searchCatalog(const std::string& type, const std::string& query,
                       std::function<void(const std::vector<MetaItem>&)> callback);

    // Fetches the full metadata for a single item from the canonical meta
    // endpoint (<base>/meta/<type>/<id>.json, Cinemeta preferred). Search
    // results only carry a few fields; this enriches them with description,
    // genre, cast, director, runtime and rating. The callback fires once with
    // a MetaItem (possibly empty if the fetch failed).
    void fetchMeta(const std::string& type, const std::string& id,
                   std::function<void(const MetaItem&)> callback);

    void addAddon(const std::string& url);
    void removeAddon(const std::string& url);
    void loadAddons();
    void saveAddons();

    void loadCatalogPrefs();
    void saveCatalogPrefs();
    void toggleCatalogHidden(const std::string& key);
    void moveCatalogUp(const std::string& key);
    void moveCatalogDown(const std::string& key);
    bool isCatalogHidden(const std::string& key);
    // Lightweight: re-sorts the already-downloaded catalogs after a
    // reorder/hide change. Does NOT hit the network (unlike fetchManifest).
    void reorderCatalogs();

    // Thread-safe snapshots: return copies under lock.
    std::vector<MetaItem> getCatalog() {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        return current_catalog;
    }
    std::vector<CatalogDef> getAvailableCatalogs() {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        return available_catalogs;
    }
    std::vector<CatalogDef> getAllCatalogs() {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        return all_catalogs;
    }
    std::vector<AddonManifest> getInstalledManifests() { 
        std::lock_guard<std::mutex> lock(addons_mutex);
        return installed_manifests; 
    }
    std::mutex& getMutex() { return catalog_mutex; }

    // Logo URL of the Cinemeta addon, used as the loading image in the
    // player while the video buffers. Empty if Cinemeta isn't installed.
    std::string getCinemetaLogo();

    // Called whenever installed addons / catalogs / ordering change.
    // May be invoked from a background thread (web server). Do not touch
    // Borealis UI directly; use brls::sync() inside the callback.
    using CatalogsChangedToken = size_t;
    CatalogsChangedToken addCatalogsChangedCallback(std::function<void()> cb);
    void removeCatalogsChangedCallback(CatalogsChangedToken token);
    
    bool isLoading() const { return is_loading; }
    void setLoading(bool val) { is_loading = val; }

private:
    AddonManager();
    ~AddonManager();

    std::vector<CatalogDef> available_catalogs;
    std::vector<CatalogDef> all_catalogs;
    std::vector<MetaItem> current_catalog;
    std::vector<std::string> installed_addons;
    std::vector<AddonManifest> installed_manifests;
    
    std::vector<std::string> catalog_order;
    std::vector<std::string> hidden_catalogs;
    
    std::mutex catalog_mutex;
    std::mutex addons_mutex;
    std::mutex prefs_mutex;
    
    std::mutex cb_mutex;
    std::mutex fetch_mutex;

    CatalogsChangedToken next_cb_token = 0;
    std::vector<std::pair<CatalogsChangedToken, std::function<void()>>> catalogs_changed_cbs;

    void notifyCatalogsChanged();
    
    std::string active_type;
    std::string active_id;
    std::string active_addon_url;
    size_t current_skip = 0;
    bool is_loading = false;
};
