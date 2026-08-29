#include "emerald/symboltable.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace emerald {

SymbolTable SymbolTable::loadFromFile(const QString &path, QString *error)
{
    SymbolTable table;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return table;
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
        return table;
    }
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("%1: expected a JSON object").arg(path);
        return table;
    }

    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (!it.value().isDouble())
            continue; // ignore non-address entries (e.g. future metadata keys)
        const quint32 addr = quint32(it.value().toDouble());
        table.m_byName.insert(it.key(), addr);
        table.m_byAddress.insert(addr, it.key());
    }

    table.m_valid = true;
    return table;
}

QStringList SymbolTable::missing(const QStringList &required) const
{
    QStringList out;
    for (const QString &name : required) {
        if (!m_byName.contains(name))
            out.append(name);
    }
    return out;
}

} // namespace emerald
