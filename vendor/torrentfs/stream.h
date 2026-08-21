#ifndef STREAM_H
#define STREAM_H

#include <mpv/client.h>

#include "torrentfs.h"

// Registers a custom mpv protocol so `loadfile torrent://...` reads through our
// callbacks, backed by the streaming torrent store. mpv issues blocking
// read/seek calls; the torrentfs layer downloads on demand around the playhead.
//
// The protocol is registered once against `holder`, a pointer to the caller's
// live `torrentfs *`; the open callback reads the store out of it each time
// mpv opens the stream, so successive playback sessions can swap stores
// without re-registering. `holder` must outlive the mpv instance. Returns 0 on
// success.
int stream_register(mpv_handle *mpv, torrentfs **holder);

#endif
