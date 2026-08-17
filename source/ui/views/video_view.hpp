#pragma once

#include <borealis.hpp>
#include "core/mpv_core.hpp"

class VideoView : public brls::Box {
public:
    VideoView();
    ~VideoView() override;

    void setUrl(const std::string& url);
    void setTitle(const std::string& title);
    // Loads an image (e.g. the Cinemeta logo) shown as a loading indicator
    // centered over the video while mpv is still buffering/loading.
    void setLoaderImage(const std::string& url);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;

    void togglePlay();
    void toggleOSD();
    void showOSD();
    void hideOSD();
    void seekTo(float progress);
    void seekRelative(double seconds);

    static brls::View* create() { return new VideoView(); }

private:
    std::string formatTime(int64_t totalSeconds);

    MPVCore* mpvCore;

    brls::Box* spacerTop;
    brls::Box* osdBox;
    brls::Label* titleLabel;
    brls::Label* statusLabel;
    brls::Slider* progressBar;
    brls::Label* timeLabel;
    brls::Label* hintsLabel;
    brls::Button* audioButton;
    brls::Button* subButton;

    brls::Image* loaderImage = nullptr;
    bool loaderImageReady = false;

    int64_t currentAudioId = -1;
    int64_t currentSubId   = -1;

    void refreshTrackUI();
    void openAudioDropdown();
    void openSubDropdown();
    std::string trackLabel(const std::vector<MPVCore::Track>& tracks, int64_t currentId, const std::string& noneText);

    bool isPaused = false;
    bool osdVisible = true;
    time_t osdLastShowTime = 0;
    bool updatingProgress = false;
};


