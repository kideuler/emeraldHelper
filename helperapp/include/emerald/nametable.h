#pragma once

#include <QString>
#include <QVector>

// Loads display_names_us_rev0.json (tools/gen_display_names.py), which
// pulls species/move/ability/type names straight out of the decomp's own
// src/data/text/*.h tables rather than hand-transcribing them.
namespace emerald {

class NameTable
{
public:
    static NameTable loadFromFile(const QString &path, QString *error = nullptr);

    bool isValid() const { return m_valid; }

    // Out-of-range or unresolved ids return a fallback like "#123" rather
    // than an empty string, so a stale/partial table degrades to
    // "still shows something" instead of a blank cell.
    QString species(int id) const { return lookup(m_species, id); }
    QString move(int id) const { return lookup(m_moves, id); }
    QString ability(int id) const { return lookup(m_abilities, id); }
    QString type(int id) const { return lookup(m_types, id); }

private:
    static QString lookup(const QVector<QString> &table, int id);

    QVector<QString> m_species;
    QVector<QString> m_moves;
    QVector<QString> m_abilities;
    QVector<QString> m_types;
    bool m_valid = false;
};

} // namespace emerald
