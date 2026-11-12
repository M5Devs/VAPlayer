/*
 * VAPlayer - shared folder dialog
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

#include "ui/SharedFolderDialog.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr char kStyleSheet[] = R"(
QWidget {
    background-color: #14161a;
    color: #e6e8ec;
    font-family: "Inter", "Segoe UI", "Noto Sans", sans-serif;
    font-size: 13px;
}
#dialogHeader { font-size: 17px; font-weight: 700; color: #f2f4f8; }
#hintLabel { color: #6c7480; font-size: 11px; }
#statusLabel { color: #b9bfca; }
QLineEdit {
    background-color: #1b1e24;
    border: 1px solid #2f343d;
    border-radius: 6px;
    padding: 6px 8px;
}
QLineEdit:focus { border-color: #2f7d4f; }
QPushButton {
    background-color: #23272f;
    border: 1px solid #2f343d;
    border-radius: 7px;
    padding: 8px 12px;
    color: #dfe3e9;
}
QPushButton:hover { background-color: #2b303a; border-color: #3a404b; }
QPushButton:pressed { background-color: #1e222a; }
QGroupBox {
    border: 1px solid #262a31;
    border-radius: 8px;
    margin-top: 14px;
    padding-top: 10px;
    font-weight: 600;
    color: #cfd4dc;
}
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }
QCheckBox { spacing: 8px; color: #c4cad3; }
QCheckBox::indicator {
    width: 15px; height: 15px; border-radius: 4px;
    border: 1px solid #3a404b; background-color: #1b1e24;
}
QCheckBox::indicator:checked {
    background-color: #2f7d4f; border-color: #37945c;
}
)";

} // namespace

SharedFolderDialog::SharedFolderDialog(QWidget *parent)
    : QDialog(parent)
{
    buildUi();
    applyTheme();
}

QString SharedFolderDialog::defaultFolderPath()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QDir(base).absoluteFilePath(QStringLiteral(".vaplayer/shared"));
}

void SharedFolderDialog::buildUi()
{
    setWindowTitle(tr("Shared Folder"));
    setMinimumSize(620, 420);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto *header = new QLabel(tr("Shared Folder"), this);
    header->setObjectName(QStringLiteral("dialogHeader"));
    root->addWidget(header);

    auto *hint = new QLabel(
        tr("Files placed in this folder on the host appear inside Android. "
           "Use VirtFS for a live mount, or ADB sync to copy into "
           "/sdcard/Download/Shared."),
        this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *folderGroup = new QGroupBox(tr("Host directory"), this);
    auto *folderLayout = new QHBoxLayout(folderGroup);
    m_pathEdit = new QLineEdit(folderGroup);
    m_pathEdit->setText(defaultFolderPath());
    m_pathEdit->setPlaceholderText(tr("Choose a folder to share…"));
    m_browseButton = new QPushButton(tr("Browse…"), folderGroup);
    m_openButton = new QPushButton(tr("Open"), folderGroup);
    folderLayout->addWidget(m_pathEdit, 1);
    folderLayout->addWidget(m_browseButton);
    folderLayout->addWidget(m_openButton);
    root->addWidget(folderGroup);

    auto *transportGroup = new QGroupBox(tr("Transport"), this);
    auto *transportLayout = new QVBoxLayout(transportGroup);
    m_virtfsCheck = new QCheckBox(tr("Export over VirtFS (9p) at boot"), transportGroup);
    m_virtfsCheck->setChecked(true);
    m_adbCheck = new QCheckBox(tr("Also sync over ADB now"), transportGroup);
    m_mountHint = new QLabel(
        tr("Guest mount point: %1").arg(guestMountPoint()), transportGroup);
    m_mountHint->setObjectName(QStringLiteral("hintLabel"));
    transportLayout->addWidget(m_virtfsCheck);
    transportLayout->addWidget(m_adbCheck);
    transportLayout->addWidget(m_mountHint);
    root->addWidget(transportGroup);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);
    root->addStretch(1);

    auto *buttonRow = new QHBoxLayout();
    m_syncButton = new QPushButton(tr("Sync now"), this);
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_okButton = new QPushButton(tr("Save"), this);
    m_okButton->setDefault(true);
    buttonRow->addWidget(m_syncButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_cancelButton);
    buttonRow->addWidget(m_okButton);
    root->addLayout(buttonRow);

    connect(m_browseButton, &QPushButton::clicked, this, &SharedFolderDialog::onBrowse);
    connect(m_openButton, &QPushButton::clicked, this, &SharedFolderDialog::onOpenFolder);
    connect(m_syncButton, &QPushButton::clicked, this, &SharedFolderDialog::onSyncNow);
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

void SharedFolderDialog::applyTheme()
{
    setStyleSheet(QString::fromUtf8(kStyleSheet));
}

void SharedFolderDialog::onBrowse()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose a folder to share"), m_pathEdit->text());
    if (!dir.isEmpty())
        m_pathEdit->setText(dir);
}

void SharedFolderDialog::onOpenFolder()
{
    const QString path = folderPath();
    if (path.isEmpty())
        return;
    // Create it on demand so "Open" works on a first run.
    if (QDir().mkpath(path))
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    else
        setStatus(tr("Could not create %1.").arg(path), true);
}

void SharedFolderDialog::onSyncNow()
{
    m_syncRequested = true;
    m_adbCheck->setChecked(true);
    setStatus(tr("The folder will be synced when you save."));
}

void SharedFolderDialog::setStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color: #e0796b;")
                                       : QString());
}

void SharedFolderDialog::setFolderPath(const QString &path)
{
    if (!path.isEmpty())
        m_pathEdit->setText(path);
}

QString SharedFolderDialog::folderPath() const
{
    return m_pathEdit->text().trimmed();
}

void SharedFolderDialog::setVirtfsEnabled(bool enabled)
{
    m_virtfsCheck->setChecked(enabled);
}

bool SharedFolderDialog::virtfsEnabled() const
{
    return m_virtfsCheck->isChecked();
}

void SharedFolderDialog::setAdbSyncEnabled(bool enabled)
{
    m_adbCheck->setChecked(enabled);
}

bool SharedFolderDialog::adbSyncEnabled() const
{
    return m_adbCheck->isChecked() || m_syncRequested;
}
