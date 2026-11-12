/*
 * VAPlayer - QmpClient protocol tests
 *
 * Copyright (C) 2026 VAPlayer contributors
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "core_bridge/QmpClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTest>

/**
 * Exercises QmpClient against a minimal in-process QMP server.  The server
 * speaks the real wire protocol (greeting, capabilities, replies, events) so
 * the client's framing and handshake logic is tested end to end.
 */
class TestQmpClient : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void handshakeReachesReadyState();
    void commandReplyIsDelivered();
    void eventIsSurfaced();
    void errorReplyEmitsError();
    void sendKeyUsesKeyValueUnion();

private:
    void serveGreeting(QLocalSocket *peer);
    void writeLine(QLocalSocket *peer, const QJsonObject &obj);
    void waitForPeer();

    QLocalServer *m_server = nullptr;
    QString m_socketName;
    QLocalSocket *m_peer = nullptr;
    int m_counter = 0;
};

void TestQmpClient::init()
{
    // A fresh server per test keeps listen state from leaking between tests,
    // and a unique short name keeps the socket path under the platform limit.
    m_socketName = QStringLiteral("vap-qmp-%1-%2")
                       .arg(QCoreApplication::applicationPid())
                       .arg(++m_counter);
    QLocalServer::removeServer(m_socketName);

    m_server = new QLocalServer(this);
    QVERIFY(m_server->listen(m_socketName));

    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        m_peer = m_server->nextPendingConnection();
    });
}

void TestQmpClient::cleanup()
{
    if (m_peer) {
        m_peer->disconnect(this);
        m_peer->abort();
        m_peer->deleteLater();
        m_peer = nullptr;
    }
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    QLocalServer::removeServer(m_socketName);
}

void TestQmpClient::waitForPeer()
{
    // Give both the client's connect and the server's accept a chance to run.
    QTRY_VERIFY_WITH_TIMEOUT(m_peer != nullptr, 5000);
    QCOMPARE(m_peer->state(), QLocalSocket::ConnectedState);
}

void TestQmpClient::writeLine(QLocalSocket *peer, const QJsonObject &obj)
{
    QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    payload.append('\n');
    peer->write(payload);
    peer->flush();
}

void TestQmpClient::serveGreeting(QLocalSocket *peer)
{
    QJsonObject version;
    version[QStringLiteral("qemu")] = QJsonObject {
        { QStringLiteral("major"), 11 },
        { QStringLiteral("minor"), 1 },
        { QStringLiteral("micro"), 90 },
    };

    QJsonObject qmp;
    qmp[QStringLiteral("version")] = version;
    qmp[QStringLiteral("capabilities")] = QJsonArray { QStringLiteral("oob") };

    QJsonObject greeting;
    greeting[QStringLiteral("QMP")] = qmp;
    writeLine(peer, greeting);
}

void TestQmpClient::handshakeReachesReadyState()
{
    QmpClient client;
    QSignalSpy connectedSpy(&client, &QmpClient::connected);

    client.connectToServer(m_socketName);

    // Wait for the server side to accept, then send the greeting.
    waitForPeer();
    serveGreeting(m_peer);

    // The client should reply with qmp_capabilities; answer it.
    QTRY_VERIFY_WITH_TIMEOUT(m_peer->bytesAvailable() > 0, 5000);
    QByteArray request = m_peer->readAll();
    QVERIFY(request.contains("qmp_capabilities"));

    QJsonObject caps;
    caps[QStringLiteral("return")] = QJsonObject {};
    writeLine(m_peer, caps);

    QTRY_COMPARE(connectedSpy.count(), 1);
    QCOMPARE(client.state(), QmpClient::State::Ready);
    QVERIFY(client.isReady());
}

void TestQmpClient::commandReplyIsDelivered()
{
    QmpClient client;
    QSignalSpy replySpy(&client, &QmpClient::replyReceived);

    client.connectToServer(m_socketName);
    waitForPeer();
    serveGreeting(m_peer);
    QTRY_VERIFY_WITH_TIMEOUT(m_peer->bytesAvailable() > 0, 5000);
    m_peer->readAll();
    writeLine(m_peer, QJsonObject { { QStringLiteral("return"), QJsonObject {} } });
    QTRY_VERIFY(client.isReady());

    const int id = client.sendCommand(QStringLiteral("query-status"));
    QVERIFY(id > 0);

    QTRY_VERIFY_WITH_TIMEOUT(m_peer->bytesAvailable() > 0, 5000);
    const QByteArray request = m_peer->readAll();
    QVERIFY(request.contains("query-status"));

    QJsonObject status;
    status[QStringLiteral("status")] = QStringLiteral("running");
    status[QStringLiteral("running")] = true;
    QJsonObject reply;
    reply[QStringLiteral("return")] = status;
    reply[QStringLiteral("id")] = id;
    writeLine(m_peer, reply);

    QTRY_COMPARE(replySpy.count(), 1);
    const QList<QVariant> arguments = replySpy.takeFirst();
    QCOMPARE(arguments.at(0).toInt(), id);
    QCOMPARE(arguments.at(1).toJsonObject()
                 .value(QStringLiteral("status")).toString(),
             QStringLiteral("running"));
}

void TestQmpClient::eventIsSurfaced()
{
    QmpClient client;
    QSignalSpy eventSpy(&client, &QmpClient::eventReceived);

    client.connectToServer(m_socketName);
    waitForPeer();
    serveGreeting(m_peer);
    QTRY_VERIFY_WITH_TIMEOUT(m_peer->bytesAvailable() > 0, 5000);
    m_peer->readAll();
    writeLine(m_peer, QJsonObject { { QStringLiteral("return"), QJsonObject {} } });
    QTRY_VERIFY(client.isReady());

    QJsonObject event;
    event[QStringLiteral("event")] = QStringLiteral("RESET");
    event[QStringLiteral("data")] = QJsonObject {
        { QStringLiteral("guest"), true },
    };
    writeLine(m_peer, event);

    QTRY_COMPARE(eventSpy.count(), 1);
    const QList<QVariant> arguments = eventSpy.takeFirst();
    QCOMPARE(arguments.at(0).toString(), QStringLiteral("RESET"));
}

void TestQmpClient::errorReplyEmitsError()
{
    QmpClient client;
    QSignalSpy errorSpy(&client, &QmpClient::errorOccurred);

    client.connectToServer(m_socketName);
    waitForPeer();
    serveGreeting(m_peer);
    QTRY_VERIFY_WITH_TIMEOUT(m_peer->bytesAvailable() > 0, 5000);
    m_peer->readAll();
    writeLine(m_peer, QJsonObject { { QStringLiteral("return"), QJsonObject {} } });
    QTRY_VERIFY(client.isReady());

    const int id = client.sendCommand(QStringLiteral("bogus-command"));
    QVERIFY(id > 0);
    QTRY_VERIFY_WITH_TIMEOUT(m_peer->bytesAvailable() > 0, 5000);
    m_peer->readAll();

    QJsonObject error;
    error[QStringLiteral("class")] = QStringLiteral("CommandNotFound");
    error[QStringLiteral("desc")] = QStringLiteral("no such command");
    QJsonObject reply;
    reply[QStringLiteral("error")] = error;
    reply[QStringLiteral("id")] = id;
    writeLine(m_peer, reply);

    QTRY_VERIFY(errorSpy.count() >= 1);
    QVERIFY(errorSpy.takeFirst().at(0).toString().contains(QStringLiteral("no such command")));
}

void TestQmpClient::sendKeyUsesKeyValueUnion()
{
    QmpClient client;
    client.connectToServer(m_socketName);
    waitForPeer();
    serveGreeting(m_peer);
    QTRY_VERIFY_WITH_TIMEOUT(m_peer->bytesAvailable() > 0, 5000);
    m_peer->readAll();
    writeLine(m_peer, QJsonObject { { QStringLiteral("return"), QJsonObject {} } });
    QTRY_VERIFY(client.isReady());

    QVERIFY(client.sendKey(QStringLiteral("volumeup"), true) > 0);
    QTRY_VERIFY_WITH_TIMEOUT(m_peer->bytesAvailable() > 0, 5000);

    const QJsonObject request =
        QJsonDocument::fromJson(m_peer->readAll().trimmed()).object();
    QCOMPARE(request.value(QStringLiteral("execute")).toString(),
             QStringLiteral("send-key"));

    // keys[0] must be a KeyValue union member, not a bare string.
    const QJsonArray keys =
        request.value(QStringLiteral("arguments")).toObject()
            .value(QStringLiteral("keys")).toArray();
    QCOMPARE(keys.size(), 1);
    const QJsonObject key = keys.at(0).toObject();
    QCOMPARE(key.value(QStringLiteral("type")).toString(), QStringLiteral("qcode"));
    QCOMPARE(key.value(QStringLiteral("data")).toString(), QStringLiteral("volumeup"));
}

QTEST_MAIN(TestQmpClient)
#include "test_qmp_client.moc"