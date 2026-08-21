#pragma once
#include <string>
#include <vector>
#include <mutex>

struct LanguageOption {
    std::string label;
    std::string code;
};

class PlaybackSettings {
public:
    static PlaybackSettings& getInstance();

    bool subsEnabled() const;
    void setSubsEnabled(bool value);

    std::string subsLang() const;
    void setSubsLang(const std::string& code);

    std::string audioLang() const;
    void setAudioLang(const std::string& code);

    bool show4KSources() const;
    void setShow4KSources(bool value);

    const std::vector<LanguageOption>& getLanguageOptions() const { return language_options; }

    void load();
    void save();

private:
    PlaybackSettings();
    ~PlaybackSettings();

    bool subs_enabled = false;
    std::string subs_lang = "spa";
    std::string audio_lang = "spa";
    bool show_4k = false;

    std::vector<LanguageOption> language_options;

    mutable std::mutex mutex;
};
