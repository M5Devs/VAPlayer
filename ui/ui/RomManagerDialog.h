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

#ifndef VAPLAYER_ROMMANAGERDIALOG_H
#define VAPLAYER_ROMMANAGERDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class ToolRunner;

/**
 * ROM manager: download a base Android image, optionally inject the ARM
 * translation bridge and spoof a high-end device, then hand the resulting
 * qcow2 to the emulator.
 *
 * Two tools run in sequence through ToolRunner: ``fetch_image.py`` produces
 * the qcow2 and ``inject_arm.py`` patches it in place.
 */
class RomManagerDialog : public QDialog
{
    Q_OBJECT

public:
    /** A selectable base image. */
    struct Preset {
        QString key;          ///< fetch_image --image value, or empty for local
        QString title;
        QString subtitle;
        QString android;
        bool local = false;
    };

    explicit RomManagerDialog(QWidget *parent = nullptr);
    ~RomManagerDialog() override;

    /** The preset list, in display order. */
    static QList<Preset> presets();

    /** Where provisioned images are written by default. */
    static QString defaultImageDirectory();

    /** The qcow2 produced by the last successful run, if any. */
    QString resultImagePath() const { return m_resultPath; }

    bool isBusy() const { return m_busy; }

    /** Exposed for tests. */
    ToolRunner *runner() const { return m_runner; }
    QString consoleText() const;

    /** Select a preset by key (used by tests and callers). */
    void selectPreset(const QString &key);

    void setOutputPath(const QString &path);
    QString outputPath() const;

public slots:
    void startProvisioning();
    void cancel();

signals:
    /** Emitted once the qcow2 exists and is ready to boot. */
    void imageReady(const QString &imagePath);

private slots:
    void onPresetChanged(int index);
    void onBrowseLocal();
    void onBrowseOutput();
    void onRunnerLine(const QString &line);
    void onRunnerProgress(int percent);
    void onRunnerFinished(bool success, int exitCode);
    void onRunnerFailed(const QString &message);

private:
    enum class Stage { Idle, Fetching, Injecting, Done };

    void buildUi();
    void applyTheme();
    void setBusy(bool busy);
    void setStatus(const QString &text, bool error = false);
    void appendConsole(const QString &line);
    void beginInjection();
    bool validateSelection();
    QString currentPresetKey() const;

    ToolRunner *m_runner = nullptr;

    QListWidget *m_presetList = nullptr;
    QLineEdit *m_localIsoEdit = nullptr;
    QPushButton *m_browseLocalButton = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_browseOutputButton = nullptr;
    QCheckBox *m_armCheck = nullptr;
    QCheckBox *m_spoofCheck = nullptr;
    QComboBox *m_profileCombo = nullptr;
    QCheckBox *m_dataDiskCheck = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPlainTextEdit *m_console = nullptr;

    Stage m_stage = Stage::Idle;
    bool m_busy = false;
    QString m_resultPath;
    QString m_pendingIso;
};

#endif // VAPLAYER_ROMMANAGERDIALOG_H
