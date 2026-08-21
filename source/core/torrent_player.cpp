#include "torrent_player.hpp"
#include "file_paths.hpp"

extern "C" {
#include "torrent.h"
}

#include <borealis/core/logger.hpp>

#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace stremio_torrent {
namespace {

constexpr uint64_t kTickSleepUs = 250 * 1000;
constexpr int kCachePathLen = 512;

struct WorkerArg {
    TorrentPlayer* self;
    std::shared_ptr<Session> session;
};

uint64_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000u);
}

const char* metaStage(int state) {
    switch (state) {
        case META_PARSE:
            return "Analizando enlace magnet...";
        case META_ANNOUNCE:
            return "Buscando pares en los trackers...";
        case META_FETCH:
            return "Descargando metadatos (BEP 9)...";
        case META_BUILD:
            return "Procesando metadatos...";
        case META_DONE:
            return "Metadatos recibidos, iniciando descarga...";
        default:
            return "Iniciando...";
    }
}

} // namespace

TorrentPlayer& TorrentPlayer::getInstance() {
    static TorrentPlayer instance;
    return instance;
}

TorrentPlayer::~TorrentPlayer() {
    stop();
}

std::string TorrentPlayer::start(const std::string& source, int fileIdx) {
    if (source.empty())
        return "Enlace de stream vacío.";

    FilePaths::ensureDataDir();

    std::shared_ptr<Session> prev;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        prev = session_;
    }
    if (prev) {
        prev->cancel = true;
        if (torrentfs* t = tfs_.load())
            torrentfs_cancel(t);
        if (prev->hasThread.load() && !prev->joined.exchange(true)) {
            if (prev->opening.load()) {
                pthread_detach(prev->thread);
            } else {
                pthread_join(prev->thread, nullptr);
            }
        }
    }

    std::shared_ptr<Session> session = std::make_shared<Session>();
    session->source = source;
    session->fileIdx = fileIdx;
    session->cancel = false;
    session->opening.store(false);
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        session_ = session;
    }

    active_.store(true);
    phase_.store(TorrentPhase::Resolving);
    tfs_.store(nullptr);
    fileSize_.store(0);
    speedBps_.store(0);
    peers_.store(0);
    piecesDone_.store(0);
    pieces_.store(0);
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        stage_ = "Iniciando...";
        error_.clear();
    }

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
    if (pthread_create(&thread, &attr, workerEntry, new WorkerArg{this, session}) != 0) {
        pthread_attr_destroy(&attr);
        {
            std::lock_guard<std::mutex> lock(sessionMutex_);
            session_.reset();
        }
        active_.store(false);
        phase_.store(TorrentPhase::Idle);
        return "No se pudo crear el hilo de torrent.";
    }
    pthread_attr_destroy(&attr);
    session->thread = thread;
    session->hasThread.store(true);
    thread_ = thread;
    return "";
}

void* TorrentPlayer::workerEntry(void* arg) {
    WorkerArg* a = static_cast<WorkerArg*>(arg);
    a->self->worker(a->session);
    delete a;
    return nullptr;
}

void TorrentPlayer::worker(std::shared_ptr<Session> session) {
    char err[256] = {0};
    char cachePath[kCachePathLen];
    snprintf(cachePath, sizeof(cachePath), "%s/stream.bin",
             FilePaths::torrentsDir().c_str());

    session->opening.store(true);
    torrentfs_set_ram_stream(1);
    torrentfs* t = torrentfs_open_file_ex(session->source.c_str(), cachePath,
                                          session->fileIdx, &session->cancel,
                                          err, sizeof(err));
    session->opening.store(false);

    bool current = false;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        current = session_.get() == session.get();
    }

    if (session->cancel || !current) {
        if (t)
            torrentfs_close(t);
        if (current) {
            phase_.store(TorrentPhase::Idle);
            active_.store(false);
        }
        return;
    }

    if (!t) {
        {
            std::lock_guard<std::mutex> lock(statusMutex_);
            error_ = err[0] ? err : "No se pudo iniciar el motor torrent.";
            stage_ = "Error";
        }
        phase_.store(TorrentPhase::Error);
        active_.store(false);
        return;
    }

    tfs_.store(t);
    fileSize_.store((uint64_t)torrentfs_size(t));
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        stage_ = "Descargando...";
    }
    phase_.store(TorrentPhase::Downloading);

    uint64_t lastBytes = (uint64_t)torrentfs_bytes_recv(t);
    uint64_t lastMs = now_ms();

    while (!session->cancel) {
        {
            std::lock_guard<std::mutex> lock(sessionMutex_);
            if (session_.get() != session.get())
                break;
        }

        int64_t done = 0, total = 0, playhead = 0;
        torrentfs_stats(t, &done, &total, &playhead);
        uint64_t b = (uint64_t)torrentfs_bytes_recv(t);
        uint64_t now = now_ms();
        if (now > lastMs)
            speedBps_.store((b - lastBytes) * 1000u / (now - lastMs));
        else
            speedBps_.store(0);
        lastBytes = b;
        lastMs = now;

        int live = 0, peak = 0, connecting = 0;
        torrentfs_live_peers(t, &live, &peak, &connecting);
        peers_.store((uint32_t)live);
        piecesDone_.store((uint32_t)done);
        pieces_.store((uint32_t)total);

        if (total > 0 && done >= total)
            phase_.store(TorrentPhase::Ready);
        usleep(kTickSleepUs);
    }

    torrentfs_cancel(t);
    torrentfs_close(t);
    if (tfs_.load() == t)
        tfs_.store(nullptr);
    phase_.store(TorrentPhase::Idle);
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        if (session_.get() == session.get())
            active_.store(false);
    }
}

void TorrentPlayer::cancel() {
    std::shared_ptr<Session> s = currentSession();
    if (s)
        s->cancel = true;
    if (torrentfs* t = tfs_.load())
        torrentfs_cancel(t);
}

void TorrentPlayer::close() {
    std::shared_ptr<Session> s = currentSession();
    if (!s)
        return;
    if (s->hasThread.load() && !s->joined.exchange(true)) {
        if (s->opening.load()) {
            pthread_detach(s->thread);
        } else {
            pthread_join(s->thread, nullptr);
        }
    }
}

void TorrentPlayer::stop() {
    cancel();
    close();
}

TorrentStatus TorrentPlayer::status() const {
    TorrentStatus s;
    s.phase = phase_.load();

    torrentfs* t = tfs_.load();
    if (t) {
        int64_t done = 0, total = 0, playhead = 0;
        torrentfs_stats(t, &done, &total, &playhead);
        int64_t plen = torrentfs_piece_len(t);
        int64_t size = torrentfs_size(t);
        uint64_t downloaded = (uint64_t)done * (uint64_t)plen;
        if ((int64_t)downloaded > size) downloaded = (uint64_t)size;
        s.downloaded = downloaded;
        s.total = (uint64_t)size;
        s.speed_bps = speedBps_.load();
        int live = 0, peak = 0, connecting = 0;
        torrentfs_live_peers(t, &live, &peak, &connecting);
        s.peers = (uint32_t)live;
        s.pieces_done = (uint32_t)done;
        s.pieces = (uint32_t)total;
    } else if (s.phase == TorrentPhase::Resolving) {
        s.stage = metaStage(torrent_meta_state);
        s.peers = (uint32_t)torrent_meta_connected;
        if (torrent_meta_state == META_FAIL && !currentSessionCancelled()) {
            s.phase = TorrentPhase::Error;
            s.error = torrent_meta_last_err;
        }
    }

    std::lock_guard<std::mutex> lock(statusMutex_);
    if (s.stage.empty())
        s.stage = stage_;
    s.error = error_;
    return s;
}

bool TorrentPlayer::streamFileInfo(std::string& path, uint64_t& fileStart,
                                   uint64_t& fileSize) const {
    torrentfs* t = tfs_.load();
    if (!t)
        return false;
    path.clear();
    fileStart = 0;
    fileSize = fileSize_.load();
    return fileSize > 0;
}

bool TorrentPlayer::rangeReady(uint64_t rel, uint64_t len) const {
    torrentfs* t = tfs_.load();
    if (!t)
        return false;
    int64_t plen = torrentfs_piece_len(t);
    if (plen <= 0)
        return false;
    int64_t offset = torrentfs_stream_offset(t);
    int64_t first = (offset + (int64_t)rel) / plen;
    int64_t last = (offset + (int64_t)rel + (int64_t)len - 1) / plen;
    for (int64_t pc = first; pc <= last; pc++) {
        if (!torrentfs_piece_done(t, pc))
            return false;
    }
    return true;
}

bool TorrentPlayer::waitForRange(uint64_t rel, uint64_t len,
                                 uint64_t timeoutMs) {
    uint64_t deadline = 0;
    if (timeoutMs > 0) {
        uint64_t start = now_ms();
        deadline = start + timeoutMs;
    }
    while (!currentSessionCancelled()) {
        if (rangeReady(rel, len))
            return true;
        if (phase_.load() == TorrentPhase::Error)
            return false;
        usleep(50 * 1000);
        if (timeoutMs > 0 && now_ms() >= deadline)
            return false;
    }
    return false;
}

} // namespace stremio_torrent