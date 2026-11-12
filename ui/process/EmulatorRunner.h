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

#ifndef VAPLAYER_EMULATORRUNNER_H
#define VAPLAYER_EMULATORRUNNER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

/**
 * Describes a single emulator invocation.  The values map directly onto the
 * command line passed to vaplayer-core.
 */
struct EmulatorConfig
{
    int ramMb = 2048;               ///< Guest RAM in megabytes.
    int cpuCores = 4;               ///< Number of virtual CPUs.

    QString emulatorPath;           ///< Overrides automatic discovery when set.
    QString diskImage;              ///< Raw disk image backing the guest.
    QString kernel;                 ///< Optional kernel image (direct boot).
    QString initrd;                 ///< Optional initial ramdisk.
    QStringList extraArguments;     ///< Passed through verbatim.

    bool enableGpu = true;          ///< virtio-gpu-pci
    bool enableSound = true;        ///< virtio-sound-pci
    bool enableNetwork = true;      ///< virtio-net-pci
    bool enableTablet = true;       ///< virtio-tablet-pci

    bool headless = true;           ///< No host display window from the core.
    bool enableAdbForward = true;   ///< Forward guest 5555 to host 5555.

    /** Android device debugging bridge port exposed on the host. */
    quint16 adbPort = 5555;

    bool enableMultitouch = true;   ///< virtio-multitouch-pci (touch screen).
    bool enableKeyboard = true;     ///< virtio-keyboard-pci (keymapper input).

    /**
     * Host directory exported to the guest over VirtFS (``-virtfs``).  The
     * guest mounts it as a 9p filesystem; VAPlayer uses it as the shared
     * folder that files dropped on the host appear in.
     */
    QString sharedFolderPath;
    QString sharedFolderTag = QStringLiteral("vaplayer");
};

/**
 * Launches and supervises a vaplayer-core process.
 *
 * The runner owns the child process, forwards its console output as logLine()
 * signals and exposes the QMP endpoint so the UI can drive the guest.
 */
class EmulatorRunner : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Stopped,
        Starting,
        Running,
        Stopping,
    };
    Q_ENUM(State)

    explicit EmulatorRunner(QObject *parent = nullptr);
    ~EmulatorRunner() override;

    void setConfig(const EmulatorConfig &config);
    EmulatorConfig config() const { return m_config; }

    /** Locate the vaplayer-core binary, honouring config.emulatorPath. */
    static QString resolveEmulatorPath(const QString &overridePath = QString());

    /** Path (or Windows pipe name) of the QMP control channel. */
    QString qmpEndpoint() const { return m_qmpEndpoint; }

    /**
     * QEMU "-qmp" argument value for the current endpoint.  Windows uses a
     * named pipe, other platforms a UNIX domain socket.
     */
    QString qmpEndpointArgument() const;

    State state() const { return m_state; }
    bool isRunning() const;

    /** Build the argument list for the current configuration (testable). */
    QStringList buildArguments() const;

    /**
     * True when the core advertises the user-mode ("slirp") network backend,
     * which is required for host port forwarding.  Probed lazily on first use
     * and cached.
     */
    bool supportsUserNetdev() const;

    /**
     * True when the core supports the given ``-device`` model (e.g.
     * "virtio-9p-pci", "virtio-multitouch-pci").  Probed lazily via
     * ``-device help`` and cached per model name.
     */
    bool supportsDevice(const QString &model) const;

    /** Start the emulator.  Returns false if the binary cannot be located. */
    bool start();

    /**
     * Request a graceful shutdown: QMP quit is issued by the caller through the
     * QmpClient; this method terminates the process, escalating to a kill if it
     * does not exit in time.
     */
    void stop(int gracePeriodMs = 3000);

    /** Immediately kill the process, without a graceful request. */
    void kill();

signals:
    void stateChanged(EmulatorRunner::State state);
    void started();
    void stopped();
    void errorOccurred(const QString &message);
    void logLine(const QString &line);

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, int exitStatus);
    void onProcessError();
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();

private:
    void setState(State state);
    void drainChannel(class QProcess *process, bool standardError);

    QProcess *m_process = nullptr;
    EmulatorConfig m_config;
    State m_state = State::Stopped;
    QString m_qmpEndpoint;

    mutable bool m_userNetdevProbed = false;
    mutable bool m_hasUserNetdev = false;

    mutable bool m_devicesProbed = false;
    mutable QStringList m_availableDevices;
};

#endif // VAPLAYER_EMULATORRUNNER_H
