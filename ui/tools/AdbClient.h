/*
 * VAPlayer - ADB helper
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

#ifndef VAPLAYER_ADBCLIENT_H
#define VAPLAYER_ADBCLIENT_H

#include <QObject>
#include <QString>
#include <QStringList>

class ToolRunner;
class QProcess;

/**
 * Thin wrapper around the Android Debug Bridge.
 *
 * The command builders are static so the exact ``adb`` invocation can be
 * asserted in tests without running a device.  Running commands is delegated
 * to a ToolRunner, which streams output and reports completion.
 */
class AdbClient : public QObject
{
    Q_OBJECT

public:
    explicit AdbClient(QObject *parent = nullptr);

    /** Path to the adb binary (``$VAPLAYER_ADB`` overrides PATH lookup). */
    static QString adbPath();

    /** True when an adb binary could be located. */
    static bool isAvailable();

    // -- Command builders (pure, directly testable) ------------------------

    /** ``adb -s <serial>`` prefix, or just ``adb`` when serial is empty. */
    static QStringList baseArguments(const QString &serial = QString());

    /** ``adb install -r <apk>`` */
    static QStringList installArguments(const QString &apkPath,
                                        const QString &serial = QString());

    /** ``adb push <local> <remote>`` */
    static QStringList pushArguments(const QString &localPath,
                                     const QString &remotePath,
                                     const QString &serial = QString());

    /** ``adb shell input keyevent <code>`` */
    static QStringList keyEventArguments(int androidKeyCode,
                                         const QString &serial = QString());

    /** ``adb exec-out screencap -p`` (PNG on stdout). */
    static QStringList screencapArguments(const QString &serial = QString());

    /** ``adb shell <command>`` */
    static QStringList shellArguments(const QString &command,
                                      const QString &serial = QString());

    // -- Execution ---------------------------------------------------------

    /** The runner that executes adb commands; its signals are re-emitted. */
    ToolRunner *runner() const { return m_runner; }

    /** Set the target device serial; empty selects the only attached device. */
    void setSerial(const QString &serial);
    QString serial() const { return m_serial; }

    /** Run ``adb install -r`` on *apkPath*. */
    bool installApk(const QString &apkPath);
    /** Push a single file to *remotePath*. */
    bool pushFile(const QString &localPath, const QString &remotePath);
    /** Send an Android key code (e.g. 4 for Back). */
    bool sendKeyEvent(int androidKeyCode);
    /** Capture a PNG screenshot to *localPath* via exec-out. */
    bool captureScreenshot(const QString &localPath);
    /** True while an adb command is in flight. */
    bool isBusy() const;

signals:
    void outputLine(const QString &line);
    void finished(bool success, int exitCode);
    void failed(const QString &message);

private:
    bool run(const QStringList &arguments);

    ToolRunner *m_runner = nullptr;
    QProcess *m_screencap = nullptr;
    QString m_screencapPath;
    QString m_serial;
};

#endif // VAPLAYER_ADBCLIENT_H
