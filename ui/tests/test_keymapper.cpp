/*
 * VAPlayer - keymapper tests
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
#include "keymapper/Keymap.h"
#include "keymapper/KeymapperEngine.h"
#include "keymapper/KeymapperOverlay.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

/**
 * A QMP server that records every "input-send-event" / "send-key" the client
 * sends, so the keymapper's output can be asserted exactly.
 */
class RecordingQmpServer : public QObject
{
    Q_OBJECT

public:
    explicit RecordingQmpServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_name = QStringLiteral("vap-km-%1").arg(QCoreApplication::applicationPid());
        QLocalServer::removeServer(m_name);
        m_server = new QLocalServer(this);
        m_server->listen(m_name);
        connect(m_server, &QLocalServer::newConnection, this, [this]() {
            m_peer = m_server->nextPendingConnection();
            connect(m_peer, &QLocalSocket::readyRead, this, &RecordingQmpServer::onRead);
            // QMP sends the greeting unsolicited, before any client request.
            sendGreeting();
        });
    }

    ~RecordingQmpServer() override
    {
        if (m_server)
            m_server->close();
        QLocalServer::removeServer(m_name);
    }

    QString name() const { return m_name; }

    /** Hand the greeting and answer qmp_capabilities to reach Ready. */
    bool handshake(QmpClient &client)
    {
        client.connectToServer(m_name);
        if (!waitFor([this]() { return m_peer != nullptr; }))
            return false;
        if (!waitFor([this]() { return m_greeted; }))
            return false;
        return true;
    }

    QList<QJsonObject> requests;    ///< Every command envelope received.
    QJsonArray lastEvents;          ///< "events" of the last input command.

private:
    void onRead()
    {
        while (m_peer && m_peer->canReadLine()) {
            const QByteArray line = m_peer->readLine().trimmed();
            if (line.isEmpty())
                continue;
            const QJsonObject obj = QJsonDocument::fromJson(line).object();
            const QString execute = obj.value(QStringLiteral("execute")).toString();

            if (execute == QLatin1String("qmp_capabilities")) {
                m_greeted = true;
                write(QJsonObject { { QStringLiteral("return"), QJsonObject {} } });
                continue;
            }

            requests.append(obj);
            if (execute == QLatin1String("input-send-event"))
                lastEvents = obj.value(QStringLiteral("arguments")).toObject()
                                 .value(QStringLiteral("events")).toArray();
            write(QJsonObject {
                { QStringLiteral("return"), QJsonObject {} },
                { QStringLiteral("id"), obj.value(QStringLiteral("id")) },
            });
        }
    }

    void sendGreeting()
    {
        QJsonObject version;
        version[QStringLiteral("qemu")] = QJsonObject {
            { QStringLiteral("major"), 11 }, { QStringLiteral("minor"), 1 },
            { QStringLiteral("micro"), 90 },
        };
        QJsonObject qmp;
        qmp[QStringLiteral("version")] = version;
        qmp[QStringLiteral("capabilities")] = QJsonArray { QStringLiteral("oob") };
        QJsonObject greeting;
        greeting[QStringLiteral("QMP")] = qmp;
        write(greeting);
    }

    void write(const QJsonObject &obj)
    {
        if (!m_peer)
            return;
        QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        payload.append('\n');
        m_peer->write(payload);
        m_peer->flush();
    }

    template <typename Predicate>
    bool waitFor(Predicate done, int timeoutMs = 5000)
    {
        QElapsedTimer timer;
        timer.start();
        while (!done() && timer.elapsed() < timeoutMs)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return done();
    }

    QLocalServer *m_server = nullptr;
    QLocalSocket *m_peer = nullptr;
    QString m_name;
    bool m_greeted = false;
};

/** Count events of a given "type" in an input-send-event payload. */
int countEvents(const QJsonArray &events, const QString &type)
{
    int count = 0;
    for (const QJsonValue &value : events) {
        if (value.toObject().value(QStringLiteral("type")).toString() == type)
            ++count;
    }
    return count;
}

/** First event of *type* whose "data.axis" matches. */
QJsonObject findAxis(const QJsonArray &events, const QString &axis)
{
    for (const QJsonValue &value : events) {
        const QJsonObject event = value.toObject();
        const QJsonObject data = event.value(QStringLiteral("data")).toObject();
        if (event.value(QStringLiteral("type")).toString() == QLatin1String("abs")
            && data.value(QStringLiteral("axis")).toString() == axis)
            return data;
    }
    return QJsonObject();
}

} // namespace

/**
 * Tests the keymapper profile model, the input engine (against a recording
 * QMP server) and the overlay's coordinate maths.
 */
class TestKeymapper : public QObject
{
    Q_OBJECT

private slots:
    void keymapRoundTripsThroughJson();
    void keymapClampsCoordinates();
    void keymapSaveAndLoad();
    void profilePathSanitisesPackage();

    void qcodeForQtKeyMapsLettersAndSpecials();

    void engineSendsTapForBoundKey();
    void engineSendsTouchDragForDpad();
    void engineIgnoresKeysWhenDisabled();
    void engineTogglesCrosshairAndForwardsMotion();
    void engineAndroidKeyMapping();

    void overlayConvertsCoordinates();
    void overlayPlacesAndRemovesTap();
};

void TestKeymapper::keymapRoundTripsThroughJson()
{
    Keymap map;
    map.name = QStringLiteral("Shooter");
    map.packageName = QStringLiteral("com.example.game");

    KeyTap fire;
    fire.key = QStringLiteral("space");
    fire.x = 0x6000;
    fire.y = 0x4000;
    fire.label = QStringLiteral("Fire");
    map.taps.append(fire);

    map.dpad.enabled = true;
    map.dpad.up = QStringLiteral("w");
    map.dpad.down = QStringLiteral("s");
    map.dpad.left = QStringLiteral("a");
    map.dpad.right = QStringLiteral("d");

    map.crosshair.enabled = true;
    map.crosshair.toggleKey = QStringLiteral("mouse-right");
    map.crosshair.sensitivity = 150;

    bool ok = false;
    const Keymap parsed = Keymap::fromJson(map.toJson(), &ok);
    QVERIFY(ok);
    QCOMPARE(parsed, map);
}

void TestKeymapper::keymapClampsCoordinates()
{
    QJsonObject tap;
    tap[QStringLiteral("key")] = QStringLiteral("space");
    tap[QStringLiteral("x")] = 0x9999;      // out of range
    tap[QStringLiteral("y")] = -5;          // out of range

    QJsonObject root;
    root[QStringLiteral("taps")] = QJsonArray { tap };
    const Keymap map = Keymap::fromJson(root);

    QCOMPARE(map.taps.size(), 1);
    QCOMPARE(map.taps.first().x, 0x7fff);
    QCOMPARE(map.taps.first().y, 0);
}

void TestKeymapper::keymapSaveAndLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Keymap map;
    map.name = QStringLiteral("Racing");
    KeyTap steer;
    steer.key = QStringLiteral("left");
    steer.x = 0x2000;
    steer.y = 0x6000;
    map.taps.append(steer);

    const QString path = dir.filePath(QStringLiteral("racing.json"));
    QString error;
    QVERIFY2(map.save(path, &error), qPrintable(error));

    bool ok = false;
    const Keymap loaded = Keymap::load(path, &ok, &error);
    QVERIFY2(ok, qPrintable(error));
    QCOMPARE(loaded, map);
}

void TestKeymapper::profilePathSanitisesPackage()
{
    // A path separator in the package name must not escape the directory.
    const QString path = Keymap::profilePathFor(QStringLiteral("../evil"));
    QVERIFY(!path.contains(QStringLiteral("../")));
    QVERIFY(path.endsWith(QStringLiteral(".json")));
}

void TestKeymapper::qcodeForQtKeyMapsLettersAndSpecials()
{
    QCOMPARE(Keymap::qcodeForQtKey(Qt::Key_A), QStringLiteral("a"));
    QCOMPARE(Keymap::qcodeForQtKey(Qt::Key_Z), QStringLiteral("z"));
    QCOMPARE(Keymap::qcodeForQtKey(Qt::Key_5), QStringLiteral("5"));
    QCOMPARE(Keymap::qcodeForQtKey(Qt::Key_Space), QStringLiteral("space"));
    QCOMPARE(Keymap::qcodeForQtKey(Qt::Key_Escape), QStringLiteral("esc"));
    QCOMPARE(Keymap::qcodeForQtKey(Qt::Key_F4), QStringLiteral("f4"));
    QVERIFY(Keymap::qcodeForQtKey(Qt::Key_Meta).isEmpty());
}

void TestKeymapper::engineSendsTapForBoundKey()
{
    RecordingQmpServer server;
    QmpClient client;
    QVERIFY(server.handshake(client));

    Keymap map;
    KeyTap tap;
    tap.key = QStringLiteral("space");
    tap.x = 0x3000;
    tap.y = 0x5000;
    map.taps.append(tap);

    KeymapperEngine engine;
    engine.setQmpClient(&client);
    engine.setKeymap(map);
    engine.setEnabled(true);

    QVERIFY(engine.handleKeyPress(QStringLiteral("space")));
    QTRY_COMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.first().value(QStringLiteral("execute")).toString(),
             QStringLiteral("input-send-event"));

    // A tap is two absolute moves plus a touch press and release.
    QCOMPARE(countEvents(server.lastEvents, QStringLiteral("abs")), 2);
    QCOMPARE(countEvents(server.lastEvents, QStringLiteral("btn")), 2);
    QCOMPARE(findAxis(server.lastEvents, QStringLiteral("x"))
                 .value(QStringLiteral("value")).toInt(), 0x3000);
}

void TestKeymapper::engineSendsTouchDragForDpad()
{
    RecordingQmpServer server;
    QmpClient client;
    QVERIFY(server.handshake(client));

    Keymap map;
    map.dpad.enabled = true;
    map.dpad.up = QStringLiteral("w");
    map.dpad.down = QStringLiteral("s");
    map.dpad.left = QStringLiteral("a");
    map.dpad.right = QStringLiteral("d");
    map.dpad.centerX = 0x4000;
    map.dpad.centerY = 0x5000;
    map.dpad.radius = 0x1000;

    KeymapperEngine engine;
    engine.setQmpClient(&client);
    engine.setKeymap(map);
    engine.setEnabled(true);

    // Pressing "w" anchors a contact and steers it upward (y decreases).
    QVERIFY(engine.handleKeyPress(QStringLiteral("w")));
    QTRY_VERIFY(server.requests.size() >= 1);

    const QJsonObject upAxis = findAxis(server.lastEvents, QStringLiteral("y"));
    QVERIFY(!upAxis.isEmpty());
    QVERIFY(upAxis.value(QStringLiteral("value")).toInt() < map.dpad.centerY);

    // Releasing the only held direction lifts the contact.
    QVERIFY(engine.handleKeyRelease(QStringLiteral("w")));
    QTRY_VERIFY(countEvents(server.lastEvents, QStringLiteral("btn")) > 0);
    const QJsonObject btn = server.lastEvents.last().toObject()
                                .value(QStringLiteral("data")).toObject();
    QCOMPARE(btn.value(QStringLiteral("down")).toBool(), false);
}

void TestKeymapper::engineIgnoresKeysWhenDisabled()
{
    RecordingQmpServer server;
    QmpClient client;
    QVERIFY(server.handshake(client));

    Keymap map;
    KeyTap tap;
    tap.key = QStringLiteral("space");
    map.taps.append(tap);

    KeymapperEngine engine;
    engine.setQmpClient(&client);
    engine.setKeymap(map);
    engine.setEnabled(false);

    QVERIFY(!engine.handleKeyPress(QStringLiteral("space")));
    QTest::qWait(50);
    QCOMPARE(server.requests.size(), 0);
}

void TestKeymapper::engineTogglesCrosshairAndForwardsMotion()
{
    RecordingQmpServer server;
    QmpClient client;
    QVERIFY(server.handshake(client));

    Keymap map;
    map.crosshair.enabled = true;
    map.crosshair.toggleKey = QStringLiteral("grave");
    map.crosshair.sensitivity = 200;

    KeymapperEngine engine;
    engine.setQmpClient(&client);
    engine.setKeymap(map);
    engine.setEnabled(true);

    QSignalSpy crosshairSpy(&engine, &KeymapperEngine::crosshairChanged);
    QVERIFY(engine.handleKeyPress(QStringLiteral("grave")));
    QCOMPARE(crosshairSpy.count(), 1);
    QVERIFY(engine.isCrosshairActive());

    server.requests.clear();
    engine.handleMouseMove(10, -4);
    QTRY_COMPARE(server.requests.size(), 1);
    // Sensitivity 200% doubles the deltas.
    QCOMPARE(server.lastEvents.size(), 2);
    QCOMPARE(server.lastEvents.at(0).toObject()
                 .value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("value")).toInt(), 20);

    QVERIFY(engine.handleKeyPress(QStringLiteral("grave")));
    QCOMPARE(crosshairSpy.count(), 2);
    QVERIFY(!engine.isCrosshairActive());
}

void TestKeymapper::engineAndroidKeyMapping()
{
    // The static mapping is what the quick-control navigation buttons rely on.
    QCOMPARE(QmpClient::androidKeyCodeFor(QStringLiteral("back")), 4);
    QCOMPARE(QmpClient::androidKeyCodeFor(QStringLiteral("Home")), 3);
    QCOMPARE(QmpClient::androidKeyCodeFor(QStringLiteral("recents")), 187);
    QCOMPARE(QmpClient::androidKeyCodeFor(QStringLiteral("app_switch")), 187);
    QCOMPARE(QmpClient::androidKeyCodeFor(QStringLiteral("bogus")), -1);
}

void TestKeymapper::overlayConvertsCoordinates()
{
    // Centre of the screen maps to the middle of QEMU's 0..0x7fff range.
    QCOMPARE(KeymapperOverlay::toNormalised(0, 100), 0);
    QCOMPARE(KeymapperOverlay::toNormalised(99, 100), 0x7fff);
    // 16383 vs 16384 is one unit of quantisation; allow it.
    QVERIFY(qAbs(KeymapperOverlay::toNormalised(50, 101) - 0x4000) <= 1);
    QCOMPARE(KeymapperOverlay::toPixel(0x7fff, 100), 99);

    // Round-tripping a mid-range value stays within one pixel.
    const int pixel = KeymapperOverlay::toPixel(0x4000, 200);
    QVERIFY(qAbs(KeymapperOverlay::toNormalised(pixel, 200) - 0x4000) <= 200);
}

void TestKeymapper::overlayPlacesAndRemovesTap()
{
    KeymapperOverlay overlay;
    overlay.resize(400, 800);

    Keymap map;
    overlay.setKeymap(map);
    overlay.setMappingMode(true);
    overlay.setArmedKey(QStringLiteral("space"));

    QSignalSpy changedSpy(&overlay, &KeymapperOverlay::keymapChanged);

    // A press with an armed key drops a tap point at the click position.
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(200, 400), QPointF(200, 400),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&overlay, &press);

    QCOMPARE(overlay.keymap().taps.size(), 1);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(overlay.keymap().taps.first().key, QStringLiteral("space"));

    // The tap is centred horizontally and below centre vertically.
    const KeyTap tap = overlay.keymap().taps.first();
    QVERIFY(qAbs(tap.x - 0x4000) < 200);
    QVERIFY(qAbs(tap.y - 0x4000) < 200);

    // Deleting the selected tap empties the profile.
    overlay.removeSelectedTap();
    QVERIFY(overlay.keymap().taps.isEmpty());
}

QTEST_MAIN(TestKeymapper)
#include "test_keymapper.moc"
