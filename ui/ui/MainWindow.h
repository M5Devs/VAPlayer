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

#include "process/EmulatorRunner.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QLabel;
class QTimer;
class QmpClient;
class QTcpSocket;

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

    /** Apply a new disk image path everywhere it is used and persist it. */
    void applyDiskImage(const QString &imagePath);

    Ui::MainWindow *ui = nullptr;
    EmulatorRunner *m_runner = nullptr;
    QmpClient *m_qmp = nullptr;

    QTimer *m_adbTimer = nullptr;
    QTcpSocket *m_adbProbe = nullptr;

    Orientation m_orientation = Orientation::Portrait;
    bool m_adbConnected = false;

    /** Persisted, user-editable emulator settings. */
    EmulatorConfig m_config;

    /** Last package chosen in V-Flash, remembered across dialog sessions. */
    QString m_lastFlashPackage;
};

#endif // VAPLAYER_MAINWINDOW_H
