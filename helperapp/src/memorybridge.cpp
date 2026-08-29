#include "emerald/memorybridge.h"

namespace emerald {

MemoryBridge::MemoryBridge(quint16 port, QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &MemoryBridge::onNewConnection);
    if (!m_server->listen(QHostAddress::LocalHost, port))
        qWarning("MemoryBridge: listen failed: %s", qUtf8Printable(m_server->errorString()));
}

void MemoryBridge::onNewConnection()
{
    if (m_client) {
        // Only one emulator at a time.
        m_server->nextPendingConnection()->deleteLater();
        return;
    }
    m_client = m_server->nextPendingConnection();
    connect(m_client, &QTcpSocket::readyRead, this, &MemoryBridge::onReadyRead);
    connect(m_client, &QTcpSocket::disconnected, this, &MemoryBridge::onDisconnected);
    emit connectionChanged(true);
}

void MemoryBridge::onReadyRead()
{
    const QVector<RawSnapshot> frames = m_parser.feed(m_client->readAll());
    for (const RawSnapshot &frame : frames)
        emit rawSnapshot(frame);
}

void MemoryBridge::onDisconnected()
{
    m_client->deleteLater();
    m_client = nullptr;
    m_parser = FrameParser();
    emit connectionChanged(false);
}

} // namespace emerald
