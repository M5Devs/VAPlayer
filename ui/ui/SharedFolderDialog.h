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

#ifndef VAPLAYER_SHAREDFOLDERDIALOG_H
#define VAPLAYER_SHAREDFOLDERDIALOG_H

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

/**
 * Configures the host directory shared with the guest.
 *
 * Two transports are offered:
 *  - VirtFS (9p): the directory is exported to the guest at boot, where it
 *    appears as a mounted filesystem.  Nothing needs to run inside Android.
 *  - ADB sync: pushes the directory contents into
 *    ``/sdcard/Download/Shared`` on demand, for guests without 9p support.
 *
 * The dialog only collects the settings; MainWindow applies them and performs
 * the sync, so the dialog stays testable without a running emulator.
 */
class SharedFolderDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SharedFolderDialog(QWidget *parent = nullptr);

    void setFolderPath(const QString &path);
    QString folderPath() const;

    void setVirtfsEnabled(bool enabled);
    bool virtfsEnabled() const;

    void setAdbSyncEnabled(bool enabled);
    bool adbSyncEnabled() const;

    /** True when the user asked for a sync by accepting the dialog. */
    bool syncRequested() const { return m_syncRequested; }

    /** Default shared directory: ``~/.vaplayer/shared``. */
    static QString defaultFolderPath();

    /** Guest mount point exposed over VirtFS. */
    static QString guestMountPoint() { return QStringLiteral("/mnt/vaplayer"); }

    /** Guest directory ADB sync copies into. */
    static QString guestSyncTarget()
    {
        return QStringLiteral("/sdcard/Download/Shared");
    }

private slots:
    void onBrowse();
    void onOpenFolder();
    void onSyncNow();

private:
    void buildUi();
    void applyTheme();
    void setStatus(const QString &text, bool error = false);

    QLineEdit *m_pathEdit = nullptr;
    QPushButton *m_browseButton = nullptr;
    QPushButton *m_openButton = nullptr;
    QCheckBox *m_virtfsCheck = nullptr;
    QCheckBox *m_adbCheck = nullptr;
    QPushButton *m_syncButton = nullptr;
    QPushButton *m_okButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_mountHint = nullptr;

    bool m_syncRequested = false;
};

#endif // VAPLAYER_SHAREDFOLDERDIALOG_H
