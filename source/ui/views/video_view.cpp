#include "ui/views/video_view.hpp"
#include <borealis/views/dropdown.hpp>
#include <borealis/core/activity.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/core/time.hpp>
#include "../../core/http_client.hpp"
#include "../../core/task_queue.hpp"
#include "../../core/playback_settings.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <vector>

std::string VideoView::formatTime(int64_t totalSeconds) {
    if (totalSeconds < 0) totalSeconds = 0;
    int64_t hours = totalSeconds / 3600;
    int64_t minutes = (totalSeconds % 3600) / 60;
    int64_t seconds = totalSeconds % 60;
    
    char buf[64];
    if (hours > 0) {
        snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", (long long)hours, (long long)minutes, (long long)seconds);
    } else {
        snprintf(buf, sizeof(buf), "%02lld:%02lld", (long long)minutes, (long long)seconds);
    }
    return std::string(buf);
}

VideoView::VideoView() {
    mpvCore = &MPVCore::instance();
    
    this->setWidthPercentage(100);
    this->setHeightPercentage(100);
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    this->setAlignItems(brls::AlignItems::CENTER);
    
    // Top spacer
    spacerTop = new brls::Box();
    spacerTop->setGrow(1.0f);
    this->addView(spacerTop);

    // OSD Container
    osdBox = new brls::Box(brls::Axis::COLUMN);
    osdBox->setWidthPercentage(94);
    osdBox->setHeight(165);
    osdBox->setMarginBottom(25);
    osdBox->setPadding(15, 25, 15, 25);
    osdBox->setCornerRadius(14);
    osdBox->setBackgroundColor(nvgRGBA(18, 18, 28, 225));
    osdBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

    // Header Row: Title and Status
    brls::Box* headerRow = new brls::Box(brls::Axis::ROW);
    headerRow->setAlignItems(brls::AlignItems::CENTER);
    headerRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

    titleLabel = new brls::Label();
    titleLabel->setText("StremioNX Player");
    titleLabel->setFontSize(20);
    titleLabel->setTextColor(nvgRGB(255, 255, 255));
    headerRow->addView(titleLabel);

    statusLabel = new brls::Label();
    statusLabel->setText("▶ Reproduciendo");
    statusLabel->setFontSize(16);
    statusLabel->setTextColor(nvgRGB(120, 220, 255));
    headerRow->addView(statusLabel);

    osdBox->addView(headerRow);

    // Progress Bar Slider
    progressBar = new brls::Slider();
    progressBar->setWidthPercentage(100);
    progressBar->setHeight(20);
    progressBar->setHideHighlight(true);
    progressBar->setMarginBottom(12);
    progressBar->setProgress(0.0f);
    progressBar->getProgressEvent()->subscribe([this](float progress) {
        if (this->updatingProgress) return;
        this->seekTo(progress);
    });
    osdBox->addView(progressBar);

    // Bottom Row: Time, track selectors and hints
    brls::Box* bottomRow = new brls::Box(brls::Axis::ROW);
    bottomRow->setWidthPercentage(100);
    bottomRow->setAlignItems(brls::AlignItems::CENTER);
    bottomRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

    brls::Box* timeBox = new brls::Box(brls::Axis::ROW);
    timeBox->setAlignItems(brls::AlignItems::CENTER);

    timeLabel = new brls::Label();
    timeLabel->setText("00:00 / 00:00");
    timeLabel->setFontSize(16);
    timeLabel->setTextColor(nvgRGB(200, 200, 200));
    timeBox->addView(timeLabel);

    brls::Box* trackBox = new brls::Box(brls::Axis::ROW);
    trackBox->setAlignItems(brls::AlignItems::CENTER);

    audioButton = new brls::Button();
    audioButton->setHideHighlight(true);
    audioButton->setFontSize(13);
    audioButton->setText("Audio");
    audioButton->registerAction("Seleccionar Audio", brls::ControllerButton::BUTTON_A, [this](brls::View* view) {
        this->openAudioDropdown();
        return true;
    });
    trackBox->addView(audioButton);

    subButton = new brls::Button();
    subButton->setHideHighlight(true);
    subButton->setMarginLeft(14);
    subButton->setFontSize(13);
    subButton->setText("Subs");
    subButton->registerAction("Seleccionar Subtítulos", brls::ControllerButton::BUTTON_A, [this](brls::View* view) {
        this->openSubDropdown();
        return true;
    });
    trackBox->addView(subButton);

    bottomRow->addView(timeBox);
    bottomRow->addView(trackBox);

    hintsLabel = new brls::Label();
    hintsLabel->setText("[A] Pausar   [B] Salir   [X] OSD   [←/→] +-10s   [L/R] +-30s");
    hintsLabel->setFontSize(13);
    hintsLabel->setTextColor(nvgRGB(160, 160, 175));

    osdBox->addView(bottomRow);
    osdBox->addView(hintsLabel);

    this->addView(osdBox);
    
    // Register actions for controller buttons
    this->registerAction("Salir", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        brls::Logger::info("Player: BUTTON_B pressed, exiting player");
        mpvCore->stop();
        brls::Application::popActivity();
        return true;
    }, true);

    this->registerAction("Pausa/Play", brls::ControllerButton::BUTTON_A, [this](brls::View* view) {
        this->togglePlay();
        return true;
    }, true);

    this->registerAction("Toggle OSD", brls::ControllerButton::BUTTON_X, [this](brls::View* view) {
        this->toggleOSD();
        return true;
    }, true);

    this->registerAction("Toggle OSD", brls::ControllerButton::BUTTON_Y, [this](brls::View* view) {
        this->toggleOSD();
        return true;
    }, true);

    this->registerAction("Retroceder 10s", brls::ControllerButton::BUTTON_LEFT, [this](brls::View* view) {
        this->seekRelative(-10);
        return true;
    }, true);

    this->registerAction("Adelantar 10s", brls::ControllerButton::BUTTON_RIGHT, [this](brls::View* view) {
        this->seekRelative(10);
        return true;
    }, true);

    this->registerAction("Retroceder 30s", brls::ControllerButton::BUTTON_LB, [this](brls::View* view) {
        this->seekRelative(-30);
        return true;
    }, true);

    this->registerAction("Adelantar 30s", brls::ControllerButton::BUTTON_RB, [this](brls::View* view) {
        this->seekRelative(30);
        return true;
    }, true);

    this->registerAction("Subir Volumen", brls::ControllerButton::BUTTON_UP, [this](brls::View* view) {
        int64_t v = std::min((int64_t)100, mpvCore->volume + 5);
        mpvCore->setVolume(v);
        this->showOSD();
        return true;
    }, true);

    this->registerAction("Bajar Volumen", brls::ControllerButton::BUTTON_DOWN, [this](brls::View* view) {
        int64_t v = std::max((int64_t)0, mpvCore->volume - 5);
        mpvCore->setVolume(v);
        this->showOSD();
        return true;
    }, true);

    // Keep the OSD track labels and the subtitle auto-selection in sync with
    // mpv's track-list (fires when the file loads and when external subtitles
    // added via sub-add finish loading).
    mpvCore->getTrackListChangedEvent().subscribe([this]() {
        this->refreshTrackUI();
        this->maybeAutoSelectSubs();
    });

    showOSD();
}

VideoView::~VideoView() {
    mpvCore->stop();
}

void VideoView::setUrl(const std::string& url) {
    mpvCore->setUrl(url);
    this->showOSD();
}

void VideoView::setLoaderImage(const std::string& url) {
    if (loaderImage == nullptr) {
        loaderImage = new brls::Image();
        loaderImage->setScalingType(brls::ImageScalingType::FIT);
        loaderImage->setDimensions(400, 400);
        loaderImage->setVisibility(brls::Visibility::INVISIBLE);
    }
    TaskQueue::getInstance().push([this, url]() {
        std::vector<unsigned char> data;
        if (HttpClient::getInstance().getBinary(url, data) && !data.empty()) {
            brls::sync([this, data = std::move(data)]() {
                if (loaderImage != nullptr) {
                    loaderImage->setImageFromMem(data.data(), (int)data.size());
                    loaderImageReady = true;
                    loaderImage->setVisibility(brls::Visibility::VISIBLE);
                }
            });
        }
    });
}

void VideoView::setTitle(const std::string& title) {
    if (titleLabel) {
        titleLabel->setText(title);
    }
}

void VideoView::setSubtitles(const std::vector<SubtitleItem>& subs) {
    pendingSubtitles = subs;
    pendingSubtitlesLoaded = false;
    autoSelectDone = false;
    if (mpvCore->fileLoaded) {
        loadPendingSubtitles();
    }
}

void VideoView::loadPendingSubtitles() {
    if (pendingSubtitlesLoaded) return;
    pendingSubtitlesLoaded = true;
    for (const auto& s : pendingSubtitles) {
        mpvCore->addSubtitle(s.url, s.name, s.lang, s.encoding);
    }
}

void VideoView::maybeAutoSelectSubs() {
    if (autoSelectDone) return;
    if (!pendingSubtitlesLoaded || pendingSubtitles.empty()) return;
    if (!PlaybackSettings::getInstance().subsEnabled()) {
        autoSelectDone = true;
        return;
    }

    auto subTracks = mpvCore->getSubtitleTracks();
    if (subTracks.empty()) return;  // external subs not in track-list yet

    for (const auto& t : subTracks) {
        if (t.selected) {
            autoSelectDone = true;
            return;
        }
    }

    // Nothing selected: prefer a track whose language matches the preferred
    // code (e.g. "spa" inside "Spanish"), otherwise the first subtitle.
    std::string pref = PlaybackSettings::getInstance().subsLang();
    std::transform(pref.begin(), pref.end(), pref.begin(), ::tolower);
    int64_t fallback = -1, matched = -1;
    for (const auto& t : subTracks) {
        if (fallback < 0) fallback = t.id;
        std::string lang = t.lang;
        std::transform(lang.begin(), lang.end(), lang.begin(), ::tolower);
        if (matched < 0 && !pref.empty() && lang.find(pref) != std::string::npos)
            matched = t.id;
    }

    int64_t pick = matched > 0 ? matched : fallback;
    if (pick > 0) {
        currentSubId = pick;
        mpvCore->setSubTrack(pick);
    }
    autoSelectDone = true;
}

void VideoView::togglePlay() {
    if (isPaused) {
        mpvCore->resume();
        isPaused = false;
        if (statusLabel) statusLabel->setText("▶ Reproduciendo");
    } else {
        mpvCore->pause();
        isPaused = true;
        if (statusLabel) statusLabel->setText("⏸ Pausado");
    }
    showOSD();
}

void VideoView::toggleOSD() {
    if (osdVisible) {
        hideOSD();
    } else {
    mpvCore->getTrackListChangedEvent().subscribe([this]() {
        this->refreshTrackUI();
    });

    this->addGestureRecognizer(new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound* sound) {
        if (status.state == brls::GestureState::END)
            this->showOSD();
    }));

    showOSD();
}
}

void VideoView::showOSD() {
    osdVisible = true;
    if (osdBox) osdBox->setVisibility(brls::Visibility::VISIBLE);
    osdLastShowTime = std::time(nullptr);
}

void VideoView::hideOSD() {
    osdVisible = false;
    if (osdBox) osdBox->setVisibility(brls::Visibility::INVISIBLE);
    brls::Application::giveFocus(this);
}

void VideoView::seekTo(float progress) {
    mpvCore->seekPercent(progress * 100.0f);
    showOSD();
}

void VideoView::seekRelative(double seconds) {
    mpvCore->seekRelative(seconds);
    showOSD();
}

void VideoView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    mpvCore->draw(brls::Rect(x, y, width, height), this->getAlpha());

    // Loading overlay: while mpv hasn't finished loading the file the window
    // is black, so pulse the Cinemeta logo in the center (opacity 20%->100%).
    if (loaderImage != nullptr && loaderImageReady && !mpvCore->fileLoaded) {
        double t = (double)brls::getCPUTimeUsec() / 1000000.0;
        float pulse = 0.2f + 0.8f * (0.5f + 0.5f * (float)std::sin(t * 3.0));
        loaderImage->setAlpha(pulse);

        float imgW = loaderImage->getWidth();
        float imgH = loaderImage->getHeight();
        float cx = x + (width - imgW) / 2.0f;
        float cy = y + (height - imgH) / 2.0f;
        loaderImage->draw(vg, cx, cy, imgW, imgH, style, ctx);
    }

    // External subtitles need a loaded file to attach to; sub-add once mpv
    // reports the file ready.
    if (!pendingSubtitlesLoaded && mpvCore->fileLoaded && !pendingSubtitles.empty()) {
        loadPendingSubtitles();
    }

    // Auto-hide OSD after 5 seconds of inactivity (only when playing)
    if (osdVisible && !isPaused && (std::time(nullptr) - osdLastShowTime > 5)) {
        hideOSD();
    }

    // Update progress bar and time labels
    if (mpvCore->fileLoaded && !mpvCore->coreIdle && mpvCore->duration > 0) {
        float progress = (float)mpvCore->playback_time / (float)mpvCore->duration;
        if (progress > 1.0f) progress = 1.0f;
        if (progress < 0.0f) progress = 0.0f;
        this->updatingProgress = true;
        progressBar->setProgress(progress);
        this->updatingProgress = false;
        
        std::string currentTimeStr = formatTime((int64_t)mpvCore->playback_time);
        std::string totalTimeStr = formatTime(mpvCore->duration);
        timeLabel->setText(currentTimeStr + " / " + totalTimeStr);
    }

    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

std::string VideoView::trackLabel(const std::vector<MPVCore::Track>& tracks, int64_t currentId, const std::string& noneText) {
    for (const auto& t : tracks) {
        if (t.id == currentId) {
            std::string name = !t.title.empty() ? t.title : t.lang;
            if (name.empty()) name = std::to_string(t.id);
            return name;
        }
    }
    return noneText;
}

void VideoView::refreshTrackUI() {
    auto audioTracks = mpvCore->getAudioTracks();
    auto subTracks   = mpvCore->getSubtitleTracks();

    if (!audioTracks.empty()) {
        for (const auto& t : audioTracks)
            if (t.selected)
                currentAudioId = t.id;
        audioButton->setText("Audio: " + trackLabel(audioTracks, currentAudioId, "Auto"));
    } else {
        currentAudioId = -1;
        audioButton->setText("Audio");
    }

    if (!subTracks.empty()) {
        bool hasSub = false;
        for (const auto& t : subTracks) {
            if (t.selected) {
                currentSubId = t.id;
                hasSub = true;
                break;
            }
        }
        if (!hasSub) currentSubId = 0;
        subButton->setText("Subs: " + trackLabel(subTracks, currentSubId, "Off"));
    } else {
        currentSubId = -1;
        subButton->setText("Subtítulos");
    }
}

void VideoView::openAudioDropdown() {
    auto audioTracks = mpvCore->getAudioTracks();
    if (audioTracks.empty()) return;

    std::vector<std::string> values;
    std::vector<int64_t> ids;
    int selected = 0;

    for (const auto& t : audioTracks) {
        std::string name = !t.title.empty() ? t.title : t.lang;
        if (name.empty()) name = "Pista " + std::to_string(t.id);
        values.push_back(name);
        ids.push_back(t.id);
        if (t.selected) selected = (int)ids.size() - 1;
    }

    auto* dropdown = new brls::Dropdown("Audio", values, [this, ids](int index) {
        if (index >= 0 && index < (int)ids.size()) {
            this->currentAudioId = ids[index];
            mpvCore->setAudioTrack(this->currentAudioId);
            this->showOSD();
        }
    }, selected);

    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void VideoView::openSubDropdown() {
    auto subTracks = mpvCore->getSubtitleTracks();

    std::vector<std::string> values;
    std::vector<int64_t> ids;
    int selected = 0;

    values.push_back("Off");
    ids.push_back(0);

    for (const auto& t : subTracks) {
        std::string name = !t.title.empty() ? t.title : t.lang;
        if (name.empty()) name = "Pista " + std::to_string(t.id);
        values.push_back(name);
        ids.push_back(t.id);
        if (t.selected) selected = (int)ids.size() - 1;
    }

    auto* dropdown = new brls::Dropdown("Subtítulos", values, [this, ids](int index) {
        if (index >= 0 && index < (int)ids.size()) {
            this->autoSelectDone = true;
            this->currentSubId = ids[index];
            mpvCore->setSubTrack(this->currentSubId);
            this->showOSD();
        }
    }, selected);

    brls::Application::pushActivity(new brls::Activity(dropdown));
}

