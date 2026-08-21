#include "ui/player_activity.hpp"
#include "core/torrent_player.hpp"
#include "core/mpv_core.hpp"
#include "core/addon_manager.hpp"

PlayerActivity::PlayerActivity(const std::string& url, const std::string& title, const std::string& loaderImageUrl, const std::string& subtitlesType, const std::string& subtitlesId) {
    alive = std::make_shared<bool>(true);

    videoView = new VideoView();
    videoView->setTitle(title);
    if (!loaderImageUrl.empty()) {
        videoView->setLoaderImage(loaderImageUrl);
    }
    videoView->setUrl(url);

    // Fetch subtitles for this video from the subtitle addons (e.g.
    // OpenSubtitles) in the background; they load into mpv as soon as they
    // arrive and the file is ready. Guarded by `alive` so a late response
    // after leaving the player does not touch a freed activity.
    if (!subtitlesType.empty() && !subtitlesId.empty()) {
        AddonManager::getInstance().fetchSubtitles(subtitlesType, subtitlesId,
            [this, alive = this->alive](const std::vector<SubtitleItem>& subs) {
                brls::sync([this, alive, subs]() {
                    if (!*alive) return;
                    if (videoView) videoView->setSubtitles(subs);
                });
            });
    }

    this->registerAction("Salir", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
}

PlayerActivity::~PlayerActivity() {
    *alive = false;
    MPVCore::instance().stopSync();
    auto& player = stremio_torrent::TorrentPlayer::getInstance();
    player.cancel();
    player.close();
}

brls::View* PlayerActivity::createContentView() {
    return videoView;
}

void PlayerActivity::onContentAvailable() {
    if (videoView) {
        brls::Application::giveFocus(videoView);
    }
}