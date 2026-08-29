#pragma once

#include "emerald/rawsnapshot.h"

#include <QByteArray>
#include <QVector>

// Phase 4.1 transport, adapted for the Phase 3.3 multi-region framed
// protocol: frames are no longer fixed-size, so parsing needs a small
// incremental state machine instead of a modulo check on buffer size.
//
// Pure and socket-free by design (Phase 2's "layers 2 and 3 must be pure
// functions with no emulator dependency") so it can be fed arbitrary byte
// chunks in tests -- including single bytes at a time -- to prove it
// survives TCP frames splitting a logical message across reads.
namespace emerald {

class FrameParser
{
public:
    // Appends `data` to the internal buffer and returns every complete
    // frame it now contains, in receive order. Leaves a trailing partial
    // frame buffered for the next call.
    QVector<RawSnapshot> feed(const QByteArray &data);

    // Bytes buffered but not yet forming a complete frame. Exposed for
    // tests/diagnostics only.
    int pendingBytes() const { return m_buffer.size(); }

private:
    // Attempts to parse exactly one frame starting at the front of
    // m_buffer without consuming it. Returns false if m_buffer does not
    // yet hold a complete frame; on success sets *consumed to the frame's
    // total byte length so the caller can trim the buffer.
    bool tryParseOne(int *consumed, RawSnapshot *out) const;

    // Drops leading bytes up to (not including) the next occurrence of the
    // magic, so a corrupted or unexpected stream doesn't wedge parsing
    // forever. If the magic isn't found, keeps only the last 3 bytes (it
    // may be split across the next read).
    void resync();

    QByteArray m_buffer;
};

} // namespace emerald
