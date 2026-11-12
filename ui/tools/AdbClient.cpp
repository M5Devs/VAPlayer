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

#include "tools/AdbClient.h"
#include "tools/ToolRunner.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {
constexpr char kAdbEnv[] = "VAPLAYER_ADB";
} // namespace

AdbClient::AdbClient(QObject *parent)
    : QObject(parent)
    , m_runner(new ToolRunner(this))
{
    connect(m_runner, &ToolRunner::outputLine,
            this, &AdbClient::outputLine);
    connect(m_runner, &ToolRunner::finished,
            this, &AdbClient::finished);
    connect(m_runner, &ToolRunner::failed,
            this, &AdbClient::failed);
}

QString AdbClient::adbPath()
{
    const QString override = qEnvironmentVariable(kAdbEnv);
    if (!override.isEmpty()) {
        const QFileInfo info(override);
        if (info.isFile())
            return info.absoluteFilePath();
    }
#ifdef Q_OS_WIN
    return QStandardPaths::findExecutable(QStringLiteral("adb.exe"));
#else
    return QStandardPaths::findExecutable(QStringLiteral("adb"));
#endif
}

bool AdbClient::isAvailable()
{
    return !adbPath().isEmpty();
}

QStringList AdbClient::baseArguments(const QString &serial)
{
    if (serial.isEmpty())
        return QStringList();
    return QStringList { QStringLiteral("-s"), serial };
}

QStringList AdbClient::installArguments(const QString &apkPath,
                                        const QString &serial)
{
    QStringList args = baseArguments(serial);
    args << QStringLiteral("install") << QStringLiteral("-r") << apkPath;
    return args;
}

QStringList AdbClient::pushArguments(const QString &localPath,
                                     const QString &remotePath,
                                     const QString &serial)
{
    QStringList args = baseArguments(serial);
    args << QStringLiteral("push") << localPath << remotePath;
    return args;
}

QStringList AdbClient::keyEventArguments(int androidKeyCode,
                                         const QString &serial)
{
    QStringList args = baseArguments(serial);
    args << QStringLiteral("shell") << QStringLiteral("input")
         << QStringLiteral("keyevent") << QString::number(androidKeyCode);
    return args;
}

QStringList AdbClient::screencapArguments(const QString &serial)
{
    QStringList args = baseArguments(serial);
    args << QStringLiteral("exec-out") << QStringLiteral("screencap")
         << QStringLiteral("-p");
    return args;
}

QStringList AdbClient::shellArguments(const QString &command,
                                      const QString &serial)
{
    QStringList args = baseArguments(serial);
    args << QStringLiteral("shell") << command;
    return args;
}

void AdbClient::setSerial(const QString &serial)
{
    m_serial = serial;
}

bool AdbClient::installApk(const QString &apkPath)
{
    if (apkPath.isEmpty())
        return false;
    return run(installArguments(apkPath, m_serial));
}

bool AdbClient::pushFile(const QString &localPath, const QString &remotePath)
{
    if (localPath.isEmpty() || remotePath.isEmpty())
        return false;
    return run(pushArguments(localPath, remotePath, m_serial));
}

bool AdbClient::sendKeyEvent(int androidKeyCode)
{
    return run(keyEventArguments(androidKeyCode, m_serial));
}

bool AdbClient::captureScreenshot(const QString &localPath)
{
    if (localPath.isEmpty())
        return false;

    const QString program = adbPath();
    if (program.isEmpty()) {
        emit failed(tr("adb was not found. Install the Android platform-tools "
                       "or set %1.")
                        .arg(QString::fromLatin1(kAdbEnv)));
        return false;
    }

    // Screencap is binary, so it bypasses the line-oriented ToolRunner and
    // writes straight to the destination file.
    if (!m_screencap) {
        m_screencap = new QProcess(this);
        connect(m_screencap, &QProcess::readyReadStandardOutput, this, [this]() {
            QFile out(m_screencapPath);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Append)) {
                emit failed(out.errorString());
                m_screencap->kill();
                return;
            }
            out.write(m_screencap->readAllStandardOutput());
        });
        connect(m_screencap,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus) {
                    emit finished(exitCode == 0, exitCode);
                });
        connect(m_screencap, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError) {
                    emit failed(m_screencap->errorString());
                });
    }

    // Truncate any previous capture so appends start from a clean file.
    QFile::remove(localPath);
    m_screencapPath = localPath;
    m_screencap->start(program, screencapArguments(m_serial));
    return true;
}

bool AdbClient::isBusy() const
{
    return m_runner && m_runner->isRunning();
}

bool AdbClient::run(const QStringList &arguments)
{
    const QString program = adbPath();
    if (program.isEmpty()) {
        emit failed(tr("adb was not found. Install the Android platform-tools "
                       "or set %1.")
                        .arg(QString::fromLatin1(kAdbEnv)));
        return false;
    }
    return m_runner->startProgram(program, arguments);
}
