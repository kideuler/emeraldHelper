#include "emerald/mainwindow.h"
#include "emerald/memorybridge.h"
#include "emerald/structlayout.h"
#include "emerald/symboltable.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>

// Phase 4.3 note: this stays single-threaded per the plan's guidance
// ("start single-threaded... move onto a worker QThread once decoding plus
// calculation exceeds a few milliseconds"). decodeSnapshot() over one
// 4-battler frame is microseconds of work, so there's no need yet.

namespace {
QString configDir()
{
    const QByteArray env = qgetenv("EMERALD_CONFIG_DIR");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
#ifdef EMERALD_CONFIG_DIR
    return QStringLiteral(EMERALD_CONFIG_DIR);
#else
    return QDir::currentPath() + QStringLiteral("/config");
#endif
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    qRegisterMetaType<emerald::RawSnapshot>("emerald::RawSnapshot");

    const QString dir = configDir();
    QString error;

    const emerald::SymbolTable symbols =
        emerald::SymbolTable::loadFromFile(dir + QStringLiteral("/symbols_us_rev0.json"), &error);
    if (!symbols.isValid()) {
        QMessageBox::critical(nullptr, QObject::tr("Emerald Battle Companion"),
                               QObject::tr("Failed to load symbol table:\n%1").arg(error));
        return 1;
    }

    const emerald::StructLayout monLayout = emerald::StructLayout::loadFromFile(
        dir + QStringLiteral("/battle_pokemon_layout_us_rev0.json"), QStringLiteral("BattlePokemon"),
        &error);
    if (!monLayout.isValid()) {
        QMessageBox::critical(nullptr, QObject::tr("Emerald Battle Companion"),
                               QObject::tr("Failed to load struct layout:\n%1").arg(error));
        return 1;
    }

    static const QStringList kRequired = {
        "gBattleMons", "gBattleTypeFlags", "gBattlerAttacker", "gBattlerTarget",
        "gAbsentBattlerFlags", "gBattlerPartyIndexes", "gPlayerParty", "gEnemyParty",
    };
    const QStringList missing = symbols.missing(kRequired);
    if (!missing.isEmpty()) {
        QMessageBox::critical(nullptr, QObject::tr("Emerald Battle Companion"),
                               QObject::tr("Symbol table is missing required addresses:\n%1")
                                   .arg(missing.join(QStringLiteral(", "))));
        return 1;
    }

    emerald::MainWindow window(symbols, monLayout);
    emerald::MemoryBridge bridge(8888);

    QObject::connect(&bridge, &emerald::MemoryBridge::rawSnapshot, &window,
                      &emerald::MainWindow::onRawSnapshot);
    QObject::connect(&bridge, &emerald::MemoryBridge::connectionChanged, &window,
                      &emerald::MainWindow::onConnectionChanged);

    window.show();
    return app.exec();
}
