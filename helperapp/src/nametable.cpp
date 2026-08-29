#include "emerald/nametable.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace emerald {

namespace {
QVector<QString> readArray(const QJsonObject &obj, const char *key)
{
    QVector<QString> out;
    const QJsonArray arr = obj.value(QLatin1String(key)).toArray();
    out.reserve(arr.size());
    for (const QJsonValue &v : arr)
        out.append(v.toString());
    return out;
}
} // namespace

NameTable NameTable::loadFromFile(const QString &path, QString *error)
{
    NameTable table;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return table;
    }

    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    if (obj.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1: invalid or empty JSON").arg(path);
        return table;
    }

    table.m_species = readArray(obj, "species");
    table.m_moves = readArray(obj, "moves");
    table.m_abilities = readArray(obj, "abilities");
    table.m_types = readArray(obj, "types");
    table.m_valid = true;
    return table;
}

QString NameTable::lookup(const QVector<QString> &table, int id)
{
    if (id < 0 || id >= table.size() || table.at(id).isEmpty())
        return QStringLiteral("#%1").arg(id);
    return table.at(id);
}

} // namespace emerald
