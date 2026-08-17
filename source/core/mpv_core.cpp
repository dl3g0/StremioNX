#include "core/mpv_core.hpp"
#include "core/playback_settings.hpp"
#include "core/file_paths.hpp"
#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#ifndef NANOVG_GL3
#define NANOVG_GL3
#endif
#include <borealis/extern/nanovg/nanovg_gl.h>
#include <cstdio>
#include <algorithm>
#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#endif

static inline void check_error(int status) {
    if (status < 0) {
        brls::Logger::error("MPV ERROR ====> {}", mpv_error_string(status));
    }
}

static void *get_proc_address(void *unused, const char *name) {
    glfwGetCurrentContext();
    return (void *)glfwGetProcAddress(name);
}

void MPVCore::on_update(void *self) {
    MPVCore *core = static_cast<MPVCore *>(self);
    if (!core || core->shuttingDown)
        return;
    brls::sync([core]() {
        static int frameCount = 0;
        uint64_t flags        = mpv_render_context_update(core->getContext());
        if (flags & MPV_RENDER_UPDATE_FRAME) {
            frameCount++;
            if (frameCount == 1 || frameCount % 300 == 0) {
                brls::Logger::info("DEBUG: update delivered frame #{}", frameCount);
            }
        }
    });
}

void MPVCore::on_wakeup(void *self) {
    MPVCore *core = static_cast<MPVCore *>(self);
    if (!core || core->shuttingDown)
        return;
    brls::sync([core]() { core->eventMainLoop(); });
}

MPVCore::MPVCore() {
    this->init();
}

MPVCore::~MPVCore() {
    this->clean();
}

void MPVCore::init() {
#ifdef __SWITCH__
    // libass on Switch has no font provider (no fontconfig/directwrite).
    // mpv only uses a font FILE if it finds "subfont.ttf" in its config dir
    // (MPV_HOME) and passes it to libass as path_default, which is used
    // directly regardless of the requested family name.
    {
        PlFontData font;
        if (R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_Standard))) {
            FilePaths::ensureDataDir();
            FILE *f = fopen(FilePaths::kFontFile, "wb");
            if (f) {
                fwrite(font.address, 1, font.size, f);
                fclose(f);
                setenv("MPV_HOME", FilePaths::kDataDir, 1);
                brls::Logger::info("DEBUG: wrote Switch font ({} bytes) to {}, MPV_HOME set", font.size, FilePaths::kFontFile);
                struct stat st;
                brls::Logger::info("DEBUG: stat {} = {}", FilePaths::kFontFile, stat(FilePaths::kFontFile, &st));
            } else {
                brls::Logger::error("DEBUG: could not write {}", FilePaths::kFontFile);
            }
        } else {
            brls::Logger::error("DEBUG: could not get Switch standard font");
        }
    }
#endif

    this->mpv = mpv_create();
    brls::Logger::info("DEBUG: init step mpv_create done");
    if (!mpv) {
        brls::fatal("Error Create mpv Handle");
    }

    mpv_set_option_string(mpv, "vo", "libmpv");
    mpv_set_option_string(mpv, "idle", "yes");
    mpv_set_option_string(mpv, "keep-open", "yes");
    mpv_set_option_string(mpv, "ytdl", "no");
    mpv_set_option_string(mpv, "audio-channels", "stereo");
    mpv_set_option_string(mpv, "osd-level", "1");
    mpv_set_option_string(mpv, "video-timing-offset", "0");
    mpv_set_option_string(mpv, "hr-seek", "yes");
    mpv_set_option_string(mpv, "loop-file", "no");
    mpv_set_option_string(mpv, "demuxer-lavf-analyzeduration", "0.4");
    mpv_set_option_string(mpv, "demuxer-lavf-probescore", "24");
    mpv_set_option_string(mpv, "cache", "no");

    // Apply playback preferences (subtitle/audio defaults).
    auto& playback = PlaybackSettings::getInstance();
    mpv_set_option_string(mpv, "sub-visibility", playback.subsEnabled() ? "yes" : "no");
    mpv_set_option_string(mpv, "sub-ass", "yes");
    if (playback.subsEnabled()) {
        // Auto-select subtitles at load time. Switching tracks mid-playback
        // forces mpv to seek to read the sub stream, which can hang on unstable
        // streams (HTTP 429); selecting via slang at start avoids that.
        mpv_set_option_string(mpv, "slang", playback.subsLang().c_str());
    } else {
        mpv_set_option_string(mpv, "slang", "");
    }
    // Default audio track preference.
    mpv_set_option_string(mpv, "alang", playback.audioLang().c_str());
#if defined(__SWITCH__)
    {
        // The builtin "libmpv" profile sets config=no, which makes
        // mp_init_paths() set configdir to "" (so subfont.ttf is never found).
        // Re-enable config loading so config-dir takes effect.
        int cfg_ret = mpv_set_option_string(mpv, "config", "yes");
        brls::Logger::info("DEBUG: set config=yes option ret={}", cfg_ret);
        cfg_ret = mpv_set_option_string(mpv, "config-dir", FilePaths::kDataDir);
        brls::Logger::info("DEBUG: set config-dir option ret={}", cfg_ret);
    }
#endif
#if defined(__SWITCH__)
    mpv_set_option_string(mpv, "hwdec", "no");
    mpv_set_option_string(mpv, "vd-lavc-dr", "no");
    mpv_set_option_string(mpv, "vd-lavc-threads", "4");
    mpv_set_option_string(mpv, "video-sync", "audio");
    mpv_set_option_string(mpv, "opengl-glfinish", "no");
#else
    mpv_set_option_string(mpv, "hwdec", "auto");
#endif

    if (mpv_initialize(mpv) < 0) {
        mpv_terminate_destroy(mpv);
        brls::fatal("Could not initialize mpv context");
    }
    brls::Logger::info("DEBUG: init step mpv_initialize done");

    mpv_request_log_messages(mpv, "debug");

    check_error(mpv_observe_property(mpv, 1, "duration", MPV_FORMAT_DOUBLE));
    check_error(mpv_observe_property(mpv, 2, "playback-time", MPV_FORMAT_DOUBLE));
    check_error(mpv_observe_property(mpv, 6, "track-list", MPV_FORMAT_NODE));
    check_error(mpv_observe_property(mpv, 7, "aid", MPV_FORMAT_INT64));
    check_error(mpv_observe_property(mpv, 8, "sid", MPV_FORMAT_INT64));
    check_error(mpv_observe_property(mpv, 9, "core-idle", MPV_FORMAT_FLAG));
    check_error(mpv_observe_property(mpv, 12, "sub-text", MPV_FORMAT_STRING));

    int advanced_control{1};
    mpv_opengl_init_params gl_init_params{get_proc_address, nullptr};
    mpv_render_param params[]{{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                              {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
                              {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced_control},
                              {MPV_RENDER_PARAM_INVALID, nullptr}};

    if (mpv_render_context_create(&mpv_context, mpv, params) < 0) {
        mpv_terminate_destroy(mpv);
        brls::fatal("failed to initialize mpv GL context");
    }
    brls::Logger::info("DEBUG: init step render_context_create done");

    mpv_set_wakeup_callback(mpv, on_wakeup, this);
    mpv_render_context_set_update_callback(mpv_context, on_update, this);

    // Clean up mpv BEFORE borealis tears down the GL platform / threads.
    // Otherwise mpv's render thread keeps calling brls::sync / GL after the
    // context is gone, crashing on app exit.
    brls::Application::getExitEvent()->subscribe([this]() {
        brls::Logger::info("DEBUG: MPVCore cleaning up on app exit");
        this->clean();
    });
    brls::Logger::info("DEBUG: init step exitEvent subscribed");

    this->initializeVideo();
}

void MPVCore::clean() {
    if (this->shuttingDown)
        return;
    this->shuttingDown = true;

    if (this->mpv_context) {
        mpv_render_context_set_update_callback(this->mpv_context, nullptr, nullptr);
    }

    this->uninitializeVideo();
    if (this->mpv_context) {
        mpv_render_context_free(this->mpv_context);
        this->mpv_context = nullptr;
    }
    if (this->mpv) {
        mpv_terminate_destroy(this->mpv);
        this->mpv = nullptr;
    }
}

void MPVCore::initializeVideo() {
    brls::Logger::info("DEBUG: initializeVideo enter");
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &default_framebuffer);
    brls::Logger::info("DEBUG: default_framebuffer = {}", (int)default_framebuffer);
}

void MPVCore::uninitializeVideo() {
    if (media_framebuffer != 0) {
        glDeleteFramebuffers(1, &media_framebuffer);
        media_framebuffer = 0;
    }
    if (media_texture != 0) {
        glDeleteTextures(1, &media_texture);
        media_texture = 0;
    }
    if (nvg_image != 0) {
        nvgDeleteImage(brls::Application::getNVGContext(), nvg_image);
        nvg_image = 0;
    }
}

void MPVCore::setFrameSize(brls::Rect r) {
    rect = r;
    if (std::isnan(rect.getWidth()) || std::isnan(rect.getHeight())) return;

    int w = (int)brls::Application::windowWidth;
    int h = (int)brls::Application::windowHeight;
    if (w == 0 || h == 0) return;

    if (media_texture == 0 || fbo_w != w || fbo_h != h) {
        fbo_w = w;
        fbo_h = h;

        if (media_texture == 0) {
            glGenTextures(1, &media_texture);
            glBindTexture(GL_TEXTURE_2D, media_texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);

            glGenFramebuffers(1, &media_framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, media_framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, media_texture, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                brls::Logger::error("DEBUG: mpv FBO incomplete");
            }
            glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);

            auto *vg = brls::Application::getNVGContext();
            if (nvg_image != 0) nvgDeleteImage(vg, nvg_image);
            nvg_image = nvglCreateImageFromHandleGL3(vg, media_texture, w, h, NVG_IMAGE_NODELETE | NVG_IMAGE_FLIPY);
            brls::Logger::info("DEBUG: mpv FBO created {}x{} nvg_image={}", w, h, nvg_image);
        } else {
            glBindTexture(GL_TEXTURE_2D, media_texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        this->mpv_fbo.w = w;
        this->mpv_fbo.h = h;
        this->mpv_fbo.fbo = (int)media_framebuffer;
    }
}

bool MPVCore::isValid() { return mpv_context != nullptr; }

void MPVCore::draw(brls::Rect area, float alpha) {
    if (mpv_context == nullptr) return;
    if (!(this->rect == area)) setFrameSize(area);
    if (media_framebuffer == 0 || media_texture == 0) return;

    glBindFramebuffer(GL_FRAMEBUFFER, media_framebuffer);
    glViewport(0, 0, fbo_w, fbo_h);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    int ret = mpv_render_context_render(this->mpv_context, mpv_params);
    if (ret != 0) {
        brls::Logger::error("DEBUG: mpv_render_context_render failed: {}", mpv_error_string(ret));
    }
    mpv_render_context_report_swap(this->mpv_context);
    glBindFramebuffer(GL_FRAMEBUFFER, default_framebuffer);
    glViewport(0, 0, (GLsizei)brls::Application::windowWidth, (GLsizei)brls::Application::windowHeight);

    if (nvg_image != 0) {
        auto *vg = brls::Application::getNVGContext();
        NVGpaint img = nvgImagePattern(vg, rect.getMinX(), rect.getMinY(), rect.getWidth(), rect.getHeight(), 0, nvg_image, alpha);
        nvgBeginPath(vg);
        nvgRect(vg, rect.getMinX(), rect.getMinY(), rect.getWidth(), rect.getHeight());
        nvgFillPaint(vg, img);
        nvgFill(vg);
    }
}

mpv_render_context *MPVCore::getContext() { return this->mpv_context; }
mpv_handle *MPVCore::getHandle() { return this->mpv; }

void MPVCore::eventMainLoop() {
    if (!this->mpv) return;
    while (true) {
        auto event = mpv_wait_event(this->mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) return;
        
        if (event->event_id != MPV_EVENT_PROPERTY_CHANGE)
            brls::Logger::info("DEBUG: MPV event: {}", (int)event->event_id);
        
        // Fire event to subscribers
        this->mpvEvent.fire((int)event->event_id);
        
        switch (event->event_id) {
            case MPV_EVENT_LOG_MESSAGE: {
                auto log = (mpv_event_log_message *)event->data;
                std::string txt = log->text;
                std::string lower = txt;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                bool interesting = lower.find("font") != std::string::npos ||
                                   lower.find("sub") != std::string::npos ||
                                   lower.find("ass") != std::string::npos ||
                                   lower.find("libass") != std::string::npos ||
                                   lower.find("subtitle") != std::string::npos ||
                                   lower.find("config") != std::string::npos ||
                                   lower.find("path") != std::string::npos;
                if (log->log_level <= MPV_LOG_LEVEL_ERROR) {
                    brls::Logger::error("[mpv] {}: {}", log->prefix, log->text);
                } else if (log->log_level <= MPV_LOG_LEVEL_WARN) {
                    brls::Logger::warning("[mpv] {}: {}", log->prefix, log->text);
                } else if (interesting) {
                    brls::Logger::info("[mpv] {}: {}", log->prefix, log->text);
                }
                break;
            }
            case MPV_EVENT_START_FILE:
                brls::Logger::info("DEBUG: MPV_EVENT_START_FILE - mpv is ready to render");
                this->readyToRender = true;
                this->fileLoaded = false;
                this->coreIdle = true;
                this->duration = 0;
                this->playback_time = 0;
                break;
            case MPV_EVENT_FILE_LOADED:
                brls::Logger::info("DEBUG: MPV_EVENT_FILE_LOADED");
                this->fileLoaded = true;
                {
                    double d = 0, pt = 0;
                    mpv_get_property(this->mpv, "duration", MPV_FORMAT_DOUBLE, &d);
                    mpv_get_property(this->mpv, "playback-time", MPV_FORMAT_DOUBLE, &pt);
                    brls::Logger::info("DEBUG: after FILE_LOADED duration={:.3f} playback_time={:.3f}", d, pt);
                    char *subtext = nullptr;
                    if (mpv_get_property(this->mpv, "sub-text", MPV_FORMAT_STRING, &subtext) == 0) {
                        brls::Logger::info("DEBUG: sub-text at FILE_LOADED = '{}'", subtext ? subtext : "(null)");
                        mpv_free(subtext);
                    } else {
                        brls::Logger::info("DEBUG: sub-text at FILE_LOADED get failed");
                    }
                }
                break;
            case MPV_EVENT_END_FILE:
                brls::Logger::info("DEBUG: MPV_EVENT_END_FILE");
                this->readyToRender = false;
                this->fileLoaded = false;
                this->coreIdle = true;
                this->duration = 0;
                this->playback_time = 0;
                break;
            case MPV_EVENT_PROPERTY_CHANGE: {
                auto prop = (mpv_event_property *)event->data;
                if (std::string(prop->name) == "duration" && prop->format == MPV_FORMAT_DOUBLE) {
                    this->duration = *(double *)prop->data;
                    if (this->duration < 0)
                        this->duration = 0;
                } else if (std::string(prop->name) == "playback-time" && prop->format == MPV_FORMAT_DOUBLE) {
                    double pt = *(double *)prop->data;
                    if (pt < 0)
                        pt = 0;
                    if (this->playback_time == 0 && pt > 0)
                        brls::Logger::info("DEBUG: playback_time first positive = {:.3f} (duration={:.3f})", pt, this->duration);
                    this->playback_time = pt;
                } else if (std::string(prop->name) == "core-idle" && prop->format == MPV_FORMAT_FLAG) {
                    this->coreIdle = *(int *)prop->data != 0;
                } else if (std::string(prop->name) == "track-list") {
                    this->refreshTracks();
                } else if (std::string(prop->name) == "aid" && prop->format == MPV_FORMAT_INT64) {
                    int64_t aid = *(int64_t *)prop->data;
                    for (auto &t : this->tracks) {
                        if (t.type == "audio")
                            t.selected = (t.id == aid);
                    }
                    this->trackListChangedEvent.fire();
                } else if (std::string(prop->name) == "sid" && prop->format == MPV_FORMAT_INT64) {
                    int64_t sid = *(int64_t *)prop->data;
                    for (auto &t : this->tracks) {
                        if (t.type == "sub")
                            t.selected = (t.id == sid);
                    }
                    this->trackListChangedEvent.fire();
                } else if (std::string(prop->name) == "sub-text" && prop->format == MPV_FORMAT_STRING && prop->data) {
                    const char *st = *(const char **)prop->data;
                    if (st && *st) {
                        brls::Logger::info("DEBUG: sub-text ACTIVE: {}", st);
                    }
                }
                break;
            }
            default: break;
        }
    }
}

void MPVCore::setUrl(const std::string &url) {
    if (!this->mpv) return;
    brls::Logger::info("DEBUG: MPVCore::setUrl started");
    const char *args[] = {"loadfile", url.c_str(), NULL};
    brls::Logger::info("DEBUG: MPVCore::setUrl mpv_command_async calling");
    mpv_command_async(this->mpv, 0, args);
    brls::Logger::info("DEBUG: MPVCore::setUrl mpv_command_async finished");
}

void MPVCore::resume() {
    if (!this->mpv) return;
    int val = 0;
    mpv_set_property(this->mpv, "pause", MPV_FORMAT_FLAG, &val);
}

void MPVCore::pause() {
    if (!this->mpv) return;
    int val = 1;
    mpv_set_property(this->mpv, "pause", MPV_FORMAT_FLAG, &val);
}

void MPVCore::stop() {
    this->readyToRender = false;
    if (!this->mpv) return;
    const char *args[] = {"stop", NULL};
    mpv_command_async(this->mpv, 0, args);
}

void MPVCore::seek(double p) {
    if (!this->mpv) return;
    std::string pos = std::to_string(p);
    const char *args[] = {"seek", pos.c_str(), "absolute", NULL};
    mpv_command_async(this->mpv, 0, args);
}

void MPVCore::seekPercent(double value) {
    if (!this->mpv) return;
    std::string pos = std::to_string(value);
    const char *args[] = {"seek", pos.c_str(), "absolute-percent", NULL};
    mpv_command_async(this->mpv, 0, args);
}

void MPVCore::seekRelative(double sec) {
    if (!this->mpv) return;
    std::string pos = std::to_string(sec);
    const char *args[] = {"seek", pos.c_str(), "relative", NULL};
    mpv_command_async(this->mpv, 0, args);
}

void MPVCore::setVolume(int64_t value) {
    if (!this->mpv) return;
    this->volume = value;
    mpv_set_property(this->mpv, "volume", MPV_FORMAT_INT64, &this->volume);
}

bool MPVCore::isStopped() const { return !readyToRender; }
bool MPVCore::isPlaying() const { return readyToRender; }
bool MPVCore::isPaused() const { return false; }

void MPVCore::refreshTracks() {
    this->tracks.clear();

    mpv_node node;
    if (mpv_get_property(this->mpv, "track-list", MPV_FORMAT_NODE, &node) < 0)
        return;

    if (node.format != MPV_FORMAT_NODE_ARRAY) {
        mpv_free_node_contents(&node);
        return;
    }

    brls::Logger::info("DEBUG: track-list total entries = {}", node.u.list->num);

    for (int i = 0; i < node.u.list->num; i++) {
        mpv_node *item = &node.u.list->values[i];
        if (item->format != MPV_FORMAT_NODE_MAP)
            continue;

        Track track;
        bool isAudio = false;
        bool isSub   = false;

        for (int j = 0; j < item->u.list->num; j++) {
            const char *key   = item->u.list->keys[j];
            mpv_node *value   = &item->u.list->values[j];
            std::string k(key ? key : "");

            if (k == "id" && value->format == MPV_FORMAT_INT64)
                track.id = value->u.int64;
            else if (k == "type" && value->format == MPV_FORMAT_STRING)
                track.type = value->u.string ? value->u.string : "";
            else if (k == "lang" && value->format == MPV_FORMAT_STRING)
                track.lang = value->u.string ? value->u.string : "";
            else if (k == "title" && value->format == MPV_FORMAT_STRING)
                track.title = value->u.string ? value->u.string : "";
            else if (k == "selected" && value->format == MPV_FORMAT_FLAG)
                track.selected = value->u.flag != 0;
        }

        isAudio = track.type == "audio";
        isSub   = track.type == "sub";

        if (isAudio || isSub) {
            this->tracks.push_back(track);
            brls::Logger::info("DEBUG: track id={} type='{}' lang='{}' title='{}' selected={}", track.id, track.type, track.lang, track.title, track.selected ? 1 : 0);
        }
    }

    mpv_free_node_contents(&node);
    this->trackListChangedEvent.fire();
}

std::vector<MPVCore::Track> MPVCore::getAudioTracks() const {
    std::vector<Track> result;
    for (const auto &t : this->tracks)
        if (t.type == "audio")
            result.push_back(t);
    return result;
}

std::vector<MPVCore::Track> MPVCore::getSubtitleTracks() const {
    std::vector<Track> result;
    for (const auto &t : this->tracks)
        if (t.type == "sub")
            result.push_back(t);
    return result;
}

void MPVCore::setAudioTrack(int64_t id) {
    mpv_set_property(this->mpv, "aid", MPV_FORMAT_INT64, &id);
}

void MPVCore::setSubTrack(int64_t id) {
    brls::Logger::info("DEBUG: setSubTrack id={}", id);
    int ret = mpv_set_property(this->mpv, "sid", MPV_FORMAT_INT64, &id);
    brls::Logger::info("DEBUG: setSubTrack sid ret={}", ret);
    int visible = (id > 0) ? 1 : 0;
    ret = mpv_set_property(this->mpv, "sub-visibility", MPV_FORMAT_FLAG, &visible);
    brls::Logger::info("DEBUG: setSubTrack sub-visibility set to {} ret={}", visible, ret);
    if (mpv_get_property(this->mpv, "sub-visibility", MPV_FORMAT_FLAG, &visible) == 0) {
        brls::Logger::info("DEBUG: sub-visibility now = {}", visible);
    }
}

