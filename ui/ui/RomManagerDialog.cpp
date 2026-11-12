/*
 * VAPlayer - ROM manager and downloader dialog
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

#include "ui/RomManagerDialog.h"

#include "tools/ToolRunner.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>

namespace {

constexpr char kStyleSheet[] = R"(
QDialog {
    background-color: #14161a;
    color: #e6e8ec;
    font-family: "Inter", "Segoe UI", "Noto Sans", sans-serif;
    font-size: 13px;
}
#dialogHeader {
    font-size: 17px;
    font-weight: 700;
    color: #f2f4f8;
}
#hintLabel {
    color: #6c7480;
    font-size: 11px;
}
#statusLabel {
    color: #b9bfca;
}
#statusLabel[state="error"] {
    color: #e0736f;
}
#statusLabel[state="ok"] {
    color: #3fb96a;
}
QListWidget {
    background-color: #0f1114;
    border: 1px solid #262a31;
    border-radius: 8px;
    color: #cfd6e0;
    outline: none;
}
QListWidget::item {
    padding: 8px 10px;
    border-bottom: 1px solid #1c2027;
}
QListWidget::item:selected {
    background-color: #23272f;
    color: #f2f4f8;
    border-left: 3px solid #2f7d4f;
}
QLineEdit, QComboBox {
    background-color: #1b1e24;
    border: 1px solid #2f343d;
    border-radius: 6px;
    padding: 6px 8px;
    color: #e6e8ec;
}
QLineEdit:focus, QComboBox:focus {
    border-color: #2f7d4f;
}
QComboBox::drop-down {
    border: none;
    width: 20px;
}
QPushButton {
    background-color: #23272f;
    border: 1px solid #2f343d;
    border-radius: 7px;
    padding: 8px 12px;
    color: #dfe3e9;
}
QPushButton:hover {
    background-color: #2b303a;
    border-color: #3a404b;
}
QPushButton:disabled {
    background-color: #1a1d22;
    color: #565c66;
    border-color: #23272f;
}
#primaryButton {
    background-color: #2f7d4f;
    border: 1px solid #37945c;
    color: #ffffff;
    font-weight: 600;
}
#primaryButton:hover {
    background-color: #37945c;
}
#primaryButton:disabled {
    background-color: #1f3a2b;
    border-color: #27492f;
    color: #6f8b78;
}
QPlainTextEdit {
    background-color: #0f1114;
    border: 1px solid #262a31;
    border-radius: 8px;
    color: #cfd6e0;
    font-family: "JetBrains Mono", "Consolas", "DejaVu Sans Mono", monospace;
    font-size: 12px;
}
QProgressBar {
    background-color: #1b1e24;
    border: 1px solid #262a31;
    border-radius: 7px;
    height: 14px;
    text-align: center;
    color: #cfd6e0;
}
QProgressBar::chunk {
    background-color: #2f7d4f;
    border-radius: 6px;
}
QCheckBox {
    spacing: 8px;
    color: #c4cad3;
}
QCheckBox::indicator {
    width: 15px;
    height: 15px;
    border-radius: 4px;
    border: 1px solid #3a404b;
    background-color: #1b1e24;
}
QCheckBox::indicator:checked {
    background-color: #2f7d4f;
    border-color: #37945c;
}
QGroupBox {
    border: 1px solid #262a31;
    border-radius: 8px;
    margin-top: 14px;
    padding-top: 10px;
    font-weight: 600;
    color: #cfd4dc;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 12px;
    padding: 0 4px;
}
)";

} // namespace

RomManagerDialog::RomManagerDialog(QWidget *parent)
    : QDialog(parent)
    , m_runner(new ToolRunner(this))
{
    buildUi();
    applyTheme();

    connect(m_runner, &ToolRunner::outputLine,
            this, &RomManagerDialog::onRunnerLine);
    connect(m_runner, &ToolRunner::progressChanged,
            this, &RomManagerDialog::onRunnerProgress);
    connect(m_runner, &ToolRunner::finished,
            this, &RomManagerDialog::onRunnerFinished);
    connect(m_runner, &ToolRunner::failed,
            this, &RomManagerDialog::onRunnerFailed);
}

RomManagerDialog::~RomManagerDialog() = default;

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

QList<RomManagerDialog::Preset> RomManagerDialog::presets()
{
    return {
        { QStringLiteral("android-x86-9"),
          QObject::tr("Android-x86 9.0-r2"),
          QObject::tr("Default gaming image — 64-bit, Vulkan-capable"),
          QStringLiteral("Android 9"), false },
        { QStringLiteral("android-x86-7.1"),
          QObject::tr("Android-x86 7.1-r5"),
          QObject::tr("Legacy / low-end — “potato mode” for weak hosts"),
          QStringLiteral("Android 7.1"), false },
        { QStringLiteral("blissos-15"),
          QObject::tr("BlissOS 15.8.6"),
          QObject::tr("Feature-rich 12L base (larger download)"),
          QStringLiteral("Android 12"), false },
        { QString(),
          QObject::tr("Local ISO / image…"),
          QObject::tr("Use an installer ISO you already have"),
          QStringLiteral("local"), true },
    };
}

QString RomManagerDialog::defaultImageDirectory()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QDir(base).absoluteFilePath(QStringLiteral("VAPlayer/images"));
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void RomManagerDialog::buildUi()
{
    setWindowTitle(tr("ROM Manager"));
    setMinimumSize(760, 620);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto *header = new QLabel(tr("ROM Manager"), this);
    header->setObjectName(QStringLiteral("dialogHeader"));
    root->addWidget(header);

    auto *hint = new QLabel(
        tr("Download and prepare a bootable Android image. The finished qcow2 "
           "is loaded into the emulator automatically."), this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *body = new QHBoxLayout();
    body->setSpacing(14);

    // Left column: image list.
    auto *listGroup = new QGroupBox(tr("Base image"), this);
    auto *listLayout = new QVBoxLayout(listGroup);
    m_presetList = new QListWidget(listGroup);
    const QList<Preset> items = presets();
    for (const Preset &preset : items) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n%2").arg(preset.title, preset.subtitle),
            m_presetList);
        item->setData(Qt::UserRole, preset.key);
    }
    m_presetList->setCurrentRow(0);
    listLayout->addWidget(m_presetList, 1);

    auto *localRow = new QHBoxLayout();
    m_localIsoEdit = new QLineEdit(listGroup);
    m_localIsoEdit->setPlaceholderText(tr("No local ISO selected"));
    m_localIsoEdit->setEnabled(false);
    m_browseLocalButton = new QPushButton(tr("Browse…"), listGroup);
    m_browseLocalButton->setEnabled(false);
    localRow->addWidget(m_localIsoEdit, 1);
    localRow->addWidget(m_browseLocalButton);
    listLayout->addLayout(localRow);

    body->addWidget(listGroup, 3);

    // Right column: options.
    auto *optionsGroup = new QGroupBox(tr("Options"), this);
    auto *optionsLayout = new QVBoxLayout(optionsGroup);

    auto *outputRow = new QHBoxLayout();
    outputRow->addWidget(new QLabel(tr("Output:"), optionsGroup));
    m_outputEdit = new QLineEdit(optionsGroup);
    m_outputEdit->setText(
        QDir(defaultImageDirectory()).absoluteFilePath(QStringLiteral("android.qcow2")));
    m_browseOutputButton = new QPushButton(tr("…"), optionsGroup);
    outputRow->addWidget(m_outputEdit, 1);
    outputRow->addWidget(m_browseOutputButton);
    optionsLayout->addLayout(outputRow);

    m_armCheck = new QCheckBox(
        tr("Enable ARM Translation (inject libndk_translation)"), optionsGroup);
    m_armCheck->setChecked(true);
    optionsLayout->addWidget(m_armCheck);

    m_spoofCheck = new QCheckBox(
        tr("Spoof High-End Device Profile (ROG Phone / 90+ FPS)"), optionsGroup);
    m_spoofCheck->setChecked(true);
    optionsLayout->addWidget(m_spoofCheck);

    auto *profileRow = new QHBoxLayout();
    profileRow->addSpacing(23);
    profileRow->addWidget(new QLabel(tr("Profile:"), optionsGroup));
    m_profileCombo = new QComboBox(optionsGroup);
    m_profileCombo->addItem(tr("ASUS ROG Phone 8 (165 Hz)"),
                            QStringLiteral("rog8"));
    m_profileCombo->addItem(tr("Samsung Galaxy S24 Ultra (120 Hz)"),
                            QStringLiteral("s24ultra"));
    m_profileCombo->addItem(tr("Google Pixel 8 Pro (120 Hz)"),
                            QStringLiteral("pixel8pro"));
    profileRow->addWidget(m_profileCombo, 1);
    optionsLayout->addLayout(profileRow);

    m_dataDiskCheck = new QCheckBox(
        tr("Also create a writable /data disk (8 GiB)"), optionsGroup);
    optionsLayout->addWidget(m_dataDiskCheck);
    optionsLayout->addStretch(1);

    body->addWidget(optionsGroup, 4);
    root->addLayout(body, 1);

    // Console.
    auto *consoleLabel = new QLabel(tr("Progress"), this);
    consoleLabel->setObjectName(QStringLiteral("hintLabel"));
    root->addWidget(consoleLabel);

    m_console = new QPlainTextEdit(this);
    m_console->setReadOnly(true);
    m_console->setMinimumHeight(130);
    m_console->setPlaceholderText(tr("Download and preparation output appears here."));
    root->addWidget(m_console);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    root->addWidget(m_progress);

    auto *footer = new QHBoxLayout();
    m_statusLabel = new QLabel(tr("Ready."), this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    footer->addWidget(m_statusLabel, 1);
    m_startButton = new QPushButton(tr("Download & Prepare"), this);
    m_startButton->setObjectName(QStringLiteral("primaryButton"));
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setEnabled(false);
    m_closeButton = new QPushButton(tr("Close"), this);
    footer->addWidget(m_startButton);
    footer->addWidget(m_cancelButton);
    footer->addWidget(m_closeButton);
    root->addLayout(footer);

    connect(m_presetList, &QListWidget::currentRowChanged,
            this, &RomManagerDialog::onPresetChanged);
    connect(m_browseLocalButton, &QPushButton::clicked,
            this, &RomManagerDialog::onBrowseLocal);
    connect(m_browseOutputButton, &QPushButton::clicked,
            this, &RomManagerDialog::onBrowseOutput);
    connect(m_startButton, &QPushButton::clicked,
            this, &RomManagerDialog::startProvisioning);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &RomManagerDialog::cancel);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(m_spoofCheck, &QCheckBox::toggled,
            m_profileCombo, &QComboBox::setEnabled);

    onPresetChanged(0);
}

void RomManagerDialog::applyTheme()
{
    setStyleSheet(QString::fromUtf8(kStyleSheet));
}

// ---------------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------------

QString RomManagerDialog::currentPresetKey() const
{
    if (auto *item = m_presetList->currentItem())
        return item->data(Qt::UserRole).toString();
    return QString();
}

void RomManagerDialog::selectPreset(const QString &key)
{
    for (int row = 0; row < m_presetList->count(); ++row) {
        if (m_presetList->item(row)->data(Qt::UserRole).toString() == key) {
            m_presetList->setCurrentRow(row);
            return;
        }
    }
}

void RomManagerDialog::setOutputPath(const QString &path)
{
    m_outputEdit->setText(path);
}

QString RomManagerDialog::outputPath() const
{
    return m_outputEdit->text().trimmed();
}

QString RomManagerDialog::consoleText() const
{
    return m_console->toPlainText();
}

void RomManagerDialog::onPresetChanged(int index)
{
    if (index < 0)
        return;
    const bool local = currentPresetKey().isEmpty();
    m_localIsoEdit->setEnabled(local);
    m_browseLocalButton->setEnabled(local);
}

void RomManagerDialog::onBrowseLocal()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select an installer ISO"), QDir::homePath(),
        tr("Disk images (*.iso *.img *.qcow2 *.raw);;All files (*)"));
    if (!path.isEmpty())
        m_localIsoEdit->setText(path);
}

void RomManagerDialog::onBrowseOutput()
{
    const QString start = outputPath().isEmpty()
                              ? defaultImageDirectory()
                              : QFileInfo(outputPath()).absolutePath();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save provisioned image as"), start,
        tr("QCOW2 images (*.qcow2);;All files (*)"));
    if (!path.isEmpty())
        m_outputEdit->setText(path);
}

// ---------------------------------------------------------------------------
// Provisioning
// ---------------------------------------------------------------------------

bool RomManagerDialog::validateSelection()
{
    if (outputPath().isEmpty()) {
        setStatus(tr("Choose an output file for the prepared image."), true);
        return false;
    }
    if (currentPresetKey().isEmpty() && m_localIsoEdit->text().trimmed().isEmpty()) {
        setStatus(tr("Select a local ISO file, or pick a preset above."), true);
        return false;
    }
    const QString local = m_localIsoEdit->text().trimmed();
    if (currentPresetKey().isEmpty() && !QFileInfo::exists(local)) {
        setStatus(tr("The selected local ISO does not exist: %1").arg(local), true);
        return false;
    }
    return true;
}

void RomManagerDialog::startProvisioning()
{
    if (m_busy || !validateSelection())
        return;

    // Make sure the destination directory exists before the tool runs.
    const QFileInfo outInfo(outputPath());
    QDir().mkpath(outInfo.absolutePath());

    m_resultPath.clear();
    m_stage = Stage::Fetching;
    setBusy(true);
    setStatus(tr("Downloading and unpacking the base image…"));

    QStringList args{ QStringLiteral("--image"), currentPresetKey(),
                      QStringLiteral("--out"), outputPath() };
    if (currentPresetKey().isEmpty())
        args << QStringLiteral("--iso") << m_localIsoEdit->text().trimmed();
    if (m_dataDiskCheck->isChecked()) {
        args << QStringLiteral("--data-disk")
             << QFileInfo(outputPath()).absolutePath() + QStringLiteral("/data.qcow2")
             << QStringLiteral("--data-size") << QStringLiteral("8G");
    }

    m_runner->start(QStringLiteral("fetch_image"), args);
}

void RomManagerDialog::beginInjection()
{
    m_stage = Stage::Injecting;
    setStatus(tr("Injecting ARM translation bridge…"));

    QStringList args{ QStringLiteral("--image"), outputPath() };
    if (m_spoofCheck->isChecked())
        args << QStringLiteral("--profile") << m_profileCombo->currentData().toString();
    else
        args << QStringLiteral("--no-spoof");

    m_runner->start(QStringLiteral("inject_arm"), args);
}

void RomManagerDialog::cancel()
{
    if (m_busy)
        m_runner->cancel();
}

// ---------------------------------------------------------------------------
// Runner reactions
// ---------------------------------------------------------------------------

void RomManagerDialog::onRunnerLine(const QString &line)
{
    appendConsole(line);

    // fetch_image prints "Provisioned image: <path>"; remember it so a later
    // failure can still point at the file.
    const QString marker = QStringLiteral("provisioned image:");
    const int at = line.toLower().indexOf(marker);
    if (at >= 0) {
        const QString path = line.mid(at + marker.length()).trimmed();
        if (!path.isEmpty())
            m_resultPath = path;
    }
}

void RomManagerDialog::onRunnerProgress(int percent)
{
    // The second stage occupies the top half of the bar so the two phases are
    // visually distinct.
    int value = percent;
    if (m_stage == Stage::Injecting)
        value = 50 + percent / 2;
    else
        value = percent / 2;
    m_progress->setValue(qBound(0, value, 100));
}

void RomManagerDialog::onRunnerFinished(bool success, int exitCode)
{
    Q_UNUSED(exitCode)

    if (!success) {
        setStatus(m_runner->errorString().isEmpty()
                      ? tr("Provisioning failed.")
                      : m_runner->errorString(),
                  true);
        setBusy(false);
        m_stage = Stage::Idle;
        return;
    }

    if (m_stage == Stage::Fetching) {
        if (m_resultPath.isEmpty())
            m_resultPath = outputPath();
        if (m_armCheck->isChecked()) {
            beginInjection();
            return;
        }
        m_stage = Stage::Done;
        m_progress->setValue(100);
        setStatus(tr("Image ready: %1").arg(m_resultPath));
        emit imageReady(m_resultPath);
        setBusy(false);
        return;
    }

    // Injection finished.
    m_stage = Stage::Done;
    m_progress->setValue(100);
    setStatus(tr("Image ready: %1").arg(m_resultPath));
    emit imageReady(m_resultPath);
    setBusy(false);
}

void RomManagerDialog::onRunnerFailed(const QString &message)
{
    setStatus(message, true);
    setBusy(false);
    m_stage = Stage::Idle;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void RomManagerDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_startButton->setEnabled(!busy);
    m_cancelButton->setEnabled(busy);
    m_presetList->setEnabled(!busy);
    m_outputEdit->setReadOnly(busy);
    m_armCheck->setEnabled(!busy);
    m_spoofCheck->setEnabled(!busy);
    m_profileCombo->setEnabled(!busy && m_spoofCheck->isChecked());
    m_dataDiskCheck->setEnabled(!busy);

    if (busy)
        m_progress->setValue(0);
}

void RomManagerDialog::appendConsole(const QString &line)
{
    m_console->appendPlainText(line);
    m_console->verticalScrollBar()->setValue(
        m_console->verticalScrollBar()->maximum());
}

void RomManagerDialog::setStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    m_statusLabel->setProperty("state",
                               error ? QStringLiteral("error")
                                     : QStringLiteral("ok"));
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}
