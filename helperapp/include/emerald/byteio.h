#pragma once

#include <QByteArray>
#include <QtGlobal>

// Phase 2.1: explicit little-endian readers. Never reinterpret_cast a
// decomp struct onto received bytes -- ARM AAPCS bitfield packing is
// implementation-defined and will not match x86-64.
namespace emerald {

inline quint8 rd8(const QByteArray &b, int off)
{
    return quint8(b.at(off));
}

inline qint8 rds8(const QByteArray &b, int off)
{
    return qint8(b.at(off));
}

inline quint16 rd16(const QByteArray &b, int off)
{
    return quint16(rd8(b, off)) | (quint16(rd8(b, off + 1)) << 8);
}

inline quint32 rd32(const QByteArray &b, int off)
{
    return quint32(rd16(b, off)) | (quint32(rd16(b, off + 2)) << 16);
}

} // namespace emerald
