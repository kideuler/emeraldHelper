#include "emerald/mainwindow.h"

#include "emerald/battlerview.h"
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

enum Row {
    RowName = 0,
    RowHp,
    RowMove1,
    RowMove2,
    RowMove3,
    RowMove4,
    RowLevel,
    RowTypes,
    RowAbility,
    RowStatus,
    RowStages,
    RowAttack,
    RowDefense,
    RowSpAttack,
    RowSpDefense,
    RowSpeed,
    RowCount,
};

const char *const kRowLabels[RowCount] = {
    "Name", "HP", "Move 1", "Move 2", "Move 3", "Move 4", "Level", "Type",
    "Ability", "Status", "Stages", "Attack", "Defense", "Sp. Atk", "Sp. Def", "Speed",
};

QString FormatMoveCell(const MoveDisplay &m)
{
    if (!m.present)
        return QString();

    QString name = m.name;
    if (!m.details.isEmpty())
        name += QStringLiteral(" [%1]").arg(m.details.join(QStringLiteral(", ")));

    if (!m.hasDamageRange) // status move, no opponent, or no effect / fails -- say why if there's a reason
        return m.note.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, m.note);

    QString cell = QStringLiteral("%1: %2-%3 (%4%-%5%)")
                       .arg(name)
                       .arg(m.minDamage)
                       .arg(m.maxDamage)
                       .arg(m.minPercent, 0, 'f', 1)
                       .arg(m.maxPercent, 0, 'f', 1);
    if (m.koChance > 0.0)
        cell += QStringLiteral(" KO %1%").arg(100.0 * m.koChance, 0, 'f', m.koChance < 0.1 ? 1 : 0);
    if (!m.note.isEmpty())
        cell += QStringLiteral(" (%1)").arg(m.note);
    return cell;
}

void FillColumn(QTableWidget *table, int col, const BattlerColumn &c)
{
    auto set = [&](int row, const QString &text) {
        table->setItem(row, col, new QTableWidgetItem(text));
    };

    if (!c.present) {
        for (int row = 0; row < RowCount; ++row)
            set(row, row == RowName ? QStringLiteral("--") : QString());
        return;
    }

    set(RowName, c.name);
    set(RowHp, QStringLiteral("%1 / %2").arg(c.hp).arg(c.maxHp));
    set(RowMove1, FormatMoveCell(c.moves[0]));
    set(RowMove2, FormatMoveCell(c.moves[1]));
    set(RowMove3, FormatMoveCell(c.moves[2]));
    set(RowMove4, FormatMoveCell(c.moves[3]));
    set(RowLevel, QString::number(c.level));
    set(RowTypes, c.typeText);
    set(RowAbility, c.abilityText);
    set(RowStatus, c.statusText);
    set(RowStages, c.stagesText);
    set(RowAttack, QString::number(c.attack));
    set(RowDefense, QString::number(c.defense));
    set(RowSpAttack, QString::number(c.spAttack));
    set(RowSpDefense, QString::number(c.spDefense));
    set(RowSpeed, QString::number(c.speed));
}

QStringList ColumnHeaders(bool isDoubleBattle)
{
    if (isDoubleBattle)
        return {QStringLiteral("Ally L"), QStringLiteral("Ally R"), QStringLiteral("Foe L"), QStringLiteral("Foe R")};
    return {QStringLiteral("Ally"), QStringLiteral("Foe")};
}

} // namespace

MainWindow::MainWindow(const SymbolTable &symbols, const BattleLayouts &layouts, const NameTable &names,
                        QWidget *parent)
    : QMainWindow(parent)
    , m_symbols(symbols)
    , m_layouts(layouts)
    , m_names(names)
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    m_statusLabel = new QLabel(tr("Waiting for mGBA bridge connection..."), central);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_fieldLabel = new QLabel(central);
    layout->addWidget(m_fieldLabel);

    m_table = new QTableWidget(RowCount, 2, central);
    QStringList rowLabels;
    for (const char *label : kRowLabels)
        rowLabels << QString::fromLatin1(label);
    m_table->setVerticalHeaderLabels(rowLabels);
    m_table->setHorizontalHeaderLabels(ColumnHeaders(false));
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_table);

    setCentralWidget(central);
    setWindowTitle(tr("Emerald Battle Companion"));
    resize(1200, 600);

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

    const BattleSnapshot snap = decodeSnapshot(raw, m_symbols, m_layouts);
    if (!snap.valid) {
        setStale(true);
        return;
    }
    applySnapshot(snap);
}

void MainWindow::applySnapshot(const BattleSnapshot &snap)
{
    QString status = snap.inBattle() ? tr("In battle") : tr("Connected, not in battle");
    // Don't let numbers computed without (say) the weather pass as exact:
    // name what the bridge isn't sending, which usually means mGBA is still
    // running an older lua/emerald_bridge.lua.
    if (!snap.missingRegions.isEmpty())
        status += tr(" -- bridge isn't sending %1; damage ignores them (reload lua/emerald_bridge.lua?)")
                      .arg(snap.missingRegions.join(QStringLiteral(", ")));
    m_statusLabel->setText(status);

    const BattlerGrid grid = BuildBattlerGrid(snap, m_names);
    m_fieldLabel->setText(grid.fieldText.isEmpty() ? QString() : tr("Field: %1").arg(grid.fieldText));

    if (m_table->columnCount() != grid.columns.size()) {
        m_table->setColumnCount(grid.columns.size());
        m_table->setHorizontalHeaderLabels(ColumnHeaders(grid.isDoubleBattle));
    }

    for (int col = 0; col < grid.columns.size(); ++col)
        FillColumn(m_table, col, grid.columns.at(col));

    m_table->setEnabled(true);
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
