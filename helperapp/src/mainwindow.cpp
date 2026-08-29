#include "emerald/mainwindow.h"

#include "emerald/decoder.h"

#include <QHeaderView>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

namespace emerald {

namespace {

// Phase 4.4: hash the raw bytes (not the decoded snapshot) so an unchanged
// frame is skipped before paying for decode + a UI repaint.
QByteArray hashRawSnapshot(const RawSnapshot &raw)
{
    QCryptographicHash hash(QCryptographicHash::Md5);
    for (const RawRegion &r : raw.regions) {
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(&r.addr), sizeof(r.addr)));
        hash.addData(r.bytes);
    }
    return hash.result();
}

} // namespace

MainWindow::MainWindow(const SymbolTable &symbols, const StructLayout &monLayout, QWidget *parent)
    : QMainWindow(parent)
    , m_symbols(symbols)
    , m_monLayout(monLayout)
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    m_statusLabel = new QLabel(tr("Waiting for mGBA bridge connection..."), central);
    layout->addWidget(m_statusLabel);

    m_table = new QTableWidget(kMaxBattlers, 8, central);
    m_table->setHorizontalHeaderLabels({tr("Battler"), tr("Species"), tr("Lvl"), tr("HP"),
                                         tr("Max HP"), tr("Status"), tr("Ability"), tr("Types")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    layout->addWidget(m_table);

    setCentralWidget(central);
    setWindowTitle(tr("Emerald Battle Companion"));
    resize(640, 320);

    setStale(true);
}

void MainWindow::onConnectionChanged(bool connected)
{
    m_connected = connected;
    if (!connected) {
        m_lastHash.clear();
        setStale(true);
    } else {
        m_statusLabel->setText(tr("Connected. Waiting for battle data..."));
    }
}

void MainWindow::onRawSnapshot(const RawSnapshot &raw)
{
    const QByteArray hash = hashRawSnapshot(raw);
    if (hash == m_lastHash)
        return; // Phase 4.4: identical frame, nothing to redo.
    m_lastHash = hash;

    const BattleSnapshot snap = decodeSnapshot(raw, m_symbols, m_monLayout);
    if (!snap.valid) {
        setStale(true);
        return;
    }
    applySnapshot(snap);
}

void MainWindow::applySnapshot(const BattleSnapshot &snap)
{
    m_statusLabel->setText(snap.inBattle() ? tr("In battle") : tr("Connected, not in battle"));

    for (int i = 0; i < kMaxBattlers; ++i) {
        const BattleMon &mon = snap.mons[i];
        const bool absent = ((snap.absentBattlerFlags >> i) & 0x1) != 0;

        m_table->setItem(i, 0, new QTableWidgetItem(tr("Battler %1").arg(i)));
        m_table->setItem(i, 1, new QTableWidgetItem(absent ? tr("--") : QString::number(mon.species)));
        m_table->setItem(i, 2, new QTableWidgetItem(QString::number(mon.level)));
        m_table->setItem(i, 3, new QTableWidgetItem(QString::number(mon.hp)));
        m_table->setItem(i, 4, new QTableWidgetItem(QString::number(mon.maxHp)));
        m_table->setItem(i, 5, new QTableWidgetItem(QString::number(mon.status1)));
        m_table->setItem(i, 6, new QTableWidgetItem(QString::number(mon.ability)));
        m_table->setItem(i, 7, new QTableWidgetItem(QStringLiteral("%1/%2").arg(mon.type1).arg(mon.type2)));
    }
}

void MainWindow::setStale(bool stale)
{
    // Phase gotcha: "confidently wrong numbers are worse than a blank
    // panel" -- grey the table out rather than leaving the last values
    // looking current when the bridge has nothing (or nothing new) to say.
    m_table->setEnabled(!stale);
    if (stale && !m_connected)
        m_statusLabel->setText(tr("Waiting for mGBA bridge connection..."));
}

} // namespace emerald
