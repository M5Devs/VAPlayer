/*
 * VAPlayer - EmulatorRunner argument construction tests
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

#include <QTest>

class TestEmulatorRunner : public QObject
{
    Q_OBJECT

private slots:
    void ramAndCpuAppearOnTheCommandLine()
    {
        EmulatorConfig config;
        config.ramMb = 4096;
        config.cpuCores = 6;

        EmulatorRunner runner;
        runner.setConfig(config);
        const QStringList args = runner.buildArguments();

        const int ramIndex = args.indexOf(QStringLiteral("-m"));
        QVERIFY(ramIndex >= 0);
        QCOMPARE(args.value(ramIndex + 1), QStringLiteral("4096"));

        const int smpIndex = args.indexOf(QStringLiteral("-smp"));
        QVERIFY(smpIndex >= 0);
        QCOMPARE(args.value(smpIndex + 1), QStringLiteral("6"));
    }

    void virtioDevicesAreRequestedWhenEnabled()
    {
        EmulatorConfig config;
        config.enableGpu = true;
        config.enableSound = true;
        config.enableNetwork = true;
        config.enableTablet = true;

        EmulatorRunner runner;
        runner.setConfig(config);
        const QStringList args = runner.buildArguments();

        QVERIFY(args.contains(QStringLiteral("virtio-gpu-pci")));
        QVERIFY(args.contains(QStringLiteral("virtio-tablet-pci")));

        // The sound device must carry an audiodev reference.
        QVERIFY(args.contains(QStringLiteral("virtio-sound-pci,audiodev=snd0")));
        QVERIFY(args.contains(QStringLiteral("-audiodev")));

        // The NIC must reference its netdev.
        QVERIFY(args.contains(QStringLiteral("virtio-net-pci,netdev=n0")));
    }

    void virtioDevicesAreOmittedWhenDisabled()
    {
        EmulatorConfig config;
        config.enableGpu = false;
        config.enableSound = false;
        config.enableNetwork = false;
        config.enableTablet = false;

        EmulatorRunner runner;
        runner.setConfig(config);
        const QStringList args = runner.buildArguments();

        QVERIFY(!args.contains(QStringLiteral("virtio-gpu-pci")));
        QVERIFY(!args.contains(QStringLiteral("virtio-sound-pci,audiodev=snd0")));
        QVERIFY(!args.contains(QStringLiteral("virtio-net-pci,netdev=n0")));
        QVERIFY(!args.contains(QStringLiteral("virtio-tablet-pci")));
    }

    void diskImageIsAttachedAsVirtioBlock()
    {
        EmulatorConfig config;
        config.diskImage = QStringLiteral("/tmp/android.img");

        EmulatorRunner runner;
        runner.setConfig(config);
        const QStringList args = runner.buildArguments();

        QVERIFY(args.contains(QStringLiteral("file=/tmp/android.img,if=virtio,format=raw")));
    }

    void qmpEndpointIsConfigured()
    {
        EmulatorConfig config;
        EmulatorRunner runner;
        runner.setConfig(config);
        const QStringList args = runner.buildArguments();

        // Before start() the endpoint is empty, so only the option and its
        // position are checked here; the concrete endpoint is verified at
        // start time.
        const int qmpIndex = args.indexOf(QStringLiteral("-qmp"));
        QVERIFY(qmpIndex >= 0);
        QVERIFY(qmpIndex + 1 < args.size());
        QVERIFY(args.value(qmpIndex + 1).startsWith(QStringLiteral("unix:"))
                || args.value(qmpIndex + 1).startsWith(QStringLiteral("pipe:")));
    }

    void ramIsClampedToASaneMinimum()
    {
        EmulatorConfig config;
        config.ramMb = 1; // Below the supported floor.
        config.cpuCores = 0;

        EmulatorRunner runner;
        runner.setConfig(config);
        const QStringList args = runner.buildArguments();

        const int ramIndex = args.indexOf(QStringLiteral("-m"));
        QVERIFY(args.value(ramIndex + 1).toInt() >= 128);

        const int smpIndex = args.indexOf(QStringLiteral("-smp"));
        QVERIFY(args.value(smpIndex + 1).toInt() >= 1);
    }
};

QTEST_MAIN(TestEmulatorRunner)
#include "test_emulator_runner.moc"
