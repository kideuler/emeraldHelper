#include "emerald/structlayout.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace emerald {

StructLayout StructLayout::loadFromFile(const QString &path, const QString &structName,
                                         QString *error)
{
    StructLayout layout;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return layout;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull()) {
        if (error) {
            *error = QStringLiteral("%1: invalid JSON at offset %2: %3")
                         .arg(path)
                         .arg(parseError.offset)
                         .arg(parseError.errorString());
        }
        return layout;
    }

    const QJsonObject structs = doc.object().value(QStringLiteral("structs")).toObject();
    if (!structs.contains(structName)) {
        if (error)
            *error = QStringLiteral("%1: no struct named %2").arg(path, structName);
        return layout;
    }

    const QJsonObject entry = structs.value(structName).toObject();
    layout.m_totalSize = entry.value(QStringLiteral("size")).toInt();

    const QJsonObject fields = entry.value(QStringLiteral("fields")).toObject();
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const QJsonObject f = it.value().toObject();
        FieldInfo info;
        info.offset = f.value(QStringLiteral("offset")).toInt();
        info.size = f.value(QStringLiteral("size")).toInt();
        info.count = f.contains(QStringLiteral("count")) ? f.value(QStringLiteral("count")).toInt() : 1;
        info.bits = f.value(QStringLiteral("bits")).toInt();
        info.bitOffset = f.value(QStringLiteral("bitOffset")).toInt();
        if (info.bits != 0 && !f.contains(QStringLiteral("bitOffset"))) {
            if (error)
                *error = QStringLiteral("%1: %2.%3 is a bitfield with no bitOffset (regenerate with tools/gen_struct_layout.py)")
                             .arg(path, structName, it.key());
            return StructLayout();
        }
        layout.m_fields.insert(it.key(), info);
    }

    layout.m_valid = true;
    return layout;
}

quint32 StructLayout::read(const QByteArray &raw, int base, const QString &field, int index) const
{
    const auto it = m_fields.constFind(field);
    if (it == m_fields.constEnd() || index < 0 || index >= it->count)
        return 0;

    // A bitfield's "offset" is the byte holding its low bit (the decomp's
    // own annotation convention); its storage unit starts bitOffset / 8
    // bytes earlier.
    const int start = it->bits ? base + it->offset - it->bitOffset / 8 : base + it->offset + index * it->size;
    if (start < 0 || it->size > 4 || start + it->size > raw.size())
        return 0;

    quint32 value = 0;
    for (int i = 0; i < it->size; ++i)
        value |= quint32(quint8(raw.at(start + i))) << (8 * i);

    if (it->bits)
        value = (value >> it->bitOffset) & (it->bits >= 32 ? 0xFFFFFFFFu : ((1u << it->bits) - 1));
    return value;
}

} // namespace emerald
