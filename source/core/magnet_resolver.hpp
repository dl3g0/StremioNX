#pragma once

#include <string>
#include <vector>

namespace stremio_torrent {

class MagnetResolver {
public:
    static bool isMagnet(const std::string& uri);
    static bool isTorrentUrl(const std::string& uri);
    static std::string buildMagnet(const std::string& infoHash, const std::vector<std::string>& extraTrackers = {});
    static std::string appendTrackers(const std::string& magnet, const std::vector<std::string>& extraTrackers = {});
};

}