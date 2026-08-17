#pragma once

#include <borealis.hpp>
#include "ui/views/video_view.hpp"

class PlayerActivity : public brls::Activity {
public:
    PlayerActivity(const std::string& url, const std::string& title = "StremioNX Player", const std::string& loaderImageUrl = "");
    ~PlayerActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    VideoView* videoView = nullptr;
};


