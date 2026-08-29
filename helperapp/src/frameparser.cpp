#include "emerald/frameparser.h"

#include "emerald/byteio.h"

#include <cstring>

namespace emerald {

namespace {
constexpr char kMagic[4] = {'E', 'M', 'B', 'C'};
constexpr int kHeaderSize = 4 + 2 + 2;  // magic + version + regionCount
constexpr int kRegionHeaderSize = 4 + 4; // gbaAddr + length

bool magicAt(const QByteArray &buf, int off)
{
    return buf.size() >= off + 4 && std::memcmp(buf.constData() + off, kMagic, 4) == 0;
}
} // namespace

QVector<RawSnapshot> FrameParser::feed(const QByteArray &data)
{
    m_buffer.append(data);

    QVector<RawSnapshot> frames;
    for (;;) {
        if (m_buffer.size() >= 4 && !magicAt(m_buffer, 0))
            resync();

        int consumed = 0;
        RawSnapshot snap;
        if (!tryParseOne(&consumed, &snap))
            break;

        frames.append(std::move(snap));
        m_buffer.remove(0, consumed);
    }
    return frames;
}

bool FrameParser::tryParseOne(int *consumed, RawSnapshot *out) const
{
    if (m_buffer.size() < kHeaderSize || !magicAt(m_buffer, 0))
        return false;

    const quint16 version = rd16(m_buffer, 4);
    const quint16 regionCount = rd16(m_buffer, 6);

    int pos = kHeaderSize;
    QVector<RawRegion> regions;
    regions.reserve(regionCount);

    for (int i = 0; i < regionCount; ++i) {
        if (m_buffer.size() < pos + kRegionHeaderSize)
            return false;

        const quint32 addr = rd32(m_buffer, pos);
        const quint32 len = rd32(m_buffer, pos + 4);
        pos += kRegionHeaderSize;

        if (m_buffer.size() < pos + int(len))
            return false;

        regions.append(RawRegion{addr, m_buffer.mid(pos, int(len))});
        pos += int(len);
    }

    out->version = version;
    out->regions = std::move(regions);
    *consumed = pos;
    return true;
}

void FrameParser::resync()
{
    const int idx = m_buffer.indexOf(QByteArray(kMagic, 4));
    if (idx >= 0) {
        m_buffer.remove(0, idx);
        return;
    }
    // Magic may be split across this read and the next; keep a tail long
    // enough to still recognize it once more data arrives.
    if (m_buffer.size() > 3)
        m_buffer.remove(0, m_buffer.size() - 3);
}

} // namespace emerald
