#include "ui/player_activity.hpp"

PlayerActivity::PlayerActivity(const std::string& url, const std::string& title, const std::string& loaderImageUrl) {
    videoView = new VideoView();
    videoView->setTitle(title);
    if (!loaderImageUrl.empty()) {
        videoView->setLoaderImage(loaderImageUrl);
    }
    videoView->setUrl(url);

    this->registerAction("Salir", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
}

PlayerActivity::~PlayerActivity() {
}

brls::View* PlayerActivity::createContentView() {
    return videoView;
}

void PlayerActivity::onContentAvailable() {
    if (videoView) {
        brls::Application::giveFocus(videoView);
    }
}


