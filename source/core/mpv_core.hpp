#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <unordered_map>
#include <functional>
#include <borealis/core/geometry.hpp>
#include <borealis/core/singleton.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/event.hpp>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

typedef struct torrentfs torrentfs;

class MPVCore : public brls::Singleton<MPVCore> {
public:
    struct Track {
        int64_t id = 0;
        std::string type;   // "audio", "sub", "video"
        std::string lang;
        std::string title;
        bool selected = false;
    };

    MPVCore();
    ~MPVCore();

    void init();
    void clean();

    void setUrl(const std::string &url);
    
    void resume();
    void pause();
    void stop();
    void stopSync();
    void seek(double p);
    void seekPercent(double value);
    void seekRelative(double sec);
    void setVolume(int64_t value);

    void refreshTracks();
    std::vector<Track> getAudioTracks() const;
    std::vector<Track> getSubtitleTracks() const;
    void setAudioTrack(int64_t id);
    void setSubTrack(int64_t id);
    // Loads an external subtitle file (URL or path) into the current file.
    // Uses "auto" flags so it joins the track-list without forcing selection.
    void addSubtitle(const std::string& url, const std::string& title,
                     const std::string& lang, const std::string& encoding = "");
    
    bool isStopped() const;
    bool isPlaying() const;
    bool isPaused() const;
    
    void draw(brls::Rect rect, float alpha = 1.0);
    mpv_render_context *getContext();
    mpv_handle *getHandle();

    void setFrameSize(brls::Rect rect);
    bool isValid();

    double duration       = 0;
    int64_t volume         = 100;
    double playback_time   = 0;
    bool readyToRender     = false;
    bool fileLoaded        = false;
    bool coreIdle          = true;
    bool shuttingDown      = false;

    brls::Event<int> getEvent() { return mpvEvent; }
    brls::Event<> getTrackListChangedEvent() { return trackListChangedEvent; }

private:
    mpv_handle *mpv                 = nullptr;
    mpv_render_context *mpv_context = nullptr;
    brls::Rect rect                 = {0, 0, 1920, 1080};

    torrentfs *torrentTfs_ = nullptr;

    GLint default_framebuffer = 0;
    GLuint media_framebuffer  = 0;
    GLuint media_texture      = 0;
    int nvg_image             = 0;
    int fbo_w                 = 0;
    int fbo_h                 = 0;
    mpv_opengl_fbo mpv_fbo{0, 1920, 1080};
    int flip_y{1};
    mpv_render_param mpv_params[3] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    brls::Event<int> mpvEvent;
    brls::Event<> trackListChangedEvent;

    std::vector<Track> tracks;
    std::atomic<bool> wakeupQueued{false};

    void eventMainLoop();
    void initializeVideo();
    void uninitializeVideo();
    
    static void on_update(void *self);
    static void on_wakeup(void *self);
};
