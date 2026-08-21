#pragma once

#include <borealis.hpp>
#include <atomic>
#include <memory>
#include <string>

class TorrentActivity : public brls::Activity {
public:
    TorrentActivity(const std::string& source, int fileIdx,
                    const std::string& title, const std::string& loaderImg,
                    const std::string& subtitlesType = "",
                    const std::string& subtitlesId = "");
    ~TorrentActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;
    void willDisappear(bool resetState) override;

private:
    void startPolling();
    void stopPolling();
    void cancelAndPop();

    brls::Label* stageLabel = nullptr;
    brls::Slider* progressBar = nullptr;
    brls::Label* statsLabel = nullptr;
    brls::Label* errorLabel = nullptr;
    brls::Button* btnCancel = nullptr;
    std::shared_ptr<bool> alive;
    pthread_t pollThread = 0;
    std::atomic<bool> launchedPlayer{false};
    std::string playerTitle;
    std::string playerLoader;
    std::string subtitlesType;
    std::string subtitlesId;
};