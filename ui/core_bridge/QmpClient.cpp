/*
 * VAPlayer - QMP (QEMU Machine Protocol) client
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

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLocalSocket>
#include <QTcpSocket>

namespace {
// QMP uses a JSON "execute"/"arguments" envelope.  Keep the wire keys in one
// place so the encoder and the tests cannot drift apart.
constexpr char kKeyExecute[] = "execute";
constexpr char kKeyArguments[] = "arguments";
constexpr char kKeyId[] = "id";
constexpr char kKeyReturn[] = "return";
constexpr char kKeyError[] = "error";
constexpr char kKeyEvent[] = "event";
constexpr char kKeyData[] = "data";

/** Absolute pointer move to *x*,*y* (0..0x7fff). */
QJsonObject absMoveEvent(const char *axis, int value)
{
    return QJsonObject {
        { QStringLiteral("type"), QStringLiteral("abs") },
        { QStringLiteral("data"),
          QJsonObject {
              { QStringLiteral("axis"), QString::fromLatin1(axis) },
              { QStringLiteral("value"), qBound(0, value, 0x7fff) },
          } },
    };
}

/** Pointer button press/release for *button*. */
QJsonObject buttonEvent(const QString &button, bool down)
{
    return QJsonObject {
        { QStringLiteral("type"), QStringLiteral("btn") },
        { QStringLiteral("data"),
          QJsonObject {
              { QStringLiteral("button"), button },
              { QStringLiteral("down"), down },
          } },
    };
}
} // namespace

QmpClient::QmpClient(QObject *parent)
    : QObject(parent)
{
}

QmpClient::~QmpClient()
{
    // Destroying the sockets here is safe: the event loop is gone, so no
    // further signals are delivered.
    delete m_localSocket;
    delete m_tcpSocket;
}

QIODevice *QmpClient::device() const
{
    if (m_transport == Transport::TcpSocket)
        return m_tcpSocket;
    return m_localSocket;
}

void QmpClient::connectToServer(const QString &endpoint)
{
    disconnectFromServer();

    m_transport = Transport::LocalSocket;
    m_localSocket = new QLocalSocket(this);

    connect(m_localSocket, &QLocalSocket::connected,
            this, &QmpClient::onConnected);
    connect(m_localSocket, &QLocalSocket::readyRead,
            this, &QmpClient::onReadyRead);
    connect(m_localSocket, &QLocalSocket::disconnected,
            this, &QmpClient::onDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(m_localSocket, &QLocalSocket::errorOccurred,
            this, &QmpClient::onSocketError);
#else
    connect(m_localSocket,
            QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::error),
            this, &QmpClient::onSocketError);
#endif

    m_state = State::Connecting;
    m_buffer.clear();
    m_localSocket->connectToServer(endpoint);
}

void QmpClient::connectToHost(const QString &host, quint16 port)
{
    disconnectFromServer();

    m_transport = Transport::TcpSocket;
    m_tcpSocket = new QTcpSocket(this);

    connect(m_tcpSocket, &QTcpSocket::connected,
            this, &QmpClient::onConnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead,
            this, &QmpClient::onReadyRead);
    connect(m_tcpSocket, &QTcpSocket::disconnected,
            this, &QmpClient::onDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(m_tcpSocket, &QTcpSocket::errorOccurred,
            this, &QmpClient::onSocketError);
#else
    connect(m_tcpSocket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &QmpClient::onSocketError);
#endif

    m_state = State::Connecting;
    m_buffer.clear();
    m_tcpSocket->connectToHost(host, port);
}

void QmpClient::disconnectFromServer()
{
    if (m_localSocket) {
        m_localSocket->disconnect(this);
        m_localSocket->abort();
        m_localSocket->deleteLater();
        m_localSocket = nullptr;
    }
    if (m_tcpSocket) {
        m_tcpSocket->disconnect(this);
        m_tcpSocket->abort();
        m_tcpSocket->deleteLater();
        m_tcpSocket = nullptr;
    }

    m_buffer.clear();
    if (m_state != State::Disconnected) {
        m_state = State::Disconnected;
        emit disconnected();
    }
}

void QmpClient::onConnected()
{
    // The server greeting arrives asynchronously; wait for it before the
    // capabilities handshake.
    m_state = State::Handshaking;
}

void QmpClient::onDisconnected()
{
    m_buffer.clear();
    m_state = State::Disconnected;
    emit disconnected();
}

void QmpClient::onSocketError()
{
    QIODevice *dev = device();
    QString message;
    if (auto *local = qobject_cast<QLocalSocket *>(dev))
        message = local->errorString();
    else if (auto *tcp = qobject_cast<QTcpSocket *>(dev))
        message = tcp->errorString();

    if (message.isEmpty())
        message = tr("QMP transport error");

    fail(message);
}

void QmpClient::onReadyRead()
{
    QIODevice *dev = device();
    if (!dev)
        return;

    m_buffer.append(dev->readAll());

    // QMP messages are newline-delimited JSON objects.
    int newline;
    while ((newline = m_buffer.indexOf('\n')) != -1) {
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (line.isEmpty())
            continue;

        QJsonParseError parseError {};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            fail(tr("Malformed QMP message: %1").arg(parseError.errorString()));
            continue;
        }
        processDocument(doc);
    }
}

void QmpClient::processDocument(const QJsonDocument &doc)
{
    if (!doc.isObject())
        return;

    const QJsonObject obj = doc.object();

    if (obj.contains(kKeyEvent)) {
        processEvent(obj);
        return;
    }

    if (obj.contains(kKeyReturn) || obj.contains(kKeyError)) {
        processReply(obj);
        return;
    }

    // The server greeting has a "QMP" key and the version information.
    if (obj.contains(QStringLiteral("QMP"))) {
        // Complete the handshake by enabling command capabilities.
        m_state = State::Handshaking;
        QJsonObject caps;
        caps[QString::fromLatin1(kKeyExecute)] =
            QStringLiteral("qmp_capabilities");
        sendRaw(caps);
        return;
    }
}

void QmpClient::processReply(const QJsonObject &obj)
{
    const int id = obj.value(QString::fromLatin1(kKeyId)).toInt(-1);

    if (obj.contains(kKeyError)) {
        const QJsonObject err = obj.value(QString::fromLatin1(kKeyError)).toObject();
        const QString desc = err.value(QStringLiteral("desc")).toString();
        emit errorOccurred(desc.isEmpty()
                               ? tr("QMP command failed")
                               : desc);
        return;
    }

    // QMP allows any JSON type for "return" (for example
    // human-monitor-command returns a string), so the value is forwarded
    // unchanged rather than coerced to an object.
    const QJsonValue result = obj.value(QString::fromLatin1(kKeyReturn));

    // The capabilities handshake reply has no command id; it flips the client
    // into the ready state and is not surfaced as an ordinary reply.
    if (m_state == State::Handshaking) {
        m_state = State::Ready;
        emit connected();
        return;
    }

    emit replyReceived(id, result);
}

void QmpClient::processEvent(const QJsonObject &obj)
{
    const QString event = obj.value(QString::fromLatin1(kKeyEvent)).toString();
    const QJsonObject data =
        obj.value(QString::fromLatin1(kKeyData)).toObject();
    emit eventReceived(event, data);
}

void QmpClient::sendRaw(const QJsonObject &obj)
{
    QIODevice *dev = device();
    if (!dev)
        return;

    QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    payload.append('\n');
    dev->write(payload);

    // QIODevice has no flush(); both concrete socket types do.
    if (auto *local = qobject_cast<QLocalSocket *>(dev))
        local->flush();
    else if (auto *tcp = qobject_cast<QTcpSocket *>(dev))
        tcp->flush();
}

int QmpClient::sendCommand(const QString &command, const QJsonObject &arguments)
{
    if (m_state != State::Ready)
        return -1;

    const int id = m_nextId++;

    QJsonObject obj;
    obj[QString::fromLatin1(kKeyExecute)] = command;
    if (!arguments.isEmpty())
        obj[QString::fromLatin1(kKeyArguments)] = arguments;
    obj[QString::fromLatin1(kKeyId)] = id;

    sendRaw(obj);
    return id;
}

int QmpClient::quit()
{
    return sendCommand(QStringLiteral("quit"));
}

int QmpClient::stop()
{
    return sendCommand(QStringLiteral("stop"));
}

int QmpClient::cont()
{
    return sendCommand(QStringLiteral("cont"));
}

int QmpClient::reset()
{
    return sendCommand(QStringLiteral("system_reset"));
}

int QmpClient::systemPowerdown()
{
    return sendCommand(QStringLiteral("system_powerdown"));
}

int QmpClient::sendKey(const QString &qcode, bool down)
{
    // QMP models each key as a KeyValue union member; the "qcode" variant
    // carries the key name in its "data" field.
    QJsonObject key;
    key[QStringLiteral("type")] = QStringLiteral("qcode");
    key[QStringLiteral("data")] = qcode;

    QJsonObject args;
    args[QStringLiteral("keys")] = QJsonArray { key };
    // A non-zero hold time makes QEMU synthesise a complete press/release
    // pair; zero leaves the key held until the next call.
    args[QStringLiteral("hold-time")] = down ? 100 : 0;
    return sendCommand(QStringLiteral("send-key"), args);
}

int QmpClient::androidKeyCodeFor(const QString &action)
{
    const QString key = action.trimmed().toLower();
    if (key == QLatin1String("back"))
        return 4;
    if (key == QLatin1String("home"))
        return 3;
    if (key == QLatin1String("recents") || key == QLatin1String("recent")
        || key == QLatin1String("app_switch") || key == QLatin1String("appswitch")
        || key == QLatin1String("overview"))
        return 187;
    return -1;
}

int QmpClient::sendAndroidKey(int androidKeyCode)
{
    // Map the Android key onto the qcode the guest's input HAL understands.
    const char *qcode = nullptr;
    switch (androidKeyCode) {
    case 4:   qcode = "esc";   break;  // KEYCODE_BACK
    case 3:   qcode = "home";  break;  // KEYCODE_HOME
    case 187: qcode = "sysrq"; break;  // KEYCODE_APP_SWITCH
    default:  return -1;
    }

    const int downId = sendKey(QString::fromLatin1(qcode), true);
    if (downId < 0)
        return -1;
    sendKey(QString::fromLatin1(qcode), false);
    return 2;
}

int QmpClient::sendTap(int x, int y)
{
    QJsonObject args;
    args[QStringLiteral("events")] = QJsonArray {
        absMoveEvent("x", x),
        absMoveEvent("y", y),
        buttonEvent(QStringLiteral("touch"), true),
        buttonEvent(QStringLiteral("touch"), false),
    };
    if (sendCommand(QStringLiteral("input-send-event"), args) < 0)
        return -1;
    return 4;
}

int QmpClient::sendTouchDown(int x, int y)
{
    QJsonObject args;
    args[QStringLiteral("events")] = QJsonArray {
        absMoveEvent("x", x),
        absMoveEvent("y", y),
        buttonEvent(QStringLiteral("touch"), true),
    };
    if (sendCommand(QStringLiteral("input-send-event"), args) < 0)
        return -1;
    return 3;
}

int QmpClient::sendTouchMove(int x, int y)
{
    QJsonObject args;
    args[QStringLiteral("events")] = QJsonArray {
        absMoveEvent("x", x),
        absMoveEvent("y", y),
    };
    if (sendCommand(QStringLiteral("input-send-event"), args) < 0)
        return -1;
    return 2;
}

int QmpClient::sendTouchUp()
{
    QJsonObject args;
    args[QStringLiteral("events")] = QJsonArray {
        buttonEvent(QStringLiteral("touch"), false),
    };
    if (sendCommand(QStringLiteral("input-send-event"), args) < 0)
        return -1;
    return 1;
}

int QmpClient::sendDrag(int x1, int y1, int x2, int y2)
{
    QJsonObject args;
    args[QStringLiteral("events")] = QJsonArray {
        absMoveEvent("x", x1),
        absMoveEvent("y", y1),
        buttonEvent(QStringLiteral("touch"), true),
        absMoveEvent("x", x2),
        absMoveEvent("y", y2),
        buttonEvent(QStringLiteral("touch"), false),
    };
    return sendCommand(QStringLiteral("input-send-event"), args);
}

int QmpClient::sendMouseDelta(int dx, int dy)
{
    const QJsonObject relX = QJsonObject {
        { QStringLiteral("type"), QStringLiteral("rel") },
        { QStringLiteral("data"),
          QJsonObject {
              { QStringLiteral("axis"), QStringLiteral("x") },
              { QStringLiteral("value"), dx },
          } },
    };
    const QJsonObject relY = QJsonObject {
        { QStringLiteral("type"), QStringLiteral("rel") },
        { QStringLiteral("data"),
          QJsonObject {
              { QStringLiteral("axis"), QStringLiteral("y") },
              { QStringLiteral("value"), dy },
          } },
    };

    QJsonObject args;
    args[QStringLiteral("events")] = QJsonArray { relX, relY };
    return sendCommand(QStringLiteral("input-send-event"), args);
}

int QmpClient::sendMouseButton(const QString &button, bool down)
{
    QJsonObject args;
    args[QStringLiteral("events")] = QJsonArray { buttonEvent(button, down) };
    return sendCommand(QStringLiteral("input-send-event"), args);
}

void QmpClient::fail(const QString &message)
{
    emit errorOccurred(message);
    if (m_state != State::Disconnected) {
        m_state = State::Disconnected;
        emit disconnected();
    }
}
