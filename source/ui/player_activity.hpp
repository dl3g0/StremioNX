#pragma once

#include <borealis.hpp>
#include <memory>
#include "ui/views/video_view.hpp"

class PlayerActivity : public brls::Activity {
public:
    PlayerActivity(const std::string& url,
                   const std::string& title = "StremioNX Player",
                   const std::string& loaderImageUrl = "",
                   const std::string& subtitlesType = "",
                   const std::string& subtitlesId = "");
    ~PlayerActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    VideoView* videoView = nullptr;
    // Guards the async subtitle fetch callback so it bails out instead of
    // touching a freed activity. Set false in the destructor.
    std::shared_ptr<bool> alive;
};


