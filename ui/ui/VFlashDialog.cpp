/*
 * VAPlayer - V-Flash dialog
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

#include "ui/VFlashDialog.h"

#include "tools/ToolRunner.h"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
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
QTabWidget::pane {
    border: 1px solid #262a31;
    border-radius: 8px;
    top: -1px;
}
QTabBar::tab {
    background: #1b1e24;
    color: #9aa2ae;
    padding: 7px 16px;
    border: 1px solid #262a31;
    border-bottom: none;
    border-top-left-radius: 7px;
    border-top-right-radius: 7px;
}
QTabBar::tab:selected {
    background: #23272f;
    color: #f2f4f8;
}
QLineEdit {
    background-color: #1b1e24;
    border: 1px solid #2f343d;
    border-radius: 6px;
    padding: 6px 8px;
    color: #e6e8ec;
}
QLineEdit:focus {
    border-color: #2f7d4f;
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
QTableWidget {
    background-color: #0f1114;
    border: 1px solid #262a31;
    border-radius: 8px;
    gridline-color: #23272f;
    color: #cfd6e0;
}
QHeaderView::section {
    background-color: #1b1e24;
    color: #8b93a1;
    border: none;
    padding: 6px 8px;
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

/** Friendly labels for the keys the inspect output can contain. */
QString friendlyKey(const QString &key)
{
    static const QHash<QString, QString> labels = {
        { QStringLiteral("image"),            QStringLiteral("Image") },
        { QStringLiteral("container"),        QStringLiteral("Container") },
        { QStringLiteral("partition scheme"), QStringLiteral("Partition scheme") },
        { QStringLiteral("partition used"),   QStringLiteral("Partition used") },
        { QStringLiteral("ext4 offset"),      QStringLiteral("ext4 offset") },
        { QStringLiteral("label"),            QStringLiteral("Filesystem label") },
        { QStringLiteral("uuid"),             QStringLiteral("Filesystem UUID") },
        { QStringLiteral("block size"),       QStringLiteral("Block size") },
        { QStringLiteral("size"),             QStringLiteral("Size") },
        { QStringLiteral("inodes"),           QStringLiteral("Inodes") },
        { QStringLiteral("free blocks"),      QStringLiteral("Free blocks") },
        { QStringLiteral("installed"),        QStringLiteral("Bridge installed") },
        { QStringLiteral("library"),          QStringLiteral("Bridge library") },
        { QStringLiteral("proxy libs"),       QStringLiteral("Proxy libraries") },
        { QStringLiteral("root entries"),     QStringLiteral("Root entries") },
    };
    return labels.value(key, key);
}

} // namespace

VFlashDialog::VFlashDialog(QWidget *parent)
    : QDialog(parent)
    , m_runner(new ToolRunner(this))
{
    buildUi();
    applyTheme();

    connect(m_runner, &ToolRunner::outputLine,
            this, &VFlashDialog::onRunnerLine);
    connect(m_runner, &ToolRunner::progressChanged,
            this, &VFlashDialog::onRunnerProgress);
    connect(m_runner, &ToolRunner::finished,
            this, &VFlashDialog::onRunnerFinished);
    connect(m_runner, &ToolRunner::failed,
            this, &VFlashDialog::onRunnerFailed);
}

VFlashDialog::~VFlashDialog() = default;

void VFlashDialog::buildUi()
{
    setWindowTitle(tr("V-Flash"));
    setMinimumSize(720, 560);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto *header = new QLabel(tr("V-Flash"), this);
    header->setObjectName(QStringLiteral("dialogHeader"));
    root->addWidget(header);

    auto *hint = new QLabel(
        tr("Inspect a disk image or flash a package (Magisk, patches) into it — "
           "offline, without booting the guest."), this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    // Target image row, shared by both tabs.
    auto *targetGroup = new QGroupBox(tr("Target disk image"), this);
    auto *targetLayout = new QHBoxLayout(targetGroup);
    m_imageEdit = new QLineEdit(targetGroup);
    m_imageEdit->setPlaceholderText(tr("Select a .qcow2 / .img / .raw image…"));
    m_browseImageButton = new QPushButton(tr("Browse…"), targetGroup);
    targetLayout->addWidget(m_imageEdit, 1);
    targetLayout->addWidget(m_browseImageButton);
    root->addWidget(targetGroup);

    m_imageSummary = new QLabel(tr("No image selected."), this);
    m_imageSummary->setObjectName(QStringLiteral("hintLabel"));
    m_imageSummary->setWordWrap(true);
    root->addWidget(m_imageSummary);

    m_tabs = new QTabWidget(this);

    // -- Flash tab ---------------------------------------------------------
    auto *flashTab = new QWidget(m_tabs);
    auto *flashLayout = new QVBoxLayout(flashTab);
    flashLayout->setContentsMargins(12, 12, 12, 12);
    flashLayout->setSpacing(10);

    auto *pkgGroup = new QGroupBox(tr("Flashable package"), flashTab);
    auto *pkgLayout = new QHBoxLayout(pkgGroup);
    m_packageEdit = new QLineEdit(pkgGroup);
    m_packageEdit->setPlaceholderText(tr("Select a flashable ZIP (e.g. Magisk)…"));
    m_browsePackageButton = new QPushButton(tr("Browse…"), pkgGroup);
    pkgLayout->addWidget(m_packageEdit, 1);
    pkgLayout->addWidget(m_browsePackageButton);
    flashLayout->addWidget(pkgGroup);

    m_backupCheck = new QCheckBox(
        tr("Write a backup copy of the image before flashing"), flashTab);
    m_backupCheck->setChecked(true);
    flashLayout->addWidget(m_backupCheck);

    auto *flashButtons = new QHBoxLayout();
    m_flashButton = new QPushButton(tr("Flash package"), flashTab);
    m_flashButton->setObjectName(QStringLiteral("primaryButton"));
    flashButtons->addWidget(m_flashButton);
    flashButtons->addStretch(1);
    flashLayout->addLayout(flashButtons);
    flashLayout->addStretch(1);

    m_tabs->addTab(flashTab, tr("Flash"));

    // -- Inspect tab -------------------------------------------------------
    auto *inspectTab = new QWidget(m_tabs);
    auto *inspectLayout = new QVBoxLayout(inspectTab);
    inspectLayout->setContentsMargins(12, 12, 12, 12);
    inspectLayout->setSpacing(10);

    m_inspectTable = new QTableWidget(0, 2, inspectTab);
    m_inspectTable->setHorizontalHeaderLabels({ tr("Property"), tr("Value") });
    m_inspectTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    m_inspectTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    m_inspectTable->verticalHeader()->setVisible(false);
    m_inspectTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_inspectTable->setSelectionMode(QAbstractItemView::SingleSelection);
    inspectLayout->addWidget(m_inspectTable, 1);

    auto *inspectButtons = new QHBoxLayout();
    m_inspectButton = new QPushButton(tr("Inspect image"), inspectTab);
    m_inspectButton->setObjectName(QStringLiteral("primaryButton"));
    inspectButtons->addWidget(m_inspectButton);
    inspectButtons->addStretch(1);
    inspectLayout->addLayout(inspectButtons);

    m_tabs->addTab(inspectTab, tr("Inspect Image"));
    root->addWidget(m_tabs, 1);

    // -- Console and status (shared) --------------------------------------
    auto *consoleLabel = new QLabel(tr("Output"), this);
    consoleLabel->setObjectName(QStringLiteral("hintLabel"));
    root->addWidget(consoleLabel);

    m_console = new QPlainTextEdit(this);
    m_console->setReadOnly(true);
    m_console->setMinimumHeight(120);
    m_console->setPlaceholderText(tr("Tool output appears here."));
    root->addWidget(m_console);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    root->addWidget(m_progress);

    auto *footer = new QHBoxLayout();
    m_statusLabel = new QLabel(tr("Ready."), this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    footer->addWidget(m_statusLabel, 1);
    m_closeButton = new QPushButton(tr("Close"), this);
    footer->addWidget(m_closeButton);
    root->addLayout(footer);

    connect(m_browseImageButton, &QPushButton::clicked,
            this, &VFlashDialog::onBrowseImage);
    connect(m_browsePackageButton, &QPushButton::clicked,
            this, &VFlashDialog::onBrowsePackage);
    connect(m_inspectButton, &QPushButton::clicked,
            this, &VFlashDialog::startInspect);
    connect(m_flashButton, &QPushButton::clicked,
            this, &VFlashDialog::startFlash);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(m_imageEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (!m_busy)
            emit imageChanged(text);
    });
}

void VFlashDialog::applyTheme()
{
    setStyleSheet(QString::fromUtf8(kStyleSheet));
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

void VFlashDialog::setDefaultImagePath(const QString &path)
{
    if (m_imageEdit->text().isEmpty() && !path.isEmpty())
        setImagePath(path);
}

void VFlashDialog::setImagePath(const QString &path)
{
    m_imageEdit->setText(path);
    if (path.isEmpty()) {
        m_imageSummary->setText(tr("No image selected."));
        return;
    }
    const QFileInfo info(path);
    if (info.exists()) {
        m_imageSummary->setText(
            tr("%1 — %2 MiB")
                .arg(info.fileName())
                .arg(info.size() / (1024.0 * 1024.0), 0, 'f', 1));
    } else {
        m_imageSummary->setText(tr("%1 (not found)").arg(info.fileName()));
    }
}

void VFlashDialog::setPackagePath(const QString &path)
{
    m_packageEdit->setText(path);
}

QString VFlashDialog::imagePath() const
{
    return m_imageEdit->text().trimmed();
}

QString VFlashDialog::packagePath() const
{
    return m_packageEdit->text().trimmed();
}

QString VFlashDialog::consoleText() const
{
    return m_console->toPlainText();
}

bool VFlashDialog::isBusy() const
{
    return m_busy;
}

QString VFlashDialog::backupPath() const
{
    const QString image = imagePath();
    if (image.isEmpty())
        return QString();
    const QFileInfo info(image);
    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
           + QStringLiteral(".backup.") + info.suffix();
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void VFlashDialog::startInspect()
{
    if (m_busy)
        return;
    if (imagePath().isEmpty()) {
        setStatus(tr("Select a disk image to inspect first."), true);
        return;
    }

    m_inspectTable->setRowCount(0);
    m_inspectResult.clear();
    m_mode = Mode::Inspect;
    setBusy(true, Mode::Inspect);
    setStatus(tr("Inspecting %1…").arg(QFileInfo(imagePath()).fileName()));

    m_runner->start(QStringLiteral("vflash"),
                    { QStringLiteral("--image"), imagePath(),
                      QStringLiteral("--inspect") });
}

void VFlashDialog::startFlash()
{
    if (m_busy)
        return;
    if (imagePath().isEmpty()) {
        setStatus(tr("Select a disk image to flash into first."), true);
        return;
    }
    if (packagePath().isEmpty()) {
        setStatus(tr("Select a flashable ZIP package first."), true);
        return;
    }

    m_mode = Mode::Flash;
    setBusy(true, Mode::Flash);
    setStatus(tr("Flashing %1…").arg(QFileInfo(packagePath()).fileName()));

    QStringList args{ QStringLiteral("--image"), imagePath(),
                      QStringLiteral("--flash"), packagePath() };
    if (m_backupCheck->isChecked())
        args << QStringLiteral("--backup") << backupPath();

    m_runner->start(QStringLiteral("vflash"), args);
}

void VFlashDialog::cancel()
{
    if (m_busy)
        m_runner->cancel();
}

// ---------------------------------------------------------------------------
// Runner reactions
// ---------------------------------------------------------------------------

void VFlashDialog::onRunnerLine(const QString &line)
{
    appendConsole(line);
}

void VFlashDialog::onRunnerProgress(int percent)
{
    m_progress->setValue(qBound(0, percent, 100));
}

void VFlashDialog::onRunnerFinished(bool success, int exitCode)
{
    Q_UNUSED(exitCode)

    if (success) {
        if (m_mode == Mode::Inspect) {
            m_inspectResult = ToolRunner::parseKeyValues(m_runner->output());
            populateInspect(m_inspectResult);
            setStatus(tr("Inspection complete."));
        } else {
            setStatus(tr("Flash complete."));
            emit flashCompleted(imagePath());
        }
    } else {
        setStatus(m_runner->errorString().isEmpty()
                      ? tr("The tool reported a failure.")
                      : m_runner->errorString(),
                  true);
    }

    setBusy(false, m_mode);
}

void VFlashDialog::onRunnerFailed(const QString &message)
{
    setStatus(message, true);
    setBusy(false, m_mode);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void VFlashDialog::onBrowseImage()
{
    const QString start = imagePath().isEmpty()
                              ? QDir::homePath()
                              : QFileInfo(imagePath()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select a disk image"), start,
        tr("Disk images (*.qcow2 *.img *.raw);;All files (*)"));
    if (!path.isEmpty())
        setImagePath(path);
}

void VFlashDialog::onBrowsePackage()
{
    const QString start = packagePath().isEmpty()
                              ? QDir::homePath()
                              : QFileInfo(packagePath()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select a flashable package"), start,
        tr("Flashable packages (*.zip);;All files (*)"));
    if (!path.isEmpty())
        setPackagePath(path);
}

void VFlashDialog::setBusy(bool busy, Mode mode)
{
    m_busy = busy;
    m_mode = mode;

    m_inspectButton->setEnabled(!busy);
    m_flashButton->setEnabled(!busy);
    m_browseImageButton->setEnabled(!busy);
    m_browsePackageButton->setEnabled(!busy);
    m_imageEdit->setReadOnly(busy);
    m_packageEdit->setReadOnly(busy);

    if (busy)
        m_progress->setValue(0);
}

void VFlashDialog::appendConsole(const QString &line)
{
    m_console->appendPlainText(line);
    // Keep the newest output visible.
    m_console->verticalScrollBar()->setValue(
        m_console->verticalScrollBar()->maximum());
}

void VFlashDialog::populateInspect(const QHash<QString, QString> &fields)
{
    m_inspectTable->setRowCount(0);

    // Present a stable, readable ordering before the remaining keys.
    const QStringList preferred = {
        QStringLiteral("image"),            QStringLiteral("container"),
        QStringLiteral("partition scheme"), QStringLiteral("partition used"),
        QStringLiteral("ext4 offset"),      QStringLiteral("label"),
        QStringLiteral("uuid"),             QStringLiteral("block size"),
        QStringLiteral("size"),             QStringLiteral("inodes"),
        QStringLiteral("free blocks"),      QStringLiteral("installed"),
        QStringLiteral("library"),          QStringLiteral("proxy libs"),
    };

    const auto addRow = [this](const QString &key, const QString &value) {
        const int row = m_inspectTable->rowCount();
        m_inspectTable->insertRow(row);
        m_inspectTable->setItem(row, 0, new QTableWidgetItem(friendlyKey(key)));
        m_inspectTable->setItem(row, 1, new QTableWidgetItem(value));
    };

    QSet<QString> seen;
    for (const QString &key : preferred) {
        if (fields.contains(key)) {
            addRow(key, fields.value(key));
            seen.insert(key);
        }
    }
    QStringList rest = fields.keys();
    rest.sort();
    for (const QString &key : rest) {
        if (!seen.contains(key))
            addRow(key, fields.value(key));
    }

    const QString label = fields.value(QStringLiteral("label"));
    const QString size = fields.value(QStringLiteral("size"));
    const QString bridge = fields.value(QStringLiteral("installed"));
    m_imageSummary->setText(
        tr("%1 — filesystem “%2”%3%4")
            .arg(QFileInfo(imagePath()).fileName(),
                 label.isEmpty() ? tr("unknown") : label,
                 size.isEmpty() ? QString() : QStringLiteral(", ") + size,
                 bridge.isEmpty()
                     ? QString()
                     : QStringLiteral(", ARM bridge: ") + bridge));
}

void VFlashDialog::setStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    m_statusLabel->setProperty("state",
                               error ? QStringLiteral("error")
                                     : QStringLiteral("ok"));
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}
