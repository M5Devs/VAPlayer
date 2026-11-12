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

#ifndef VAPLAYER_QMPCLIENT_H
#define VAPLAYER_QMPCLIENT_H

#include <QObject>
#include <QJsonObject>
#include <QJsonValue>
#include <QByteArray>
#include <QString>

class QLocalSocket;
class QTcpSocket;
class QJsonDocument;

/**
 * Asynchronous client for the QEMU Machine Protocol.
 *
 * A QMP session starts with a greeting sent by the server, followed by a
 * client-side "qmp_capabilities" handshake.  After that, arbitrary commands
 * may be issued and their replies delivered asynchronously.
 *
 * The transport is a UNIX domain socket (or a Windows named pipe) by default;
 * a TCP socket may be selected instead.  Commands are JSON objects encoded in
 * QMP's newline-delimited form.
 */
class QmpClient : public QObject
{
    Q_OBJECT

public:
    enum class Transport {
        LocalSocket, ///< UNIX domain socket / Windows named pipe.
        TcpSocket,   ///< TCP stream socket.
    };

    enum class State {
        Disconnected,
        Connecting,
        Handshaking,
        Ready,
    };
    Q_ENUM(State)

    explicit QmpClient(QObject *parent = nullptr);
    ~QmpClient() override;

    /** Connect to a UNIX socket path or a Windows named pipe name. */
    void connectToServer(const QString &endpoint);

    /** Connect to a TCP endpoint. */
    void connectToHost(const QString &host, quint16 port);

    /** Close the connection and reset the client to its initial state. */
    void disconnectFromServer();

    State state() const { return m_state; }
    bool isReady() const { return m_state == State::Ready; }

    /**
     * Queue a QMP command.  Arguments may be empty for parameterless commands.
     * Returns a monotonically increasing identifier that is echoed back with
     * the corresponding reply() signal, or -1 if the client is not ready.
     */
    int sendCommand(const QString &command,
                    const QJsonObject &arguments = QJsonObject());

    /** Convenience wrappers for the commands the UI issues directly. */
    int quit();
    int stop();
    int cont();
    int reset();
    int systemPowerdown();

    /** Send "volume up/down" style key events through the input subsystem. */
    int sendKey(const QString &qcode, bool down);

    /**
     * Send an Android navigation key as a complete press/release pair.
     *
     * Android keys are not QEMU qcodes, so each maps onto the qcode whose
     * Linux key code the Android input HAL translates to the requested
     * Android key code:
     *   - Back (4)        -> "esc"
     *   - Home (3)        -> "home"
     *   - Recent apps(187)-> "sysrq"
     *
     * Returns the number of commands queued, or -1 when not connected.
     */
    int sendAndroidKey(int androidKeyCode);

    /**
     * Android key code for a named navigation action ("back", "home",
     * "recents"/"app_switch"), or -1 when the name is unknown.  Static so the
     * mapping can be tested without a connection.
     */
    static int androidKeyCodeFor(const QString &action);

    /**
     * Synthesise a single tap at *x*,*y* in absolute coordinates (0..0x7fff).
     * Sends an absolute move, a touch press and a touch release.
     */
    int sendTap(int x, int y);

    /** Begin a touch contact at *x*,*y* (absolute, 0..0x7fff). */
    int sendTouchDown(int x, int y);

    /** Move the active touch contact to *x*,*y* (absolute, 0..0x7fff). */
    int sendTouchMove(int x, int y);

    /** Lift the active touch contact. */
    int sendTouchUp();

    /**
     * Synthesise a complete touch drag from (*x1*,*y1*) to (*x2*,*y2*).
     * Equivalent to touchDown/sendTouchMove/sendTouchUp in one command.
     */
    int sendDrag(int x1, int y1, int x2, int y2);

    /**
     * Send a relative pointer motion, used by the keymapper's crosshair mode
     * to translate host mouse deltas into camera movement.
     */
    int sendMouseDelta(int dx, int dy);

    /** Press or release a mouse button ("left", "right", "middle"). */
    int sendMouseButton(const QString &button, bool down);

signals:
    /** Emitted once the capability handshake completes successfully. */
    void connected();

    /** Emitted after the socket is closed, voluntarily or otherwise. */
    void disconnected();

    /** Emitted when the transport or protocol fails; message is human readable. */
    void errorOccurred(const QString &message);

    /**
     * Emitted for every command reply.  id matches the value returned by
     * sendCommand(); result holds the "return" object (empty on error).
     */
    void replyReceived(int id, const QJsonObject &result);

    /** Emitted for asynchronous events such as RESET or SHUTDOWN. */
    void eventReceived(const QString &event, const QJsonObject &data);

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError();

private:
    QIODevice *device() const;
    void processDocument(const QJsonDocument &doc);
    void processReply(const QJsonObject &obj);
    void processEvent(const QJsonObject &obj);
    void fail(const QString &message);
    void sendRaw(const QJsonObject &obj);

    QLocalSocket *m_localSocket = nullptr;
    QTcpSocket *m_tcpSocket = nullptr;

    QByteArray m_buffer;
    State m_state = State::Disconnected;
    Transport m_transport = Transport::LocalSocket;
    int m_nextId = 1;
};

#endif // VAPLAYER_QMPCLIENT_H
