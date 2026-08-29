#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QVector>
#include <QtGlobal>

// Phase 3.3 wire format (see helperapp/lua/emerald_bridge.lua):
//   [u32 magic 'EMBC'][u16 version][u16 regionCount]
//     [u32 gbaAddr][u32 length][bytes...] x regionCount
//
// A region is identified by its gbaAddr, resolved back to a symbol name via
// SymbolTable on the receiving side -- not by position -- so the Qt side
// stays version-tolerant as described in the plan.
namespace emerald {

struct RawRegion {
    quint32 addr = 0;
    QByteArray bytes;
};

struct RawSnapshot {
    quint16 version = 0;
    QVector<RawRegion> regions;
};

} // namespace emerald

Q_DECLARE_METATYPE(emerald::RawSnapshot)
