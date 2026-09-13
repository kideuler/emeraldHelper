#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

// Phase 1.4 / 2: loads the field offset/size/count contract produced by
// tools/gen_struct_layout.py (helperapp/config/battle_pokemon_layout_*.json)
// for a single named struct (e.g. "BattlePokemon"). This is the contract
// between the decomp source and the decoder -- offsets are looked up by
// field name here rather than hardcoded as magic numbers in decoder.cpp.
namespace emerald {

class StructLayout
{
public:
    // `structName` selects one entry from the "structs" object in the JSON
    // (e.g. "BattlePokemon"). On failure returns an invalid table and, if
    // `error` is non-null, a human-readable message.
    static StructLayout loadFromFile(const QString &path, const QString &structName,
                                      QString *error = nullptr);

    bool isValid() const { return m_valid; }
    bool contains(const QString &field) const { return m_fields.contains(field); }

    int offset(const QString &field) const { return m_fields.value(field).offset; }
    int elemSize(const QString &field) const { return m_fields.value(field).size; }
    int count(const QString &field) const { return m_fields.value(field).count; }
    // Bitfields: `bits` wide, starting `bitOffset` bits into the elemSize()-
    // byte storage unit. For a plain field bits() is 0.
    int bits(const QString &field) const { return m_fields.value(field).bits; }
    int bitOffset(const QString &field) const { return m_fields.value(field).bitOffset; }
    int totalSize() const { return m_totalSize; }

    // Reads `field` (element `index` of it, for arrays) out of a struct
    // instance starting at `base` in `raw`: little-endian, bitfields
    // shifted and masked. Returns 0 for an unknown field or out-of-range
    // read, so a short or stale region degrades to zeroes, never a crash.
    quint32 read(const QByteArray &raw, int base, const QString &field, int index = 0) const;

private:
    struct FieldInfo {
        int offset = 0;
        int size = 0;
        int count = 1;
        int bits = 0;
        int bitOffset = 0;
    };

    QMap<QString, FieldInfo> m_fields;
    int m_totalSize = 0;
    bool m_valid = false;
};

} // namespace emerald
