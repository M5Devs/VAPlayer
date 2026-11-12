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

#ifndef VAPLAYER_VFLASHDIALOG_H
#define VAPLAYER_VFLASHDIALOG_H

#include <QDialog>
#include <QHash>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTabWidget;
class ToolRunner;

/**
 * Front end for the ``vflash`` tool.
 *
 * Two tabs:
 *  - Flash: pick a target disk image and a flashable ZIP, then flash it.
 *  - Inspect: read the filesystem and Android properties out of the image.
 *
 * Both actions run ``tools/bin/vflash`` asynchronously; output streams into a
 * console and the progress bar reflects the tool's stage messages.
 */
class VFlashDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VFlashDialog(QWidget *parent = nullptr);
    ~VFlashDialog() override;

    /** Pre-select the image shown in the dialog (the loaded VM disk). */
    void setDefaultImagePath(const QString &path);
    void setImagePath(const QString &path);
    QString imagePath() const;

    void setPackagePath(const QString &path);
    QString packagePath() const;

    /** True while a tool process is running. */
    bool isBusy() const;

    /** The parsed key/value result of the most recent inspect, if any. */
    QHash<QString, QString> inspectResult() const { return m_inspectResult; }

    /** The runner driving the tool, exposed for tests and diagnostics. */
    ToolRunner *runner() const { return m_runner; }

    /** Console text, exposed for tests. */
    QString consoleText() const;

public slots:
    void startInspect();
    void startFlash();
    void cancel();

signals:
    /** Emitted after a successful flash, with the image that was modified. */
    void flashCompleted(const QString &imagePath);
    /** Emitted when the image being inspected or flashed changes. */
    void imageChanged(const QString &imagePath);

private slots:
    void onBrowseImage();
    void onBrowsePackage();
    void onRunnerLine(const QString &line);
    void onRunnerProgress(int percent);
    void onRunnerFinished(bool success, int exitCode);
    void onRunnerFailed(const QString &message);

private:
    enum class Mode { Inspect, Flash };

    void buildUi();
    void applyTheme();
    void setBusy(bool busy, Mode mode);
    void appendConsole(const QString &line);
    void populateInspect(const QHash<QString, QString> &fields);
    void setStatus(const QString &text, bool error = false);
    QString backupPath() const;

    ToolRunner *m_runner = nullptr;

    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_imageEdit = nullptr;
    QLineEdit *m_packageEdit = nullptr;
    QPushButton *m_browseImageButton = nullptr;
    QPushButton *m_browsePackageButton = nullptr;
    QCheckBox *m_backupCheck = nullptr;
    QPushButton *m_flashButton = nullptr;
    QPushButton *m_inspectButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QTableWidget *m_inspectTable = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_imageSummary = nullptr;
    QProgressBar *m_progress = nullptr;
    QPlainTextEdit *m_console = nullptr;

    Mode m_mode = Mode::Inspect;
    bool m_busy = false;
    QHash<QString, QString> m_inspectResult;
};

#endif // VAPLAYER_VFLASHDIALOG_H
