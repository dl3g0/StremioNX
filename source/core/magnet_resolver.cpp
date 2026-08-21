#include "magnet_resolver.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace stremio_torrent {

bool MagnetResolver::isMagnet(const std::string& uri) {
    return uri.rfind("magnet:?", 0) == 0;
}

bool MagnetResolver::isTorrentUrl(const std::string& uri) {
    if (uri.rfind("http://", 0) != 0 && uri.rfind("https://", 0) != 0)
        return false;
    size_t query = uri.find('?');
    size_t end = query == std::string::npos ? uri.size() : query;
    size_t dot = uri.rfind('.', end);
    if (dot == std::string::npos || end - dot < 9)
        return false;
    std::string ext = uri.substr(dot + 1, end - dot - 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return ext == "torrent";
}

namespace {
const char* const kTrackers[] = {
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://tracker.openbittorrent.com:6969/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://open.stealth.si:80/announce",
    "udp://open.demonii.com:1337/announce",
    "udp://tracker.publictracker.xyz:6969/announce",
    "udp://tracker.qu.ax:6969/announce",
    "udp://tracker.theoks.net:6969/announce",
    "udp://tracker2.dler.org:80/announce",
    "udp://tracker.0x7c0.com:6969/announce",
    "udp://tracker-udp.gbitt.info:80/announce",
    "udp://zer0day.ch:1337/announce",
};
constexpr size_t kTrackerCount =
    sizeof(kTrackers) / sizeof(kTrackers[0]);

std::string extractTracker(const std::string& magnet, size_t pos) {
    size_t end = magnet.find('&', pos);
    if (end == std::string::npos)
        end = magnet.size();
    std::string tr = magnet.substr(pos + 4, end - (pos + 4));
    return tr;
}

bool hasTracker(const std::string& magnet, const std::string& tracker) {
    size_t pos = 0;
    while ((pos = magnet.find("&tr=", pos)) != std::string::npos) {
        if (extractTracker(magnet, pos) == tracker)
            return true;
        pos += 4;
    }
    return false;
}
} // namespace

std::string MagnetResolver::appendTrackers(const std::string& magnet, const std::vector<std::string>& extraTrackers) {
    std::string out = magnet;
    for (const auto& raw : extraTrackers) {
        std::string tr = raw;
        if (tr.rfind("tracker:", 0) == 0) tr = tr.substr(8);
        if (tr.rfind("udp://", 0) != 0 && tr.rfind("http://", 0) != 0 && tr.rfind("https://", 0) != 0) continue;
        if (!hasTracker(out, tr)) {
            out += "&tr=";
            out += tr;
        }
    }
    for (size_t i = 0; i < kTrackerCount; i++) {
        if (!hasTracker(out, kTrackers[i])) {
            out += "&tr=";
            out += kTrackers[i];
        }
    }
    return out;
}

std::string MagnetResolver::buildMagnet(const std::string& infoHash, const std::vector<std::string>& extraTrackers) {
    return appendTrackers("magnet:?xt=urn:btih:" + infoHash, extraTrackers);
}

}