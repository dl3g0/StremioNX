#pragma once
#include <borealis.hpp>
#include <vector>
#include <string>
#include "../core/addon_manager.hpp"

class DetailsActivity : public brls::Activity {
public:
    DetailsActivity(const MetaItem& item);
    virtual ~DetailsActivity();

    MetaItem item;
    brls::Box* rootBox;
    
    // Currently fetched streams, kept so the "Fuentes" filter can re-render
    // the list without re-fetching.
    std::vector<StreamItem> fetchedStreams;
    // Empty = show all; otherwise only streams from this addon URL.
    std::string streamFilter;
    // True while addons are still being polled; prevents the "No streams"
    // message from showing before the fetch actually finished.
    bool streamsLoading = false;

    // Rebuilds the streams list applying the current streamFilter.
    void renderStreamList();
    // Friendly display name for an addon URL (falls back to host).
    static std::string addonDisplayName(const std::string& addonUrl);

    // --- Series support ---
    // True when the detail belongs to a series (item.type == "series").
    bool isSeries = false;
    // All episodes of the series (every season), from the addon meta endpoint.
    std::vector<EpisodeItem> episodes;
    // Currently selected season (1-based). Defaults to season 1.
    int currentSeason = 1;
    // Cached pointer to the "Fuentes" button; acts as a "Temporada" season
    // picker while browsing episodes and becomes the sources filter once an
    // episode has been selected.
    brls::View* btnFuentes = nullptr;
    // True once the user picked an episode and the sidebar switched to streams.
    bool episodeSelected = false;

    // Fetches and displays streams for a movie id or a full episode id.
    void loadStreams(const std::string& id);
    // Series flow: load episodes, render them and show the season dropdown.
    void loadSeriesMeta();
    // Pops up the "Temporada" dropdown (default = currentSeason).
    void showSeasonDropdown();
    // Renders the episodes of currentSeason into the sidebar list.
    void renderEpisodeList();
    // Called when an episode is clicked: loads its streams and reveals the
    // "Fuentes" button.
    void selectEpisode(const EpisodeItem& ep);
    
    brls::View* createContentView() override;
    void onContentAvailable() override;
};
