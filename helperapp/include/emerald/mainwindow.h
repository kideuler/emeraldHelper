#pragma once

#include "emerald/battlesnapshot.h"
#include "emerald/rawsnapshot.h"
#include "emerald/structlayout.h"
#include "emerald/symboltable.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QLabel>
#include <QMainWindow>
#include <QTableWidget>

// Phase 4 / 7 v1: deliberately minimal ("ugly table, live, correct" --
// Milestone 7). A correctness-first placeholder, not the polished layout
// described in Phase 7 (which is out of scope here).
namespace emerald {

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(const SymbolTable &symbols, const StructLayout &monLayout, QWidget *parent = nullptr);

public slots:
    void onRawSnapshot(const emerald::RawSnapshot &raw);
    void onConnectionChanged(bool connected);

private:
    void applySnapshot(const BattleSnapshot &snap);
    void setStale(bool stale);

    SymbolTable m_symbols;
    StructLayout m_monLayout;

    QLabel *m_statusLabel;
    QTableWidget *m_table;

    QByteArray m_lastHash;
    bool m_connected = false;
};

} // namespace emerald
