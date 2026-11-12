/*
 * VAPlayer - provisioning UI tests
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

#include "tools/ToolRunner.h"
#include "ui/RomManagerDialog.h"
#include "ui/VFlashDialog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

/**
 * Tests for the provisioning layer: the ToolRunner process/parse helpers and
 * the two dialogs that drive them.
 *
 * The runner is exercised against the real ``python3`` interpreter and the
 * real tools under ``../tools``; no part of the toolchain is mocked.  Tests
 * that need the tools skip cleanly when the directory is unavailable.
 */
class TestProvisioning : public QObject
{
    Q_OBJECT

private slots:
    void toolsDirectoryIsDiscovered();
    void resolveToolBuildsRealCommands();
    void parsesKeyValueOutput();
    void parsesProgressPercentages();
    void mapsStageMessagesToProgress();
    void recognisesErrorsAndSuccess();

    void runsARealProcessAndStreamsOutput();
    void reportsNonZeroExitAsFailure();
    void runsTheRealInspectTool();

    void vflashDialogDefaultsAndSignals();
    void vflashDialogRejectsEmptySelection();
    void vflashDialogFlashesRealPackage();
    void romManagerPresetsAndSelection();
    void romManagerRejectsMissingLocalIso();
};

namespace {

/** Run *program* with *args*, returning true when it exits cleanly. */
bool runProcess(const QString &program, const QStringList &args)
{
    QProcess proc;
    proc.start(program, args);
    if (!proc.waitForFinished(120000))
        return false;
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

} // namespace

// ---------------------------------------------------------------------------
// ToolRunner: discovery and parsing
// ---------------------------------------------------------------------------

void TestProvisioning::toolsDirectoryIsDiscovered()
{
    const QString dir = ToolRunner::toolsDirectory();
    QVERIFY2(!dir.isEmpty(),
             "tools/ must be found relative to the build tree");
    QVERIFY(QFileInfo::exists(dir + QStringLiteral("/fetch_image.py")));
    QVERIFY(QFileInfo::exists(dir + QStringLiteral("/inject_arm.py")));
    QVERIFY(QFileInfo::exists(dir + QStringLiteral("/vflash/cli.py")));
}

void TestProvisioning::resolveToolBuildsRealCommands()
{
    const QStringList vflash = ToolRunner::resolveTool(QStringLiteral("vflash"));
    QVERIFY(!vflash.isEmpty());
    QVERIFY(vflash.last().endsWith(QStringLiteral("vflash"))
            || vflash.last().endsWith(QStringLiteral("cli.py")));

    const QStringList fetch = ToolRunner::resolveTool(QStringLiteral("fetch_image"));
    QVERIFY(fetch.size() >= 2);
    QVERIFY(fetch.last().endsWith(QStringLiteral("fetch_image.py")));

    const QStringList inject = ToolRunner::resolveTool(QStringLiteral("inject_arm"));
    QVERIFY(inject.size() >= 2);
    QVERIFY(inject.last().endsWith(QStringLiteral("inject_arm.py")));

    QVERIFY(ToolRunner::resolveTool(QStringLiteral("does_not_exist")).isEmpty());
}

void TestProvisioning::parsesKeyValueOutput()
{
    const QString sample = QStringLiteral(
        "image:            /tmp/system.img\n"
        "container:        raw\n"
        "partition scheme: none\n"
        "filesystem:\n"
        "  label:          system\n"
        "  size:           2420.0 MiB (2537553920 bytes)\n"
        "native bridge:\n"
        "  installed:      False\n");

    const QHash<QString, QString> fields = ToolRunner::parseKeyValues(sample);
    QCOMPARE(fields.value(QStringLiteral("image")), QStringLiteral("/tmp/system.img"));
    QCOMPARE(fields.value(QStringLiteral("container")), QStringLiteral("raw"));
    QCOMPARE(fields.value(QStringLiteral("partition scheme")), QStringLiteral("none"));
    QCOMPARE(fields.value(QStringLiteral("label")), QStringLiteral("system"));
    QCOMPARE(fields.value(QStringLiteral("installed")), QStringLiteral("False"));
    QCOMPARE(ToolRunner::lookup(fields, QStringLiteral("Container")),
             QStringLiteral("raw"));
}

void TestProvisioning::parsesProgressPercentages()
{
    QCOMPARE(ToolRunner::parsePercent(QStringLiteral("  12.34%")), 12);
    QCOMPARE(ToolRunner::parsePercent(QStringLiteral("100.00%")), 100);
    QCOMPARE(ToolRunner::parsePercent(QStringLiteral("done")), -1);
    QCOMPARE(ToolRunner::parsePercent(QStringLiteral("no digits here")), -1);
}

void TestProvisioning::mapsStageMessagesToProgress()
{
    QCOMPARE(ToolRunner::stageProgress(QStringLiteral("Downloading ISO")), 5);
    QVERIFY(ToolRunner::stageProgress(QStringLiteral("flattening qcow2")) == 20);
    QVERIFY(ToolRunner::stageProgress(QStringLiteral("Bridge installed.")) == 100);
    QCOMPARE(ToolRunner::stageProgress(QStringLiteral("unrelated chatter")), -1);
}

void TestProvisioning::recognisesErrorsAndSuccess()
{
    QVERIFY(ToolRunner::looksLikeError(QStringLiteral("error: no such image")));
    QVERIFY(ToolRunner::looksLikeError(QStringLiteral("Traceback (most recent call last):")));
    QVERIFY(!ToolRunner::looksLikeError(QStringLiteral("Extracting 4 files")));

    QVERIFY(ToolRunner::looksLikeSuccess(QStringLiteral("Bridge installed.")));
    QVERIFY(ToolRunner::looksLikeSuccess(QStringLiteral("OK: image is fine")));
    QVERIFY(!ToolRunner::looksLikeSuccess(QStringLiteral("still working")));
}

// ---------------------------------------------------------------------------
// ToolRunner: real process execution
// ---------------------------------------------------------------------------

void TestProvisioning::runsARealProcessAndStreamsOutput()
{
    ToolRunner runner;
    QSignalSpy lineSpy(&runner, &ToolRunner::outputLine);
    QSignalSpy finishSpy(&runner, &ToolRunner::finished);

    // A portable one-liner: no reliance on a shell.
    const QString python = ToolRunner::pythonInterpreter();
    QVERIFY(runner.startProgram(python, { QStringLiteral("-c"),
        QStringLiteral("print('hello'); print('world')") }));

    QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() == 1, 10000);
    QCOMPARE(finishSpy.first().at(0).toBool(), true);
    QCOMPARE(finishSpy.first().at(1).toInt(), 0);
    QCOMPARE(runner.state(), ToolRunner::State::Succeeded);
    QCOMPARE(lineSpy.count(), 2);
    QVERIFY(runner.output().contains(QStringLiteral("hello")));
    QVERIFY(runner.output().contains(QStringLiteral("world")));
}

void TestProvisioning::reportsNonZeroExitAsFailure()
{
    ToolRunner runner;
    QSignalSpy finishSpy(&runner, &ToolRunner::finished);

    const QString python = ToolRunner::pythonInterpreter();
    QVERIFY(runner.startProgram(python, { QStringLiteral("-c"),
        QStringLiteral("import sys; print('error: boom', file=sys.stderr); "
                       "sys.exit(3)") }));

    QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() == 1, 10000);
    QCOMPARE(finishSpy.first().at(0).toBool(), false);
    QCOMPARE(finishSpy.first().at(1).toInt(), 3);
    QCOMPARE(runner.state(), ToolRunner::State::Failed);
    // The most relevant line is surfaced as the error message.
    QVERIFY(runner.errorString().contains(QStringLiteral("boom")));
}

void TestProvisioning::runsTheRealInspectTool()
{
    // Produce a tiny raw ext4 image with mke2fs so `vflash --inspect` has
    // something real to read; skip if the host lacks the tools.
    if (QStandardPaths::findExecutable(QStringLiteral("mke2fs")).isEmpty())
        QSKIP("mke2fs is required to build a test image");

    const QString tmp = QDir::tempPath() + QStringLiteral("/vap-test-inspect.img");
    QFile::remove(tmp);

    // 32 MiB file filled with zeroes.
    {
        QFile image(tmp);
        QVERIFY(image.open(QIODevice::WriteOnly));
        QVERIFY(image.resize(32 * 1024 * 1024));
    }

    QProcess mkfs;
    mkfs.start(QStringLiteral("mke2fs"),
               { QStringLiteral("-q"), QStringLiteral("-t"),
                 QStringLiteral("ext4"), QStringLiteral("-L"),
                 QStringLiteral("system"), QStringLiteral("-F"), tmp });
    QVERIFY(mkfs.waitForFinished(20000));
    QCOMPARE(mkfs.exitCode(), 0);

    ToolRunner runner;
    QSignalSpy finishSpy(&runner, &ToolRunner::finished);
    QVERIFY(runner.start(QStringLiteral("vflash"),
                         { QStringLiteral("--image"), tmp,
                           QStringLiteral("--inspect") }));

    QTRY_VERIFY_WITH_TIMEOUT(finishSpy.count() == 1, 60000);
    QCOMPARE(finishSpy.first().at(0).toBool(), true);

    const QHash<QString, QString> fields =
        ToolRunner::parseKeyValues(runner.output());
    QCOMPARE(fields.value(QStringLiteral("container")), QStringLiteral("raw"));
    QCOMPARE(fields.value(QStringLiteral("label")), QStringLiteral("system"));

    QFile::remove(tmp);
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

void TestProvisioning::vflashDialogDefaultsAndSignals()
{
    VFlashDialog dialog;
    QVERIFY(dialog.imagePath().isEmpty());

    QSignalSpy imageSpy(&dialog, &VFlashDialog::imageChanged);
    dialog.setDefaultImagePath(QStringLiteral("/tmp/android.qcow2"));
    QCOMPARE(dialog.imagePath(), QStringLiteral("/tmp/android.qcow2"));

    // A second default must not clobber an explicit choice.
    dialog.setDefaultImagePath(QStringLiteral("/tmp/other.qcow2"));
    QCOMPARE(dialog.imagePath(), QStringLiteral("/tmp/android.qcow2"));

    dialog.setPackagePath(QStringLiteral("/tmp/magisk.zip"));
    QCOMPARE(dialog.packagePath(), QStringLiteral("/tmp/magisk.zip"));

    QVERIFY(!dialog.isBusy());
    QVERIFY(!imageSpy.isEmpty());
}

void TestProvisioning::vflashDialogRejectsEmptySelection()
{
    VFlashDialog dialog;
    QSignalSpy finishSpy(dialog.runner(), &ToolRunner::finished);

    // Nothing selected: both actions must refuse to spawn a process.
    dialog.startInspect();
    dialog.startFlash();

    QCOMPARE(finishSpy.count(), 0);
    QVERIFY(!dialog.isBusy());
    QVERIFY(!dialog.runner()->isRunning());
}

void TestProvisioning::vflashDialogFlashesRealPackage()
{
    // Needs mke2fs to build the image and a zip writer to build the package.
    if (QStandardPaths::findExecutable(QStringLiteral("mke2fs")).isEmpty())
        QSKIP("mke2fs is required to build a test image");

    const QString image = QDir::tempPath() + QStringLiteral("/vap-flash-test.img");
    const QString package = QDir::tempPath() + QStringLiteral("/vap-flash-test.zip");
    QFile::remove(image);
    QFile::remove(package);

    {
        QFile file(image);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(32 * 1024 * 1024));
    }
    QVERIFY(runProcess(QStringLiteral("mke2fs"),
                       { QStringLiteral("-q"), QStringLiteral("-t"),
                         QStringLiteral("ext4"), QStringLiteral("-L"),
                         QStringLiteral("system"), QStringLiteral("-F"), image }));

    // Build the flashable ZIP with Python's zipfile (no `zip` binary needed).
    const QString script = QStringLiteral(
        "import zipfile,sys\n"
        "z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED)\n"
        "z.writestr('payload/hello.txt','hello\\n')\n"
        "z.writestr('META-INF/com/google/android/updater-script',"
        "'package_extract_file(\"payload/hello.txt\",\"/system/bin/hello.txt\");\\n')\n"
        "z.close()\n");
    QVERIFY(runProcess(ToolRunner::pythonInterpreter(),
                       { QStringLiteral("-c"), script, package }));
    QVERIFY(QFileInfo::exists(package));

    VFlashDialog dialog;
    dialog.setImagePath(image);
    dialog.setPackagePath(package);

    QSignalSpy completed(&dialog, &VFlashDialog::flashCompleted);
    dialog.startFlash();
    QVERIFY(dialog.isBusy());

    QTRY_VERIFY_WITH_TIMEOUT(completed.count() == 1, 120000);
    QVERIFY(!dialog.isBusy());
    QCOMPARE(completed.first().at(0).toString(), image);
    QVERIFY(dialog.consoleText().contains(QStringLiteral("applied")));

    // Confirm the payload really landed in the filesystem.  vflash maps the
    // image root onto /system, so the file appears at /bin/hello.txt.
    ToolRunner cat;
    QSignalSpy catDone(&cat, &ToolRunner::finished);
    QVERIFY(cat.start(QStringLiteral("vflash"),
                      { QStringLiteral("--image"), image,
                        QStringLiteral("--cat"),
                        QStringLiteral("/bin/hello.txt") }));
    QTRY_VERIFY_WITH_TIMEOUT(catDone.count() == 1, 60000);
    QCOMPARE(catDone.first().at(0).toBool(), true);
    QVERIFY(cat.output().contains(QStringLiteral("hello")));

    QFile::remove(image);
    QFile::remove(package);
}

void TestProvisioning::romManagerPresetsAndSelection()
{
    RomManagerDialog dialog;

    const QList<RomManagerDialog::Preset> presets = RomManagerDialog::presets();
    QVERIFY(presets.size() >= 3);

    // The two required presets must be present.
    QStringList keys;
    for (const RomManagerDialog::Preset &preset : presets)
        keys << preset.key;
    QVERIFY(keys.contains(QStringLiteral("android-x86-9")));
    QVERIFY(keys.contains(QStringLiteral("android-x86-7.1")));

    // At least one entry is a local-ISO option.
    bool hasLocal = false;
    for (const RomManagerDialog::Preset &preset : presets)
        hasLocal = hasLocal || preset.local;
    QVERIFY(hasLocal);

    // Output path defaults to a .qcow2 file.
    QVERIFY(dialog.outputPath().endsWith(QStringLiteral(".qcow2")));

    dialog.selectPreset(QStringLiteral("android-x86-7.1"));
    dialog.setOutputPath(QStringLiteral("/tmp/custom.qcow2"));
    QCOMPARE(dialog.outputPath(), QStringLiteral("/tmp/custom.qcow2"));

    QVERIFY(!dialog.isBusy());
}

void TestProvisioning::romManagerRejectsMissingLocalIso()
{
    RomManagerDialog dialog;
    QSignalSpy finishSpy(dialog.runner(), &ToolRunner::finished);

    // Select the local-ISO preset but point at a non-existent file.
    dialog.selectPreset(QString());
    dialog.setOutputPath(QStringLiteral("/tmp/out.qcow2"));
    dialog.startProvisioning();

    QCOMPARE(finishSpy.count(), 0);
    QVERIFY(!dialog.runner()->isRunning());
}

QTEST_MAIN(TestProvisioning)
#include "test_provisioning.moc"
