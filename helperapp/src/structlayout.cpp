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
        layout.m_fields.insert(it.key(), info);
    }

    layout.m_valid = true;
    return layout;
}

} // namespace emerald
