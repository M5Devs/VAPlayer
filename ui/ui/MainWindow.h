/*
 * VAPlayer - main application window
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

#ifndef VAPLAYER_MAINWINDOW_H
#define VAPLAYER_MAINWINDOW_H

#include <QMainWindow>
#include <QPointer>

#include "keymapper/Keymap.h"
#include "process/EmulatorRunner.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QLabel;
class QTimer;
class AdbClient;
class KeymapperDialog;
class KeymapperEngine;
class QmpClient;
class QTcpSocket;
class ViewportWidget;

/**
 * Top-level window: a dark themed shell with a screen viewport on the right and
 * an emulator control sidebar on the left.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void onPowerClicked();
    void onShutdownClicked();
    void onResetClicked();
    void onVolumeUpClicked();
    void onVolumeDownClicked();
    void onRotateClicked();
    void onSettingsClicked();
    void onAdbConnectClicked();
    void onVFlashClicked();
    void onRomManagerClicked();

    void onBackClicked();
    void onHomeClicked();
    void onRecentsClicked();
    void onScreenshotClicked();
    void onFullscreenToggled(bool enabled);
    void onSharedFolderClicked();
    void onKeymapperToggled(bool enabled);

    void onFilesDropped(const QStringList &paths);
    void processNextDrop();

    void onRomImageReady(const QString &imagePath);

    void onRunnerStateChanged(EmulatorRunner::State state);
    void onRunnerError(const QString &message);
    void onRunnerLog(const QString &line);

    void onQmpConnected();
    void onQmpError(const QString &message);

    void pollAdbStatus();

private:
    enum class Orientation { Portrait, Landscape };

    void setupTheme();
    void setupConnections();
    void applyStateToControls();
    void connectQmp();
    void disconnectQmp();

    void setOrientation(Orientation orientation);
    void applyOrientation();

    void updateAdbIndicator(bool connected);
    void setStatusMessage(const QString &message);
    void appendLog(const QString &line);
    void showToast(const QString &message);

    /** Apply a new disk image path everywhere it is used and persist it. */
    void applyDiskImage(const QString &imagePath);

    /** Route a single dropped file to install, flash or push. */
    void handleDroppedFile(const QString &path);
    /** Recursively push a directory into the guest's shared folder. */
    void pushDirectory(const QString &localDir, const QString &remoteDir);

    /** Load the keymap for the active game, falling back to the default. */
    void loadKeymapForActiveGame();
    /** Persist the active keymap and refresh the overlay. */
    void persistActiveKeymap();

    Ui::MainWindow *ui = nullptr;
    EmulatorRunner *m_runner = nullptr;
    QmpClient *m_qmp = nullptr;
    AdbClient *m_adb = nullptr;

    QTimer *m_adbTimer = nullptr;
    QTcpSocket *m_adbProbe = nullptr;

    ViewportWidget *m_viewport = nullptr;
    KeymapperEngine *m_keymapper = nullptr;
    QPointer<KeymapperDialog> m_keymapperDialog;

    Orientation m_orientation = Orientation::Portrait;
    bool m_adbConnected = false;
    bool m_keymapperEnabled = false;

    /** Active keymap profile (per game, or the default). */
    Keymap m_keymap;

    /** Files queued for the sequential drop handler. */
    QStringList m_dropQueue;
    bool m_dropInProgress = false;

    /** Persisted, user-editable emulator settings. */
    EmulatorConfig m_config;

    /** Last package chosen in V-Flash, remembered across dialog sessions. */
    QString m_lastFlashPackage;
};

#endif // VAPLAYER_MAINWINDOW_H
