/*
 * VAPlayer - end-to-end core bridge integration test
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
#include "process/EmulatorRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

/**
 * Drives the real EmulatorRunner and QmpClient against an actual vaplayer-core
 * binary.  The test is skipped when the binary is not present, so a UI-only
 * checkout still passes.
 */
class TestCoreIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void handshakeAndStatusRoundTrip();
    void controlCommandsAreAccepted();
    void coreAdvertisesVirtio9p();
    void sharedFolderDeviceIsInstantiated();

private:
    QString m_corePath;
};

void TestCoreIntegration::initTestCase()
{
    // CI passes an explicit path to a staged core.  An in-tree build has no
    // such variable, so fall back to the location the core is built in.
    m_corePath = qEnvironmentVariable("VAPLAYER_CORE");
    if (m_corePath.isEmpty()) {
#ifdef VAPLAYER_DEFAULT_CORE
        const QString candidate = QStringLiteral(VAPLAYER_DEFAULT_CORE);
        if (QFileInfo::exists(candidate))
            m_corePath = QFileInfo(candidate).canonicalFilePath();
#endif
    }

    // No core was requested anywhere: a UI-only checkout, so skipping is
    // correct.  If a path was given but does not work, fail rather than skip,
    // otherwise a broken build passes CI while testing nothing.
    if (m_corePath.isEmpty())
        QSKIP("vaplayer-core not found; set VAPLAYER_CORE to run this test");
    QVERIFY2(QFileInfo(m_corePath).isExecutable(),
             qPrintable(QStringLiteral("vaplayer-core is not executable: %1")
                            .arg(m_corePath)));
}

void TestCoreIntegration::handshakeAndStatusRoundTrip()
{
    EmulatorConfig config;
    config.headless = true;
    config.enableSound = false;      // keep the guest device set minimal
    config.enableNetwork = false;
    config.enableGpu = false;
    config.enableTablet = false;
    config.emulatorPath = m_corePath;

    EmulatorRunner runner;
    runner.setConfig(config);

    QSignalSpy stateSpy(&runner, &EmulatorRunner::stateChanged);
    QVERIFY(runner.start());
    QTRY_VERIFY_WITH_TIMEOUT(stateSpy.count() >= 1, 10000);

    const QString endpoint = runner.qmpEndpoint();
    QVERIFY(!endpoint.isEmpty());

    QmpClient client;
    QSignalSpy connectedSpy(&client, &QmpClient::connected);
    QSignalSpy errorSpy(&client, &QmpClient::errorOccurred);

    // The socket appears a moment after the process starts.
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(endpoint), 10000);
    client.connectToServer(endpoint);

    QTRY_VERIFY_WITH_TIMEOUT(client.isReady(), 10000);
    QCOMPARE(connectedSpy.count(), 1);

    QSignalSpy replySpy(&client, &QmpClient::replyReceived);
    QVERIFY(client.sendCommand(QStringLiteral("query-status")) > 0);
    QTRY_VERIFY_WITH_TIMEOUT(replySpy.count() >= 1, 10000);

    const QJsonObject status = replySpy.takeFirst().at(1).toJsonObject();
    QVERIFY(status.contains(QStringLiteral("status")));

    client.disconnectFromServer();
    runner.stop(5000);
    QTRY_VERIFY_WITH_TIMEOUT(!runner.isRunning(), 10000);
}

void TestCoreIntegration::controlCommandsAreAccepted()
{
    EmulatorConfig config;
    config.headless = true;
    config.enableSound = false;
    config.enableNetwork = false;
    config.enableGpu = false;
    config.enableTablet = false;
    config.emulatorPath = m_corePath;

    EmulatorRunner runner;
    runner.setConfig(config);
    QVERIFY(runner.start());

    QmpClient client;
    QSignalSpy errorSpy(&client, &QmpClient::errorOccurred);
    const QString endpoint = runner.qmpEndpoint();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(endpoint), 10000);
    client.connectToServer(endpoint);
    QTRY_VERIFY_WITH_TIMEOUT(client.isReady(), 10000);

    // Every command the sidebar can issue must be accepted by the core.
    // replyReceived carries the value of the "return" member, so an accepted
    // command yields an (often empty) object and a rejection is reported
    // through errorOccurred instead.
    QSignalSpy replySpy(&client, &QmpClient::replyReceived);

    QVERIFY(client.stop() > 0);
    QTRY_VERIFY_WITH_TIMEOUT(replySpy.count() >= 1, 10000);
    replySpy.takeFirst();

    QVERIFY(client.cont() > 0);
    QTRY_VERIFY_WITH_TIMEOUT(replySpy.count() >= 1, 10000);
    replySpy.takeFirst();

    // A malformed KeyValue union would be rejected with an error reply.
    QVERIFY(client.sendKey(QStringLiteral("volumeup"), true) > 0);
    QTRY_VERIFY_WITH_TIMEOUT(replySpy.count() >= 1, 10000);
    replySpy.takeFirst();

    QVERIFY2(errorSpy.isEmpty(),
             qPrintable(errorSpy.isEmpty()
                            ? QString()
                            : errorSpy.first().at(0).toString()));

    client.disconnectFromServer();
    runner.stop(5000);
}

/**
 * The whole shared-folder feature depends on the core advertising
 * virtio-9p-pci, so assert it against the real binary rather than a stub.
 */
void TestCoreIntegration::coreAdvertisesVirtio9p()
{
    EmulatorRunner runner;
    QVERIFY2(runner.supportsDevice(QStringLiteral("virtio-9p-pci")),
             "vaplayer-core was built without VirtFS: rebuild core/ with "
             "--enable-virtfs");
}

/**
 * Starts the core exactly as the UI does for a shared folder and confirms
 * through QMP that the 9p device really reached the machine, not merely that
 * the command line was well formed.
 */
void TestCoreIntegration::sharedFolderDeviceIsInstantiated()
{
    QTemporaryDir shareDir;
    QVERIFY(shareDir.isValid());
    const QString hostFile = shareDir.filePath(QStringLiteral("marker.txt"));
    {
        QFile marker(hostFile);
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.write("vaplayer-9p-marker");
    }

    EmulatorConfig config;
    config.headless = true;
    config.enableSound = false;
    config.enableNetwork = false;
    config.enableGpu = false;
    config.enableTablet = false;
    config.emulatorPath = m_corePath;
    config.sharedFolderPath = shareDir.path();
    config.sharedFolderTag = QStringLiteral("vaplayer_share");

    EmulatorRunner runner;
    runner.setConfig(config);
    QVERIFY(runner.start());

    QmpClient client;
    const QString endpoint = runner.qmpEndpoint();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(endpoint), 10000);
    client.connectToServer(endpoint);
    QTRY_VERIFY_WITH_TIMEOUT(client.isReady(), 10000);

    // "info qtree" is served over HMP; its output names every instantiated
    // device, so it proves the 9p controller and its backend are live.
    QSignalSpy replySpy(&client, &QmpClient::replyReceived);
    QJsonObject args;
    args.insert(QStringLiteral("command-line"), QStringLiteral("info qtree"));
    QVERIFY(client.sendCommand(QStringLiteral("human-monitor-command"), args) > 0);
    QTRY_VERIFY_WITH_TIMEOUT(replySpy.count() >= 1, 10000);

    const QString tree = replySpy.takeFirst().at(1).toJsonValue().toString();
    QVERIFY2(tree.contains(QStringLiteral("virtio-9p-pci")),
             qPrintable(QStringLiteral("qtree had no 9p device:\n") + tree));
    QVERIFY2(tree.contains(QStringLiteral("fsdev = \"vaplayer_fs\"")),
             qPrintable(QStringLiteral("qtree had no 9p fsdev:\n") + tree));

    client.disconnectFromServer();
    runner.stop(5000);
    QTRY_VERIFY_WITH_TIMEOUT(!runner.isRunning(), 10000);
}

QTEST_MAIN(TestCoreIntegration)
#include "test_core_integration.moc"
