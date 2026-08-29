#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

// Phase 1.3 / 4: loads the runtime-loaded symbols_*.json config produced by
// tools/gen_symbols.py. Supporting a new ROM revision costs one JSON file,
// not a rebuild -- so this must stay a runtime load, never a compiled-in
// header of addresses.
namespace emerald {

class SymbolTable
{
public:
    // On failure returns an empty (isValid() == false) table and, if
    // `error` is non-null, a human-readable message.
    static SymbolTable loadFromFile(const QString &path, QString *error = nullptr);

    bool isValid() const { return m_valid; }
    bool contains(const QString &name) const { return m_byName.contains(name); }

    // Returns 0 if `name` is not present; check contains() first if that's
    // ambiguous with a legitimate address.
    quint32 address(const QString &name) const { return m_byName.value(name, 0); }

    // Reverse lookup, used to identify which symbol a received region's
    // gbaAddr corresponds to. Returns an empty string if unknown.
    QString nameForAddress(quint32 addr) const { return m_byAddress.value(addr); }

    // Names from `required` that are absent from this table.
    QStringList missing(const QStringList &required) const;

private:
    QMap<QString, quint32> m_byName;
    QMap<quint32, QString> m_byAddress;
    bool m_valid = false;
};

} // namespace emerald
