#include "playback_settings.hpp"
#include "file_paths.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>

using json = nlohmann::json;

PlaybackSettings& PlaybackSettings::getInstance() {
    static PlaybackSettings instance;
    return instance;
}

PlaybackSettings::PlaybackSettings() {
    language_options = {
        {"Español (Latino)", "spa"},
        {"Español (España)", "es"},
        {"Inglés", "eng"},
        {"Francés", "fra"},
        {"Alemán", "deu"},
        {"Portugués", "por"},
        {"Italiano", "ita"},
        {"Japonés", "jpn"},
        {"Coreano", "kor"},
        {"Chino", "zho"},
        {"Ruso", "rus"},
        {"Árabe", "ara"},
        {"Hindi", "hin"},
        {"Polaco", "pol"},
        {"Turco", "tur"},
        {"Holandés", "nld"},
        {"Sueco", "swe"},
        {"Danés", "dan"},
    };
    load();
}

PlaybackSettings::~PlaybackSettings() {
    save();
}

bool PlaybackSettings::subsEnabled() const {
    std::lock_guard<std::mutex> lock(mutex);
    return subs_enabled;
}

void PlaybackSettings::setSubsEnabled(bool value) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        subs_enabled = value;
    }
    save();
}

std::string PlaybackSettings::subsLang() const {
    std::lock_guard<std::mutex> lock(mutex);
    return subs_lang;
}

void PlaybackSettings::setSubsLang(const std::string& code) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        subs_lang = code;
    }
    save();
}

std::string PlaybackSettings::audioLang() const {
    std::lock_guard<std::mutex> lock(mutex);
    return audio_lang;
}

void PlaybackSettings::setAudioLang(const std::string& code) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        audio_lang = code;
    }
    save();
}

bool PlaybackSettings::show4KSources() const {
    std::lock_guard<std::mutex> lock(mutex);
    return show_4k;
}

void PlaybackSettings::setShow4KSources(bool value) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        show_4k = value;
    }
    save();
}

void PlaybackSettings::load() {
    std::lock_guard<std::mutex> lock(mutex);
    FilePaths::ensureDataDir();
    std::ifstream file(FilePaths::kPlaybackSettingsFile);
    if (file.is_open()) {
        try {
            json j;
            file >> j;
            if (j.contains("subs_enabled") && j["subs_enabled"].is_boolean())
                subs_enabled = j["subs_enabled"].get<bool>();
            if (j.contains("subs_lang") && j["subs_lang"].is_string())
                subs_lang = j["subs_lang"].get<std::string>();
            if (j.contains("audio_lang") && j["audio_lang"].is_string())
                audio_lang = j["audio_lang"].get<std::string>();
            if (j.contains("show_4k") && j["show_4k"].is_boolean())
                show_4k = j["show_4k"].get<bool>();
        } catch (...) {}
    }
}

void PlaybackSettings::save() {
    std::lock_guard<std::mutex> lock(mutex);
    std::ofstream file(FilePaths::kPlaybackSettingsFile);
    if (file.is_open()) {
        json j;
        j["subs_enabled"] = subs_enabled;
        j["subs_lang"] = subs_lang;
        j["audio_lang"] = audio_lang;
        j["show_4k"] = show_4k;
        file << j.dump(4);
    }
}
