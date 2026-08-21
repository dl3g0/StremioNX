#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif
#include "torrentfs.h"
#ifdef __cplusplus
}
#endif

namespace stremio_torrent {

enum class TorrentPhase { Idle, Resolving, Downloading, Ready, Error };

struct TorrentStatus {
    TorrentPhase phase = TorrentPhase::Idle;
    uint64_t downloaded = 0;
    uint64_t total = 0;
    uint64_t speed_bps = 0;
    uint32_t peers = 0;
    uint32_t pieces_done = 0;
    uint32_t pieces = 0;
    std::string stage;
    std::string error;
};

struct Session {
    std::string source;
    int fileIdx = -1;
    volatile bool cancel = false;
    std::atomic<bool> opening{false};
    pthread_t thread = 0;
    std::atomic<bool> hasThread{false};
    std::atomic<bool> joined{false};
};

/*
 * Orchestrates one torrent playback session: opens the torrentfs streaming
 * engine (magnet/.torrent -> tracker announce -> BEP 9 metadata -> piece
 * download into a bounded RAM window) and exposes byte-availability for mpv's
 * torrent:// stream. The engine opens on its own 2 MiB thread.
 */
class TorrentPlayer {
public:
    static TorrentPlayer& getInstance();

    /*
     * Start a session. `source` is a magnet: URI or a path to a .torrent
     * file. `fileIdx` >= 0 forces that torrent file; -1 picks the largest
     * file. Returns "" on success or a Spanish error string.
     */
    std::string start(const std::string& source, int fileIdx);

    /* Cancel the session (unblock any parked read) without waiting. */
    void cancel();

    /* Close the session, joining the engine thread. Safe to call anytime. */
    void close();

    /* cancel() + close(). */
    void stop();

    TorrentStatus status() const;

    bool isActive() const { return active_.load(); }
    bool isStopped() const { return currentSessionCancelled(); }

    /* The live torrentfs backing the current session, or nullptr. */
    torrentfs* getTorrentfs() const { return tfs_.load(); }

    /*
     * Chosen file info for the stream server. Valid while a session is active.
     * The torrentfs stream is a single virtual file; `path` is left empty.
     */
    bool streamFileInfo(std::string& path, uint64_t& fileStart,
                        uint64_t& fileSize) const;

    /* True when the stream's bytes [rel, rel+len) are downloaded. */
    bool rangeReady(uint64_t rel, uint64_t len) const;

    /*
     * Block until the stream range [rel, rel+len) is readable or the session
     * stops. Returns true when readable.
     */
    bool waitForRange(uint64_t rel, uint64_t len, uint64_t timeoutMs);

private:
    TorrentPlayer() = default;
    ~TorrentPlayer();
    TorrentPlayer(const TorrentPlayer&) = delete;
    TorrentPlayer& operator=(const TorrentPlayer&) = delete;

    void worker(std::shared_ptr<Session> session);
    static void* workerEntry(void* arg);

    std::shared_ptr<Session> currentSession() const {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        return session_;
    }
    bool currentSessionCancelled() const {
        std::shared_ptr<Session> s = currentSession();
        return s && s->cancel;
    }

    std::atomic<bool> active_{false};
    std::atomic<TorrentPhase> phase_{TorrentPhase::Idle};

    mutable std::mutex statusMutex_;
    std::string stage_;
    std::string error_;

    mutable std::mutex sessionMutex_;
    std::shared_ptr<Session> session_;

    std::atomic<torrentfs*> tfs_{nullptr};
    std::atomic<uint64_t> fileSize_{0};
    std::atomic<uint64_t> speedBps_{0};
    std::atomic<uint32_t> peers_{0};
    std::atomic<uint32_t> piecesDone_{0};
    std::atomic<uint32_t> pieces_{0};

    pthread_t thread_{0};
};

}