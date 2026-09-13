#pragma once

#include "emerald/battlesnapshot.h"
#include "emerald/decoder.h"
#include "emerald/nametable.h"
#include "emerald/rawsnapshot.h"
#include "emerald/symboltable.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QLabel>
#include <QMainWindow>
#include <QTableWidget>

// Battler-column layout: 2 columns (4 in a double battle), one per
// battler, rows = name / HP / four moves (each with a computed damage
// range) / the rest of the stats. Column/row data comes from
// BuildBattlerGrid() (battlerview.h) -- this class only renders it.
namespace emerald {

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(const SymbolTable &symbols, const BattleLayouts &layouts, const NameTable &names,
               QWidget *parent = nullptr);

public slots:
    void onRawSnapshot(const emerald::RawSnapshot &raw);
    void onConnectionChanged(bool connected);

private:
    void applySnapshot(const BattleSnapshot &snap);
    void setStale(bool stale);

    SymbolTable m_symbols;
    BattleLayouts m_layouts;
    NameTable m_names;

    QLabel *m_statusLabel;
    QLabel *m_fieldLabel;
    QTableWidget *m_table;

    QByteArray m_lastHash;
    bool m_connected = false;
};

} // namespace emerald
