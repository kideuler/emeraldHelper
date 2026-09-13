#include "emerald/mainwindow.h"
#include "emerald/memorybridge.h"
#include "emerald/nametable.h"
#include "emerald/decoder.h"
#include "emerald/symboltable.h"

extern "C" {
#include "emerald_calc.h"
}

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QVector>

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

// Phase 5: hands the calc library the item -> hold-effect table (tools/
// gen_item_hold_effects.py). Non-fatal if missing -- the app still runs,
// just with GetItemHoldEffect() always returning HOLD_EFFECT_NONE. The
// backing vectors are static so the pointers EmeraldCalc_SetItemHoldEffects
// stores stay valid for the process lifetime.
void loadItemHoldEffects(const QString &path)
{
    static QVector<quint8> holdEffect;
    static QVector<quint8> holdEffectParam;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("main: no item hold-effect table at %s (items won't affect damage calc)",
                 qUtf8Printable(path));
        return;
    }
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray he = obj.value(QStringLiteral("holdEffect")).toArray();
    const QJsonArray hep = obj.value(QStringLiteral("holdEffectParam")).toArray();
    if (he.size() != hep.size() || he.isEmpty()) {
        qWarning("main: malformed item hold-effect table at %s", qUtf8Printable(path));
        return;
    }

    holdEffect.resize(he.size());
    holdEffectParam.resize(hep.size());
    for (int i = 0; i < he.size(); ++i) {
        holdEffect[i] = quint8(he.at(i).toInt());
        holdEffectParam[i] = quint8(hep.at(i).toInt());
    }
    EmeraldCalc_SetItemHoldEffects(holdEffect.constData(), holdEffectParam.constData(), quint16(holdEffect.size()));
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

    const emerald::BattleLayouts layouts =
        emerald::BattleLayouts::loadFromFile(dir + QStringLiteral("/battle_pokemon_layout_us_rev0.json"), &error);
    if (!layouts.isValid()) {
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
    // The rest of BattleState: without these the app still runs, but the
    // decoder reports them missing every frame and the UI says so.
    static const QStringList kBattleState = {
        "gBattlersCount", "gBattlerPositions", "gBattleWeather", "gSideStatuses", "gSideTimers",
        "gStatuses3", "gDisableStructs", "gProtectStructs", "gEnigmaBerries", "gBattleEnvironment",
        "gBattleResources", "gSaveBlock1Ptr", "gTrainerBattleOpponent_A",
    };
    const QStringList missingState = symbols.missing(kBattleState);
    if (!missingState.isEmpty())
        qWarning("main: symbol table has no %s -- regenerate it with tools/gen_symbols.py",
                 qUtf8Printable(missingState.join(QStringLiteral(", "))));

    loadItemHoldEffects(dir + QStringLiteral("/item_hold_effects_us_rev0.json"));

    const emerald::NameTable names =
        emerald::NameTable::loadFromFile(dir + QStringLiteral("/display_names_us_rev0.json"), &error);
    if (!names.isValid()) {
        QMessageBox::critical(nullptr, QObject::tr("Emerald Battle Companion"),
                               QObject::tr("Failed to load display names:\n%1").arg(error));
        return 1;
    }

    emerald::MainWindow window(symbols, layouts, names);
    emerald::MemoryBridge bridge(8888);

    QObject::connect(&bridge, &emerald::MemoryBridge::rawSnapshot, &window,
                      &emerald::MainWindow::onRawSnapshot);
    QObject::connect(&bridge, &emerald::MemoryBridge::connectionChanged, &window,
                      &emerald::MainWindow::onConnectionChanged);

    window.show();
    return app.exec();
}
