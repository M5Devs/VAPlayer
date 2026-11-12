/*
 * VAPlayer - emulator process manager
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

#include "process/EmulatorRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QRegularExpression>

namespace {

#ifdef Q_OS_WIN
constexpr char kExecutableName[] = "vaplayer-core.exe";
#else
constexpr char kExecutableName[] = "vaplayer-core";
#endif

/** Directory holding the QMP socket; short paths keep UNIX sockets valid. */
QString runtimeDirectory()
{
    const QString temp = QStandardPaths::writableLocation(
        QStandardPaths::RuntimeLocation);
    if (!temp.isEmpty() && QDir().mkpath(temp))
        return temp;
    return QDir::tempPath();
}

} // namespace

EmulatorRunner::EmulatorRunner(QObject *parent)
    : QObject(parent)
{
}

EmulatorRunner::~EmulatorRunner()
{
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(2000);
        }
    }
}

void EmulatorRunner::setConfig(const EmulatorConfig &config)
{
    m_config = config;
}

QString EmulatorRunner::resolveEmulatorPath(const QString &overridePath)
{
    if (!overridePath.isEmpty())
        return overridePath;

    // The core is built next to the UI source tree, i.e. ../core/build/
    // relative to the ui/ directory.  Search a few plausible locations so a
    // developer build and an installed layout both work.
    const QString appDir = QCoreApplication::applicationDirPath();

    const QStringList candidates = {
        QDir(appDir).absoluteFilePath(kExecutableName),
        QDir(appDir).absoluteFilePath(QStringLiteral("../core/build/") + kExecutableName),
        QDir(appDir).absoluteFilePath(QStringLiteral("../../core/build/") + kExecutableName),
        QDir(appDir).absoluteFilePath(QStringLiteral("../../../core/build/") + kExecutableName),
        QDir::current().absoluteFilePath(QStringLiteral("../core/build/") + kExecutableName),
        QDir::current().absoluteFilePath(QStringLiteral("core/build/") + kExecutableName),
    };

    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }

    // Fall back to PATH so an installed vaplayer-core is found.
    const QString onPath = QStandardPaths::findExecutable(kExecutableName);
    if (!onPath.isEmpty())
        return onPath;

    return QString();
}

bool EmulatorRunner::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

QString EmulatorRunner::qmpEndpointArgument() const
{
#ifdef Q_OS_WIN
    // QEMU expects a pipe name of the form \\.\pipe\<name>.
    return QStringLiteral("pipe:%1,server=on,wait=off").arg(m_qmpEndpoint);
#else
    return QStringLiteral("unix:%1,server=on,wait=off").arg(m_qmpEndpoint);
#endif
}

bool EmulatorRunner::supportsUserNetdev() const
{
    if (m_userNetdevProbed)
        return m_hasUserNetdev;

    m_userNetdevProbed = true;
    m_hasUserNetdev = false;

    const QString program = resolveEmulatorPath(m_config.emulatorPath);
    if (program.isEmpty())
        return false;

    // "-netdev help" lists the compiled-in backends and exits immediately.
    QProcess probe;
    probe.start(program, { QStringLiteral("-netdev"), QStringLiteral("help") });
    if (!probe.waitForFinished(5000)) {
        probe.kill();
        return false;
    }

    const QString output = QString::fromLocal8Bit(probe.readAllStandardOutput())
                           + QString::fromLocal8Bit(probe.readAllStandardError());
    // Match the backend name as a whole word on its own line.
    m_hasUserNetdev = output.contains(QRegularExpression(
        QStringLiteral("(^|\\s)user(\\s|$)")));
    return m_hasUserNetdev;
}

QStringList EmulatorRunner::buildArguments() const
{
    QStringList args;

    // Machine and CPU.
    args << QStringLiteral("-M") << QStringLiteral("q35")
         << QStringLiteral("-accel") << QStringLiteral("tcg")
         << QStringLiteral("-smp") << QString::number(qMax(1, m_config.cpuCores))
         << QStringLiteral("-m") << QString::number(qMax(128, m_config.ramMb));

    if (m_config.headless)
        args << QStringLiteral("-display") << QStringLiteral("none");
    else
        args << QStringLiteral("-display") << QStringLiteral("gtk");

    args << QStringLiteral("-no-reboot");

    // Optional direct kernel boot.
    if (!m_config.kernel.isEmpty())
        args << QStringLiteral("-kernel") << m_config.kernel;
    if (!m_config.initrd.isEmpty())
        args << QStringLiteral("-initrd") << m_config.initrd;

    // Storage.
    if (!m_config.diskImage.isEmpty())
        args << QStringLiteral("-drive")
             << QStringLiteral("file=%1,if=virtio,format=raw").arg(m_config.diskImage);

    // Sound needs an explicit backend; the "none" driver keeps the guest
    // device functional without touching host audio hardware.
    if (m_config.enableSound)
        args << QStringLiteral("-audiodev") << QStringLiteral("none,id=snd0");

    // VirtIO devices.
    if (m_config.enableGpu)
        args << QStringLiteral("-device") << QStringLiteral("virtio-gpu-pci");
    if (m_config.enableSound)
        args << QStringLiteral("-device") << QStringLiteral("virtio-sound-pci,audiodev=snd0");
    if (m_config.enableTablet)
        args << QStringLiteral("-device") << QStringLiteral("virtio-tablet-pci");
    if (m_config.enableNetwork) {
        args << QStringLiteral("-device")
             << QStringLiteral("virtio-net-pci,netdev=n0");
        if (m_config.enableAdbForward && supportsUserNetdev()) {
            // User-mode networking is the only backend that can forward a
            // guest port to the host; it is present when the core was built
            // with libslirp.
            args << QStringLiteral("-netdev")
                 << QStringLiteral("user,id=n0,hostfwd=tcp::%1-:%1")
                        .arg(m_config.adbPort);
        } else {
            // Fall back to a point-to-point socket backend so the NIC still
            // has a valid netdev to attach to.
            args << QStringLiteral("-netdev")
                 << QStringLiteral("socket,id=n0,listen=:0");
        }
    }

    // Control channel.
    args << QStringLiteral("-qmp") << qmpEndpointArgument();

    args << QStringLiteral("-serial") << QStringLiteral("none");

    if (!m_config.extraArguments.isEmpty())
        args << m_config.extraArguments;

    return args;
}

bool EmulatorRunner::start()
{
    if (isRunning()) {
        emit errorOccurred(tr("The emulator is already running."));
        return false;
    }

    const QString program = resolveEmulatorPath(m_config.emulatorPath);
    if (program.isEmpty()) {
        emit errorOccurred(tr("Could not find the vaplayer-core binary. "
                              "Build core/ first or set its path in Settings."));
        return false;
    }

    // A fresh, unique endpoint per run avoids colliding with a stale socket.
#ifdef Q_OS_WIN
    m_qmpEndpoint = QStringLiteral("vaplayer-qmp-%1")
                        .arg(QCoreApplication::applicationPid());
#else
    m_qmpEndpoint = QDir(runtimeDirectory())
                        .absoluteFilePath(QStringLiteral("vaplayer-qmp-%1.sock")
                                              .arg(QCoreApplication::applicationPid()));
    QFile::remove(m_qmpEndpoint);
#endif

    if (!m_process) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
        connect(m_process, &QProcess::started,
                this, &EmulatorRunner::onProcessStarted);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        connect(m_process, &QProcess::finished,
                this, &EmulatorRunner::onProcessFinished);
#else
        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &EmulatorRunner::onProcessFinished);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        connect(m_process, &QProcess::errorOccurred,
                this, &EmulatorRunner::onProcessError);
#else
        connect(m_process,
                QOverload<QProcess::ProcessError>::of(&QProcess::error),
                this, &EmulatorRunner::onProcessError);
#endif
        connect(m_process, &QProcess::readyReadStandardOutput,
                this, &EmulatorRunner::onReadyReadStandardOutput);
        connect(m_process, &QProcess::readyReadStandardError,
                this, &EmulatorRunner::onReadyReadStandardError);
    }

    setState(State::Starting);
    emit logLine(tr("Launching: %1 %2")
                     .arg(program, buildArguments().join(QLatin1Char(' '))));
    m_process->start(program, buildArguments());
    return true;
}

void EmulatorRunner::stop(int gracePeriodMs)
{
    if (!isRunning()) {
        setState(State::Stopped);
        return;
    }

    setState(State::Stopping);
    m_process->terminate();

    // Escalate to a hard kill if the process does not exit in time.
    QTimer::singleShot(gracePeriodMs, this, [this]() {
        if (isRunning()) {
            emit logLine(tr("Emulator did not exit in time; killing it."));
            m_process->kill();
        }
    });
}

void EmulatorRunner::kill()
{
    if (isRunning()) {
        setState(State::Stopping);
        m_process->kill();
    }
}

void EmulatorRunner::onProcessStarted()
{
    setState(State::Running);
    emit started();
}

void EmulatorRunner::onProcessFinished(int exitCode, int exitStatus)
{
    Q_UNUSED(exitStatus)
    emit logLine(tr("Emulator exited with code %1.").arg(exitCode));
    setState(State::Stopped);
    emit stopped();
}

void EmulatorRunner::onProcessError()
{
    emit errorOccurred(tr("Failed to start the emulator: %1")
                           .arg(m_process->errorString()));
    setState(State::Stopped);
}

void EmulatorRunner::onReadyReadStandardOutput()
{
    drainChannel(m_process, false);
}

void EmulatorRunner::onReadyReadStandardError()
{
    drainChannel(m_process, true);
}

void EmulatorRunner::drainChannel(QProcess *process, bool standardError)
{
    if (!process)
        return;

    const QByteArray chunk = standardError ? process->readAllStandardError()
                                           : process->readAllStandardOutput();
    if (chunk.isEmpty())
        return;

    const QString text = QString::fromLocal8Bit(chunk);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]")),
                                         Qt::SkipEmptyParts);
    for (const QString &line : lines)
        emit logLine(line);
}

void EmulatorRunner::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(m_state);
}
