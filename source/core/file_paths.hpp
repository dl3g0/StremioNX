#pragma once
#include <string>

#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#endif

// All StremioNX runtime files live under sdmc:/switch/stremio/.
namespace FilePaths {

constexpr const char* kDataDir = "sdmc:/switch/stremio";
constexpr const char* kLogFile = "sdmc:/switch/stremio/stremionx_log.txt";
constexpr const char* kAddonsFile = "sdmc:/switch/stremio/addons.json";
constexpr const char* kCatalogPrefsFile = "sdmc:/switch/stremio/catalog_prefs.json";
constexpr const char* kPlaybackSettingsFile = "sdmc:/switch/stremio/playback_settings.json";
constexpr const char* kAuthFile = "sdmc:/switch/stremio/auth.json";
constexpr const char* kFontFile = "sdmc:/switch/stremio/subfont.ttf";
constexpr const char* kTorrentsDir = "sdmc:/switch/stremio/torrents";
constexpr const char* kTorrentDownloadDir = "sdmc:/switch/stremio/torrents/download";
constexpr const char* kTorrentLogFile = "sdmc:/switch/stremio/torrent.log";

inline std::string dataDir() { return kDataDir; }
inline std::string logFile() { return kLogFile; }
inline std::string addonsFile() { return kAddonsFile; }
inline std::string catalogPrefsFile() { return kCatalogPrefsFile; }
inline std::string playbackSettingsFile() { return kPlaybackSettingsFile; }
inline std::string authFile() { return kAuthFile; }
inline std::string fontFile() { return kFontFile; }
inline std::string torrentsDir() { return kTorrentsDir; }
inline std::string torrentDownloadDir() { return kTorrentDownloadDir; }
inline std::string torrentLogFile() { return kTorrentLogFile; }

// Creates the data directory (and any missing parents) if needed.
inline void ensureDataDir() {
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/stremio", 0777);
    mkdir("sdmc:/switch/stremio/torrents", 0777);
    mkdir("sdmc:/switch/stremio/torrents/download", 0777);
#endif
}

} // namespace FilePaths
