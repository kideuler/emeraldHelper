#pragma once

#include "emerald/frameparser.h"
#include "emerald/rawsnapshot.h"

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

// Phase 4.1: Qt listens, Lua connects out (mGBA's socket.connect() is
// blocking, so it must be the emulator that dials in on a timer -- never
// the other way around). Only one emulator connection is accepted at a
// time; a second concurrent connection attempt is refused.
namespace emerald {

class MemoryBridge : public QObject
{
    Q_OBJECT
public:
    explicit MemoryBridge(quint16 port, QObject *parent = nullptr);

    bool isConnected() const { return m_client != nullptr; }
    bool isListening() const { return m_server->isListening(); }
    QString errorString() const { return m_server->errorString(); }

signals:
    void rawSnapshot(const emerald::RawSnapshot &raw);
    void connectionChanged(bool connected);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    QTcpServer *m_server;
    QTcpSocket *m_client = nullptr;
    FrameParser m_parser;
};

} // namespace emerald
