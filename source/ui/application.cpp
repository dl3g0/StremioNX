#include "application.hpp"
#include <pthread.h>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include "../core/addon_manager.hpp"
#include "../core/logger.hpp"
#include <thread>
#include <borealis/core/box.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/image.hpp>
#include <borealis/views/button.hpp>
#include <borealis/views/dropdown.hpp>
#include <borealis/core/application.hpp>
#include "../core/http_client.hpp"
#include "../core/task_queue.hpp"
#include "player_activity.hpp"
#include "torrent_activity.hpp"
#include "../core/magnet_resolver.hpp"
#include "../core/playback_settings.hpp"
#include "../core/web_server.hpp"

using namespace brls;

static std::string cleanStreamText(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (unsigned char c : text) {
        if (c == '\n') {
            result += '\n';
        } else if (c == '\t' || c == '\r') {
            result += ' ';
        } else if (c >= 0x20 && c != 0x7F) {
            result += (char)c;
        }
    }
    // Collapse repeated spaces (but preserve newlines)
    std::string cleaned;
    bool prevSpace = false;
    for (char c : result) {
        if (c == ' ') {
            if (!prevSpace)
                cleaned += c;
            prevSpace = true;
        } else {
            cleaned += c;
            prevSpace = false;
        }
    }
    return cleaned;
}

static bool isResolutionOver1080p(const std::string& name, const std::string& title) {
    std::string n = name;
    std::string t = title;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    static const char* tokens[] = {"2160", "4320", "8k", "4k", "3840", "7680"};
    for (const char* tok : tokens) {
        if (n.find(tok) != std::string::npos || t.find(tok) != std::string::npos)
            return true;
    }
    if ((n.find("uhd") != std::string::npos ||
         t.find("uhd") != std::string::npos) &&
        n.find("1080p") == std::string::npos &&
        t.find("1080p") == std::string::npos)
        return true;
    return false;
}

// DetailsActivity

DetailsActivity::DetailsActivity(const MetaItem& item) : item(item) {
    alive = std::make_shared<bool>(true);
    rootBox = static_cast<brls::Box*>(brls::View::createFromXMLResource("activity/details.xml"));
    
    // Bind UI elements
    brls::Image* background = dynamic_cast<brls::Image*>(rootBox->getView("details/background"));
    
    // Populate simple data + pills
    applyMetaToViews();
    
    // Search results from Cinemeta only carry id/type/name/poster/background;
    // the rest (description, genre, cast, director, runtime, rating) is filled
    // in from the canonical meta endpoint when missing.
    if (item.description.empty() || item.genre.empty() || item.cast.empty()) {
        AddonManager::getInstance().fetchMeta(item.type, item.id, [this, alive = this->alive](const MetaItem& full) {
            brls::sync([this, alive, full]() {
                if (!*alive) return;
                if (full.id.empty() && full.name.empty()) return;
                if (!full.name.empty()) this->item.name = full.name;
                if (!full.logo_url.empty()) this->item.logo_url = full.logo_url;
                if (!full.description.empty()) this->item.description = full.description;
                if (!full.year.empty()) this->item.year = full.year;
                if (!full.runtime.empty()) this->item.runtime = full.runtime;
                if (!full.imdbRating.empty()) this->item.imdbRating = full.imdbRating;
                if (!full.background_url.empty()) this->item.background_url = full.background_url;
                if (!full.genre.empty()) this->item.genre = full.genre;
                if (!full.cast.empty()) this->item.cast = full.cast;
                if (!full.director.empty()) this->item.director = full.director;
                this->applyMetaToViews();
            });
        });
    }
    
    // Wire up Addons button
    brls::Button* btnAddons = dynamic_cast<brls::Button*>(rootBox->getView("details/btn_addons"));
    if (btnAddons) {
        btnAddons->registerClickAction([](brls::View* view) {
            std::string ip = WebServer::getInstance().getLocalIP();
            brls::Dialog* d = new brls::Dialog(
                "Instalar complementos\n\nPuedes administrar complementos desde la pestana Ajustes en el menu principal o navegando a:\nhttp://" + ip + ":8080");
            d->addButton("Aceptar", []() {});
            d->open();
            return true;
        });
    }
    
    // Wire up the "Fuentes" filter button: dropdown to filter the stream list
    // by source addon. Default shows all sources.
    brls::Button* btnFuentes = dynamic_cast<brls::Button*>(rootBox->getView("details/btn_fuentes"));
    if (btnFuentes) {
        btnFuentes->registerClickAction([this](brls::View* view) {
            // For a series, while the user is still picking an episode the
            // button re-opens the season picker; once an episode is selected
            // it filters the sources like a movie.
            if (this->isSeries && !this->episodeSelected) {
                this->showSeasonDropdown();
                return true;
            }

            // Collect the unique addons that actually returned streams.
            std::vector<std::string> addonUrls;
            for (const auto& s : fetchedStreams) {
                if (std::find(addonUrls.begin(), addonUrls.end(), s.addonName) == addonUrls.end())
                    addonUrls.push_back(s.addonName);
            }
            
            std::vector<std::string> values;
            std::vector<std::string> urls;
            int selected = 0;
            
            values.push_back("Todos");
            urls.push_back("");
            
            for (size_t i = 0; i < addonUrls.size(); i++) {
                values.push_back(addonDisplayName(addonUrls[i]));
                urls.push_back(addonUrls[i]);
                if (streamFilter == addonUrls[i])
                    selected = (int)(i + 1);
            }
            
            auto* dropdown = new brls::Dropdown("Fuentes", values, [this, urls, values](int index) {
                if (index < 0 || index >= (int)urls.size()) return;
                streamFilter = urls[index];
                
                brls::Label* titleLbl = dynamic_cast<brls::Label*>(rootBox->getView("details/streams_title"));
                if (titleLbl) titleLbl->setText(values[index]);
                
                // Briefly show the loader while the filtered list is rebuilt so
                // the sidebar never looks blank when switching sources.
                brls::View* loader = rootBox->getView("details/streams_loader");
                brls::View* scroll = rootBox->getView("details/streams_scroll");
                if (loader) loader->setVisibility(brls::Visibility::VISIBLE);
                if (scroll) scroll->setVisibility(brls::Visibility::GONE);
                
                renderStreamList();
                
                brls::delay(200, [this, alive = this->alive]() {
                    if (!*alive) return;
                    brls::View* loader2 = rootBox->getView("details/streams_loader");
                    brls::View* scroll2 = rootBox->getView("details/streams_scroll");
                    brls::View* noStreams = rootBox->getView("details/no_streams_box");
                    if (loader2) loader2->setVisibility(brls::Visibility::GONE);
                    if (scroll2) scroll2->setVisibility(brls::Visibility::VISIBLE);
                    if (noStreams) noStreams->setVisibility(brls::Visibility::GONE);
                });
            }, selected);
            
            brls::Application::pushActivity(new brls::Activity(dropdown));
            return true;
        });
    }
    
    // Load background image asynchronously
    if (background && !item.background_url.empty()) {
        std::string url = item.background_url;
        if (url.find("//") == 0) {
            url = "https:" + url;
        }
        
        TaskQueue::getInstance().push([background, url, alive = this->alive]() {
            std::vector<unsigned char> data;
            if (HttpClient::getInstance().getBinary(url, data) && !data.empty()) {
                brls::sync([background, alive, data = std::move(data)]() {
                    if (!*alive) return;
                    background->setImageFromMem(data.data(), data.size());
                });
            }
        });
    }
    
    // Series vs movie flow: a series shows a season picker + episode list in
    // the sidebar first; streams load only after an episode is selected.
    isSeries = (item.type == "series");
    this->btnFuentes = rootBox->getView("details/btn_fuentes");

    if (isSeries) {
        // The button becomes a season picker while browsing episodes.
        if (this->btnFuentes) {
            brls::Button* b = dynamic_cast<brls::Button*>(this->btnFuentes);
            if (b) b->setText("Temporada");
            this->btnFuentes->setVisibility(brls::Visibility::VISIBLE);
        }
        loadSeriesMeta();
    } else {
        loadStreams(item.id);
    }
}

void DetailsActivity::loadStreams(const std::string& id) {
    brls::View* loader = rootBox->getView("details/streams_loader");
    brls::View* noStreams = rootBox->getView("details/no_streams_box");
    brls::View* scroll = rootBox->getView("details/streams_scroll");

    if (loader) loader->setVisibility(brls::Visibility::VISIBLE);
    if (noStreams) noStreams->setVisibility(brls::Visibility::GONE);
    if (scroll) scroll->setVisibility(brls::Visibility::GONE);

    brls::Label* loaderLbl = dynamic_cast<brls::Label*>(rootBox->getView("details/streams_loader_text"));
    if (loaderLbl) loaderLbl->setText("Cargando fuentes...");

    streamFilter.clear();
    fetchedStreams.clear();
    streamsLoading = true;
    activeStreamId = id;

    // Progressive callback: gets called once per addon (loading=true) and a
    // final time when every addon has answered (loading=false).
    AddonManager::getInstance().fetchStreams(item.type, id, [this, alive = this->alive](const std::vector<StreamItem>& streams, bool loading) {
        brls::sync([this, alive, streams, loading]() {
            if (!*alive) return;
            
            brls::View* s_loader = rootBox->getView("details/streams_loader");
            brls::View* s_noStreams = rootBox->getView("details/no_streams_box");
            brls::View* s_scroll = rootBox->getView("details/streams_scroll");
            
            fetchedStreams = streams;
            streamsLoading = loading;
            
            if (loading) {
                // Show whatever streams have arrived so far; the list just
                // grows as addons respond. The full-height loader only stays
                // visible while there is nothing to show yet, so it never
                // sits as a small box above the list.
                if (streams.empty()) {
                    if (s_loader) s_loader->setVisibility(brls::Visibility::VISIBLE);
                    if (s_scroll) s_scroll->setVisibility(brls::Visibility::GONE);
                    if (s_noStreams) s_noStreams->setVisibility(brls::Visibility::GONE);
                } else {
                    if (s_loader) s_loader->setVisibility(brls::Visibility::GONE);
                    if (s_scroll) s_scroll->setVisibility(brls::Visibility::VISIBLE);
                    if (s_noStreams) s_noStreams->setVisibility(brls::Visibility::GONE);
                    renderStreamList();
                }
                return;
            }
            
            if (s_loader) s_loader->setVisibility(brls::Visibility::GONE);
            
            if (streams.empty()) {
                if (s_noStreams) s_noStreams->setVisibility(brls::Visibility::VISIBLE);
                if (s_scroll) s_scroll->setVisibility(brls::Visibility::GONE);
            } else {
                if (s_noStreams) s_noStreams->setVisibility(brls::Visibility::GONE);
                if (s_scroll) s_scroll->setVisibility(brls::Visibility::VISIBLE);
                renderStreamList();
            }
        });
    });
}

void DetailsActivity::loadSeriesMeta() {
    brls::View* loader = rootBox->getView("details/streams_loader");
    brls::View* noStreams = rootBox->getView("details/no_streams_box");
    brls::View* scroll = rootBox->getView("details/streams_scroll");

    if (loader) loader->setVisibility(brls::Visibility::VISIBLE);
    if (noStreams) noStreams->setVisibility(brls::Visibility::GONE);
    if (scroll) scroll->setVisibility(brls::Visibility::GONE);

    brls::Label* loaderLbl = dynamic_cast<brls::Label*>(rootBox->getView("details/streams_loader_text"));
    if (loaderLbl) loaderLbl->setText("Cargando capítulos...");

    AddonManager::getInstance().fetchSeriesMeta(item.id, [this, alive = this->alive](const std::vector<EpisodeItem>& eps) {
        brls::sync([this, alive, eps]() {
            if (!*alive) return;

            episodes = eps;

            brls::View* s_loader = rootBox->getView("details/streams_loader");
            if (s_loader) s_loader->setVisibility(brls::Visibility::GONE);

            if (eps.empty()) {
                brls::View* s_noStreams = rootBox->getView("details/no_streams_box");
                brls::View* s_scroll = rootBox->getView("details/streams_scroll");
                if (s_noStreams) {
                    s_noStreams->setVisibility(brls::Visibility::VISIBLE);
                    brls::Label* txt = dynamic_cast<brls::Label*>(rootBox->getView("details/no_streams_text"));
                    if (txt) txt->setText("No episodes found");
                }
                if (s_scroll) s_scroll->setVisibility(brls::Visibility::GONE);
                return;
            }

            // Season 1 is the default.
            currentSeason = 1;
            renderEpisodeList();

            // The season picker pops up on entry so the user can switch to a
            // different season before choosing an episode.
            showSeasonDropdown();
        });
    });
}

void DetailsActivity::showSeasonDropdown() {
    std::vector<int> seasons;
    for (const auto& e : episodes) {
        if (std::find(seasons.begin(), seasons.end(), e.season) == seasons.end())
            seasons.push_back(e.season);
    }
    std::sort(seasons.begin(), seasons.end());
    if (seasons.empty()) return;

    std::vector<std::string> values;
    int selected = 0;
    for (size_t i = 0; i < seasons.size(); i++) {
        values.push_back("Temporada " + std::to_string(seasons[i]));
        if (seasons[i] == currentSeason) selected = (int)i;
    }

    auto* dropdown = new brls::Dropdown("Temporada", values, [this, seasons](int index) {
        if (index < 0 || index >= (int)seasons.size()) return;
        this->currentSeason = seasons[index];
        this->renderEpisodeList();
    }, selected);

    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void DetailsActivity::renderEpisodeList() {
    brls::Box* s_list = dynamic_cast<brls::Box*>(rootBox->getView("details/streams_list"));
    if (!s_list) return;

    brls::Label* titleLbl = dynamic_cast<brls::Label*>(rootBox->getView("details/streams_title"));
    if (titleLbl) titleLbl->setText("Temporada " + std::to_string(currentSeason));

    // Keep the button acting as a season picker while browsing episodes.
    if (this->btnFuentes) {
        brls::Button* b = dynamic_cast<brls::Button*>(this->btnFuentes);
        if (b) b->setText("Temporada");
        this->btnFuentes->setVisibility(brls::Visibility::VISIBLE);
    }

    brls::View* s_noStreams = rootBox->getView("details/no_streams_box");
    brls::View* s_scroll = rootBox->getView("details/streams_scroll");

    s_list->clearViews();
    brls::View* firstCell = nullptr;

    for (const auto& ep : episodes) {
        if (ep.season != currentSeason) continue;

        brls::Box* cell = new brls::Box(brls::Axis::COLUMN);
        cell->setFocusable(true);
        cell->setPadding(6);
        cell->setMarginBottom(6);
        cell->setCornerRadius(6);
        cell->setWidthPercentage(100);
        if (!firstCell) firstCell = cell;

        std::string epName = "Episodio " + std::to_string(ep.episode);
        if (!ep.name.empty()) epName += " - " + ep.name;

        brls::Label* nameLbl = new brls::Label();
        nameLbl->setText(cleanStreamText(epName));
        nameLbl->setFontSize(21);
        nameLbl->setWidthPercentage(100);
        nameLbl->setMarginBottom(4);

        brls::Label* overviewLbl = new brls::Label();
        overviewLbl->setText(cleanStreamText(ep.overview));
        overviewLbl->setFontSize(14);
        overviewLbl->setWidthPercentage(100);
        overviewLbl->setSingleLine(false);

        cell->addView(nameLbl);
        cell->addView(overviewLbl);

        cell->registerClickAction([this, ep](brls::View* view) {
            this->selectEpisode(ep);
            return true;
        });

        s_list->addView(cell);
    }

    if (s_noStreams) s_noStreams->setVisibility(brls::Visibility::GONE);
    if (s_scroll) s_scroll->setVisibility(brls::Visibility::VISIBLE);

    if (firstCell) {
        brls::Application::giveFocus(firstCell);
    }
}

void DetailsActivity::selectEpisode(const EpisodeItem& ep) {
    episodeSelected = true;
    if (btnFuentes) {
        brls::Button* b = dynamic_cast<brls::Button*>(this->btnFuentes);
        if (b) b->setText("Fuentes");
        btnFuentes->setVisibility(brls::Visibility::VISIBLE);
    }

    brls::Label* titleLbl = dynamic_cast<brls::Label*>(rootBox->getView("details/streams_title"));
    if (titleLbl) titleLbl->setText("T" + std::to_string(ep.season) + "E" + std::to_string(ep.episode));

    loadStreams(ep.id);
}

// Friendly display name for an addon URL (falls back to the host).
std::string DetailsActivity::addonDisplayName(const std::string& addonUrl) {
    auto manifests = AddonManager::getInstance().getInstalledManifests();
    for (const auto& m : manifests) {
        if (m.url == addonUrl && !m.name.empty()) return m.name;
    }
    std::string host = addonUrl;
    size_t scheme = host.find("://");
    if (scheme != std::string::npos) host = host.substr(scheme + 3);
    size_t slash = host.find('/');
    if (slash != std::string::npos) host = host.substr(0, slash);
    return host.empty() ? addonUrl : host;
}

void DetailsActivity::renderStreamList() {
    brls::Box* s_list = dynamic_cast<brls::Box*>(rootBox->getView("details/streams_list"));
    if (!s_list) return;
    
    s_list->clearViews();
    brls::View* firstCell = nullptr;
    for (const auto& stream : fetchedStreams) {
        if (!streamFilter.empty() && stream.addonName != streamFilter) continue;
        if (!PlaybackSettings::getInstance().show4KSources() &&
            isResolutionOver1080p(stream.name, stream.title))
            continue;
        
        brls::Box* cell = new brls::Box(brls::Axis::COLUMN);
        cell->setFocusable(true);
        cell->setPadding(6);
        cell->setMarginBottom(6);
        cell->setCornerRadius(6);
        cell->setWidthPercentage(100);
        if (!firstCell) firstCell = cell;
        
        brls::Label* nameLbl = new brls::Label();
        std::string nameText = cleanStreamText(stream.name.empty() ? stream.title : stream.name);
        nameText = cleanStreamText(nameText);
        nameLbl->setText(nameText);
        nameLbl->setFontSize(24);
        nameLbl->setWidthPercentage(100);
        nameLbl->setMarginBottom(4);

        brls::Label* titleLbl = new brls::Label();
        titleLbl->setText(cleanStreamText(stream.title.empty() ? stream.description : stream.title));
        titleLbl->setFontSize(15);
        titleLbl->setWidthPercentage(100);
        titleLbl->setSingleLine(false);
        
        cell->addView(nameLbl);
        cell->addView(titleLbl);
        
        cell->registerClickAction([this, stream](brls::View* view) {
            std::string streamTitle = stream.title.empty() ? (stream.name.empty() ? stream.addonName : stream.name) : stream.title;
            // Loading image in the player: prefer the item's own logo, falling
            // back to the poster when the catalog has no logo for it.
            std::string loaderImg = this->item.logo_url;
            if (loaderImg.empty()) loaderImg = this->item.poster_url;
            std::string source = stream.url;
            bool isTorrent = stremio_torrent::MagnetResolver::isMagnet(source) ||
                             stremio_torrent::MagnetResolver::isTorrentUrl(source);
            if (!isTorrent && !stream.infoHash.empty()) {
                source = stremio_torrent::MagnetResolver::buildMagnet(stream.infoHash, stream.sources);
                isTorrent = true;
            } else if (isTorrent &&
                       stremio_torrent::MagnetResolver::isMagnet(source)) {
                source = stremio_torrent::MagnetResolver::appendTrackers(source, stream.sources);
            }
            if (isTorrent) {
                int fileIdx = stream.fileIdx.empty()
                                  ? -1
                                  : atoi(stream.fileIdx.c_str());
                brls::Application::pushActivity(
                    new TorrentActivity(source, fileIdx, streamTitle, loaderImg,
                                        item.type, activeStreamId),
                    brls::TransitionAnimation::FADE);
            } else {
                brls::Application::pushActivity(
                    new PlayerActivity(source, streamTitle, loaderImg,
                                       item.type, activeStreamId));
            }
            return true;
        });
        
        s_list->addView(cell);
    }

    // Give focus to the first stream so the highlight border
    // sits on the cell instead of the whole streams panel.
    if (firstCell) {
        brls::Application::giveFocus(firstCell);
    } else if (!streamsLoading) {
        // Filter produced no streams (e.g. selected source had none).
        brls::View* s_noStreams = rootBox->getView("details/no_streams_box");
        brls::View* s_scroll = rootBox->getView("details/streams_scroll");
        if (s_noStreams) s_noStreams->setVisibility(brls::Visibility::VISIBLE);
        if (s_scroll) s_scroll->setVisibility(brls::Visibility::GONE);
    }
}

DetailsActivity::~DetailsActivity() {
    *alive = false;
}

void DetailsActivity::applyMetaToViews() {
    brls::Label* title = dynamic_cast<brls::Label*>(rootBox->getView("details/title"));
    brls::Label* runtime = dynamic_cast<brls::Label*>(rootBox->getView("details/runtime"));
    brls::Label* year = dynamic_cast<brls::Label*>(rootBox->getView("details/year"));
    brls::Label* rating = dynamic_cast<brls::Label*>(rootBox->getView("details/rating"));
    brls::Label* summary = dynamic_cast<brls::Label*>(rootBox->getView("details/summary"));

    if (title) title->setText(item.name);
    if (runtime) runtime->setText(item.runtime.empty() ? "" : item.runtime);
    if (year) year->setText(item.year.empty() ? "" : item.year);
    if (rating) rating->setText(item.imdbRating.empty() ? "" : item.imdbRating + " IMDb");
    if (summary) summary->setText(item.description);

    auto rebuildPills = [this](const char* viewName, const std::vector<std::string>& items) {
        brls::Box* container = dynamic_cast<brls::Box*>(rootBox->getView(viewName));
        if (!container) return;
        container->clearViews();
        for (const auto& txt : items) {
            brls::Box* pill = new brls::Box(brls::Axis::ROW);
            pill->setCornerRadius(15);
            pill->setBackgroundColor(brls::Application::getTheme()["color/grey_4"]);
            pill->setPaddingTop(5); pill->setPaddingBottom(5);
            pill->setPaddingLeft(15); pill->setPaddingRight(15);
            pill->setMarginRight(10);
            pill->setMarginBottom(10);

            brls::Label* lbl = new brls::Label();
            lbl->setText(txt);
            lbl->setFontSize(14);
            pill->addView(lbl);

            container->addView(pill);
        }
    };
    rebuildPills("details/genres", item.genre);
    rebuildPills("details/cast", item.cast);
    rebuildPills("details/directors", item.director);
}

brls::View* DetailsActivity::createContentView() {
    return rootBox;
}

void DetailsActivity::onContentAvailable() {
    this->registerAction("Back", brls::BUTTON_B, [this](brls::View* view) {
        // In a series, once an episode has been selected B goes back to the
        // episode list instead of leaving the whole details screen.
        if (this->isSeries && this->episodeSelected) {
            this->episodeSelected = false;
            if (this->btnFuentes) {
                brls::Button* b = dynamic_cast<brls::Button*>(this->btnFuentes);
                if (b) b->setText("Temporada");
                this->btnFuentes->setVisibility(brls::Visibility::VISIBLE);
            }
            this->renderEpisodeList();
            return true;
        }
        brls::Application::popActivity();
        return true;
    });
}
