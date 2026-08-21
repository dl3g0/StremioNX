#include "ui/torrent_activity.hpp"
#include "core/torrent_player.hpp"
#include "ui/player_activity.hpp"

#include <borealis/core/logger.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/progress_spinner.hpp>
#include <borealis/views/rectangle.hpp>
#include <borealis/views/slider.hpp>

#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <string>
#include <thread>

namespace {
std::string formatBytes(uint64_t bytes) {
    char buf[64];
    if (bytes >= 1024 * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024));
    else
        snprintf(buf, sizeof(buf), "%.0f KB", bytes / 1024.0);
    return buf;
}
} // namespace

TorrentActivity::TorrentActivity(const std::string& source, int fileIdx,
                                 const std::string& title,
                                 const std::string& loaderImg,
                                 const std::string& subtitlesType,
                                 const std::string& subtitlesId)
    : playerTitle(title), playerLoader(loaderImg),
      subtitlesType(subtitlesType), subtitlesId(subtitlesId) {
    alive = std::make_shared<bool>(true);

    std::string startErr =
        stremio_torrent::TorrentPlayer::getInstance().start(source, fileIdx);
    if (!startErr.empty()) {
        if (errorLabel)
            errorLabel->setText(startErr);
        stageLabel = nullptr;
        progressBar = nullptr;
    }
}

TorrentActivity::~TorrentActivity() {
    stopPolling();
}

void TorrentActivity::cancelAndPop() {
    if (!alive || !*alive) return;
    *alive = false;
    stopPolling();
    std::thread([]() {
        stremio_torrent::TorrentPlayer::getInstance().stop();
    }).detach();
    brls::Application::popActivity();
}

brls::View* TorrentActivity::createContentView() {
    brls::Box* root = new brls::Box(brls::Axis::COLUMN);
    root->setWidthPercentage(100);
    root->setHeightPercentage(100);
    root->setAlignItems(brls::AlignItems::CENTER);
    root->setJustifyContent(brls::JustifyContent::CENTER);
    root->setPadding(40);

    brls::Label* titleLbl = new brls::Label();
    titleLbl->setText("Streaming por torrent");
    titleLbl->setFontSize(26);
    titleLbl->setTextColor(nvgRGB(120, 220, 255));
    root->addView(titleLbl);

    brls::ProgressSpinner* spinner = new brls::ProgressSpinner(brls::ProgressSpinnerSize::LARGE);
    spinner->setMarginTop(30);
    spinner->setMarginBottom(30);
    root->addView(spinner);

    stageLabel = new brls::Label();
    stageLabel->setText("Iniciando...");
    stageLabel->setFontSize(20);
    stageLabel->setWidthPercentage(100);
    stageLabel->setTextColor(nvgRGB(220, 220, 220));
    root->addView(stageLabel);

    progressBar = new brls::Slider();
    progressBar->setWidthPercentage(100);
    progressBar->setHeight(14);
    progressBar->setHideHighlight(true);
    progressBar->setProgress(0.0f);
    progressBar->setMarginTop(25);
    root->addView(progressBar);

    statsLabel = new brls::Label();
    statsLabel->setText("0% · 0 pares · 0 KB/s");
    statsLabel->setFontSize(17);
    statsLabel->setWidthPercentage(100);
    statsLabel->setTextColor(nvgRGB(180, 180, 180));
    statsLabel->setMarginTop(20);
    root->addView(statsLabel);

    errorLabel = new brls::Label();
    errorLabel->setText("");
    errorLabel->setFontSize(17);
    errorLabel->setWidthPercentage(100);
    errorLabel->setSingleLine(false);
    errorLabel->setTextColor(nvgRGB(255, 110, 110));
    errorLabel->setMarginTop(20);
    root->addView(errorLabel);

    btnCancel = new brls::Button();
    btnCancel->setText("Cancelar (B)");
    btnCancel->setFontSize(18);
    btnCancel->setMarginTop(30);
    btnCancel->setPadding(10, 30, 10, 30);
    btnCancel->setCornerRadius(6);
    btnCancel->registerClickAction([this](brls::View* view) {
        this->cancelAndPop();
        return true;
    });
    root->addView(btnCancel);

    if (stageLabel)
        startPolling();

    return root;
}

void TorrentActivity::onContentAvailable() {
    this->registerAction("Cancelar", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        this->cancelAndPop();
        return true;
    });
    if (btnCancel) {
        brls::Application::giveFocus(btnCancel);
    }
}

void TorrentActivity::willDisappear(bool resetState) {
    *alive = false;
    stopPolling();
}

void TorrentActivity::startPolling() {
    struct PollTask {
        TorrentActivity* act;
        std::shared_ptr<bool> alive;
    };
    PollTask* task = new PollTask{this, alive};

    auto threadFunc = [](void* arg) -> void* {
        PollTask* t = static_cast<PollTask*>(arg);
        TorrentActivity* act = t->act;
        std::shared_ptr<bool> alive = t->alive;
        delete t;

        while (*alive && !act->launchedPlayer.load()) {
            auto& player = stremio_torrent::TorrentPlayer::getInstance();
            stremio_torrent::TorrentStatus status = player.status();

            std::string stage = status.stage;
            float progress = 0.0f;
            uint64_t downloaded = status.downloaded;
            uint64_t total = status.total;
            uint64_t speed = status.speed_bps;
            uint32_t peers = status.peers;
            uint32_t piecesDone = status.pieces_done;
            if (total > 0)
                progress = (float)((double)downloaded / (double)total);
            std::string err = status.error;

            bool readyToPlay = player.isActive() && total > 0 &&
                               piecesDone >= 1;

            brls::sync([act, alive, stage, progress, downloaded, total, speed,
                        peers, piecesDone, err, readyToPlay]() {
                if (!*alive)
                    return;
                if (act->stageLabel)
                    act->stageLabel->setText(stage);
                if (act->progressBar)
                    act->progressBar->setProgress(progress);
                if (act->statsLabel) {
                    char buf[128];
                    if (total > 0 && downloaded < total)
                        snprintf(buf, sizeof(buf), "%d%% · %s / %s · %u pares · %s/s",
                                 (int)(progress * 100.0f),
                                 formatBytes(downloaded).c_str(),
                                 formatBytes(total).c_str(), peers,
                                 formatBytes(speed).c_str());
                    else if (total > 0)
                        snprintf(buf, sizeof(buf), "%d%% · %s · %u pares",
                                 (int)(progress * 100.0f),
                                 formatBytes(total).c_str(), peers);
                    else
                        snprintf(buf, sizeof(buf), "%u piezas descargadas · %u pares",
                                 piecesDone, peers);
                    act->statsLabel->setText(buf);
                }
                if (act->errorLabel && !err.empty())
                    act->errorLabel->setText(err);

                if (readyToPlay && !act->launchedPlayer.load()) {
                    act->launchedPlayer.store(true);
                    brls::Application::popActivity();
                    brls::Application::pushActivity(
                        new PlayerActivity("torrent://stream",
                                           act->playerTitle, act->playerLoader,
                                           act->subtitlesType, act->subtitlesId),
                        brls::TransitionAnimation::FADE);
                }
            });
            usleep(250 * 1000);
        }
        return nullptr;
    };

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
    pthread_create(&pollThread, &attr, threadFunc, task);
    pthread_detach(pollThread);
    pthread_attr_destroy(&attr);
}

void TorrentActivity::stopPolling() {
    *alive = false;
}