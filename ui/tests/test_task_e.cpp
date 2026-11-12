/*
 * VAPlayer - drag & drop, ADB and dialog tests
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

#include "tools/AdbClient.h"
#include "ui/DropHelper.h"
#include "ui/KeymapperDialog.h"
#include "ui/SharedFolderDialog.h"
#include "ui/ViewportWidget.h"

#include "keymapper/KeymapperEngine.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

/**
 * Covers the Task E plumbing that does not need a running guest: drop-file
 * classification, the ADB command builders, the viewport's drop signals and
 * the two configuration dialogs.
 */
class TestTaskE : public QObject
{
    Q_OBJECT

private slots:
    void classifyDropRoutesByExtension();
    void dropDestinationRootsUnderDownloads();

    void adbCommandBuilders();
    void adbBaseArgumentsHonourSerial();

    void viewportEmitsFilesDropped();
    void viewportIgnoresNonFileDrops();
    void viewportForwardsMouseOnlyWithAnEngine();

    void sharedFolderDialogRoundTripsSettings();
    void sharedFolderGuestPaths();

    void keymapperDialogRoundTripsProfile();
    void keymapperDialogEmitsArmedKey();
};

void TestTaskE::classifyDropRoutesByExtension()
{
    QCOMPARE(classifyDrop(QStringLiteral("app.apk")), DropAction::InstallApk);
    QCOMPARE(classifyDrop(QStringLiteral("update.ZIP")), DropAction::FlashZip);
    QCOMPARE(classifyDrop(QStringLiteral("movie.mp4")), DropAction::PushFile);
    QCOMPARE(classifyDrop(QStringLiteral("song.mp3")), DropAction::PushFile);
    QCOMPARE(classifyDrop(QStringLiteral("noextension")), DropAction::Ignore);
    QCOMPARE(dropActionLabel(DropAction::InstallApk), QStringLiteral("install"));
}

void TestTaskE::dropDestinationRootsUnderDownloads()
{
    // Only the base name is used, so a path with directories is flattened.
    QCOMPARE(dropDestination(QStringLiteral("/tmp/a/b/pic.png")),
             QStringLiteral("/sdcard/Download/pic.png"));
}

void TestTaskE::adbCommandBuilders()
{
    QCOMPARE(AdbClient::installArguments(QStringLiteral("/tmp/x.apk")),
             QStringList({ QStringLiteral("install"), QStringLiteral("-r"),
                           QStringLiteral("/tmp/x.apk") }));

    QCOMPARE(AdbClient::pushArguments(QStringLiteral("/tmp/x"), QStringLiteral("/sdcard/x")),
             QStringList({ QStringLiteral("push"), QStringLiteral("/tmp/x"),
                           QStringLiteral("/sdcard/x") }));

    QCOMPARE(AdbClient::keyEventArguments(4),
             QStringList({ QStringLiteral("shell"), QStringLiteral("input"),
                           QStringLiteral("keyevent"), QStringLiteral("4") }));

    QCOMPARE(AdbClient::screencapArguments(),
             QStringList({ QStringLiteral("exec-out"), QStringLiteral("screencap"),
                           QStringLiteral("-p") }));

    QCOMPARE(AdbClient::shellArguments(QStringLiteral("getprop")),
             QStringList({ QStringLiteral("shell"), QStringLiteral("getprop") }));
}

void TestTaskE::adbBaseArgumentsHonourSerial()
{
    // An empty serial selects the only attached device (no -s prefix).
    QVERIFY(AdbClient::baseArguments().isEmpty());
    QCOMPARE(AdbClient::baseArguments(QStringLiteral("emulator-5554")),
             QStringList({ QStringLiteral("-s"), QStringLiteral("emulator-5554") }));

    const QStringList args =
        AdbClient::installArguments(QStringLiteral("/tmp/x.apk"),
                                    QStringLiteral("emulator-5554"));
    QCOMPARE(args.first(), QStringLiteral("-s"));
    QCOMPARE(args.at(1), QStringLiteral("emulator-5554"));
    QVERIFY(args.contains(QStringLiteral("install")));
}

void TestTaskE::viewportEmitsFilesDropped()
{
    ViewportWidget viewport;
    viewport.resize(400, 800);
    QSignalSpy droppedSpy(&viewport, &ViewportWidget::filesDropped);

    auto *mime = new QMimeData;
    mime->setUrls({ QUrl::fromLocalFile(QStringLiteral("/tmp/game.apk")) });

    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, mime,
                          Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&viewport, &enter);
    QVERIFY(enter.isAccepted());

    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, mime,
                    Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&viewport, &drop);

    QCOMPARE(droppedSpy.count(), 1);
    QCOMPARE(droppedSpy.first().at(0).toStringList(),
             QStringList({ QStringLiteral("/tmp/game.apk") }));
}

void TestTaskE::viewportIgnoresNonFileDrops()
{
    ViewportWidget viewport;
    QSignalSpy droppedSpy(&viewport, &ViewportWidget::filesDropped);

    // A plain-text drag carries no file URLs, so it must be refused.
    auto *mime = new QMimeData;
    mime->setText(QStringLiteral("just some text"));

    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, mime,
                          Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&viewport, &enter);
    QVERIFY(!enter.isAccepted());
    QCOMPARE(droppedSpy.count(), 0);
}

void TestTaskE::viewportForwardsMouseOnlyWithAnEngine()
{
    ViewportWidget viewport;
    viewport.resize(400, 800);

    QSignalSpy pressedSpy(&viewport, &ViewportWidget::mousePressed);
    QSignalSpy movedSpy(&viewport, &ViewportWidget::mouseMoved);

    // Without an engine the viewport stays inert.
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&viewport, &press);
    QCOMPARE(pressedSpy.count(), 0);

    // Attaching an engine enables forwarding.
    KeymapperEngine engine;
    viewport.setKeymapperEngine(&engine);
    QCOMPARE(viewport.keymapperEngine(), &engine);

    QCoreApplication::sendEvent(&viewport, &press);
    QCOMPARE(pressedSpy.count(), 1);
    QCOMPARE(pressedSpy.first().at(0).toString(), QStringLiteral("left"));

    // Movement is only emitted while a button is held.
    QMouseEvent move(QEvent::MouseMove, QPointF(30, 25), QPointF(30, 25),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&viewport, &move);
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(movedSpy.first().at(0).toInt(), 20);
    QCOMPARE(movedSpy.first().at(1).toInt(), 15);

    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(30, 25), QPointF(30, 25),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&viewport, &release);

    movedSpy.clear();
    QCoreApplication::sendEvent(&viewport, &move);
    QCOMPARE(movedSpy.count(), 0);
}

void TestTaskE::sharedFolderDialogRoundTripsSettings()
{
    SharedFolderDialog dialog;
    dialog.setFolderPath(QStringLiteral("/home/user/shared"));
    QCOMPARE(dialog.folderPath(), QStringLiteral("/home/user/shared"));

    dialog.setVirtfsEnabled(false);
    QVERIFY(!dialog.virtfsEnabled());
    dialog.setVirtfsEnabled(true);
    QVERIFY(dialog.virtfsEnabled());

    dialog.setAdbSyncEnabled(true);
    QVERIFY(dialog.adbSyncEnabled());
}

void TestTaskE::sharedFolderGuestPaths()
{
    QCOMPARE(SharedFolderDialog::guestMountPoint(),
             QStringLiteral("/mnt/vaplayer"));
    QCOMPARE(SharedFolderDialog::guestSyncTarget(),
             QStringLiteral("/sdcard/Download/Shared"));
    QVERIFY(SharedFolderDialog::defaultFolderPath().endsWith(
        QStringLiteral("shared")));
}

void TestTaskE::keymapperDialogRoundTripsProfile()
{
    Keymap map;
    map.name = QStringLiteral("Test profile");
    map.dpad.enabled = true;
    map.dpad.up = QStringLiteral("w");
    map.crosshair.enabled = true;
    map.crosshair.toggleKey = QStringLiteral("grave");
    map.crosshair.sensitivity = 175;

    KeymapperDialog dialog;
    dialog.setKeymap(map);
    QCOMPARE(dialog.keymap().name, QStringLiteral("Test profile"));
    QCOMPARE(dialog.keymap().dpad.up, QStringLiteral("w"));
    QCOMPARE(dialog.keymap().crosshair.sensitivity, 175);
}

void TestTaskE::keymapperDialogEmitsArmedKey()
{
    KeymapperDialog dialog;
    QSignalSpy armSpy(&dialog, &KeymapperDialog::armKeyChanged);
    // The default combo starts on "(none)", which is a valid empty arm state.
    QVERIFY(armSpy.isValid());
    QCOMPARE(dialog.armedKey(), QString());
}

QTEST_MAIN(TestTaskE)
#include "test_task_e.moc"
