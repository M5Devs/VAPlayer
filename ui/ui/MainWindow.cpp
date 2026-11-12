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

#include "ui/MainWindow.h"
#include "ui/ui_MainWindow.h"
#include "ui/SettingsDialog.h"

#include "core_bridge/QmpClient.h"
#include "process/EmulatorRunner.h"

#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHostAddress>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <memory>

namespace {

constexpr int kAdbPort = 5555;
constexpr int kAdbPollIntervalMs = 3000;

/** X11 key codes for the Android volume rocker. */
constexpr char kKeyVolumeUp[] = "volumeup";
constexpr char kKeyVolumeDown[] = "volumedown";

/** Palette for the dark, minimal "gaming" theme. */
constexpr char kStyleSheet[] = R"(
QWidget {
    background-color: #14161a;
    color: #e6e8ec;
    font-family: "Inter", "Segoe UI", "Noto Sans", sans-serif;
    font-size: 13px;
}
QMainWindow, QDialog {
    background-color: #14161a;
}
#sidebar {
    background-color: #1b1e24;
    border-right: 1px solid #262a31;
}
#viewportFrame {
    background-color: #0f1114;
}
#screenFrame {
    background-color: #000000;
    border: 1px solid #262a31;
    border-radius: 10px;
}
#screenLabel {
    background: transparent;
    color: #5a626e;
    font-size: 14px;
}
#brandLabel {
    font-size: 19px;
    font-weight: 700;
    color: #f2f4f8;
}
#brandSubtitle {
    color: #7d8593;
    font-size: 11px;
}
#sectionLabel {
    color: #6c7480;
    font-size: 10px;
    font-weight: 700;
    letter-spacing: 1px;
    margin-top: 4px;
}
#dialogHeader {
    font-size: 17px;
    font-weight: 700;
    color: #f2f4f8;
}
#fieldLabel {
    color: #b9bfca;
}
#hintLabel {
    color: #6c7480;
    font-size: 11px;
}
#statusBarLabel {
    color: #8b93a1;
    font-size: 12px;
}
#statusLabel {
    color: #b9bfca;
}
#separator {
    color: #262a31;
    background-color: #262a31;
    max-height: 1px;
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
QPushButton:pressed {
    background-color: #1e222a;
}
QPushButton:disabled {
    background-color: #1a1d22;
    color: #565c66;
    border-color: #23272f;
}
#powerButton {
    background-color: #2f7d4f;
    border: 1px solid #37945c;
    color: #ffffff;
    font-weight: 600;
}
#powerButton:hover {
    background-color: #37945c;
}
#powerButton:pressed {
    background-color: #286843;
}
#powerButton:disabled {
    background-color: #1f3a2b;
    border-color: #27492f;
    color: #6f8b78;
}
#ghostButton {
    background-color: transparent;
    border: 1px solid #2f343d;
    color: #aeb5c0;
}
#ghostButton:hover {
    background-color: #23272f;
}
#statusDot {
    background-color: #5a626e;
    border-radius: 5px;
}
#statusDot[state="online"] {
    background-color: #3fb96a;
}
#statusDot[state="offline"] {
    background-color: #c0504d;
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
QSpinBox {
    background-color: #1b1e24;
    border: 1px solid #2f343d;
    border-radius: 6px;
    padding: 4px 8px;
    min-width: 84px;
}
QSpinBox:focus {
    border-color: #2f7d4f;
}
QSlider::groove:horizontal {
    height: 4px;
    background: #2a2f37;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    background: #3fb96a;
    width: 14px;
    margin: -6px 0;
    border-radius: 7px;
}
QSlider::sub-page:horizontal {
    background: #2f7d4f;
    border-radius: 2px;
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
QStatusBar {
    background-color: #1b1e24;
    color: #8b93a1;
    border-top: 1px solid #262a31;
}
QToolTip {
    background-color: #23272f;
    color: #e6e8ec;
    border: 1px solid #2f343d;
}
)";

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    m_runner = new EmulatorRunner(this);
    m_qmp = new QmpClient(this);

    m_adbTimer = new QTimer(this);
    m_adbTimer->setInterval(kAdbPollIntervalMs);
    connect(m_adbTimer, &QTimer::timeout, this, &MainWindow::pollAdbStatus);

    setupTheme();
    setupConnections();

    // Restore persisted settings, then reflect them in the UI.
    QSettings settings;
    m_config.ramMb = settings.value(QStringLiteral("emulator/ramMb"), m_config.ramMb).toInt();
    m_config.cpuCores = settings.value(QStringLiteral("emulator/cpuCores"), m_config.cpuCores).toInt();
    m_config.diskImage = settings.value(QStringLiteral("emulator/diskImage")).toString();
    m_config.enableGpu = settings.value(QStringLiteral("emulator/enableGpu"), true).toBool();
    m_config.enableSound = settings.value(QStringLiteral("emulator/enableSound"), true).toBool();
    m_config.enableNetwork = settings.value(QStringLiteral("emulator/enableNetwork"), true).toBool();
    m_config.enableTablet = settings.value(QStringLiteral("emulator/enableTablet"), true).toBool();

    m_runner->setConfig(m_config);

    setOrientation(Orientation::Portrait);
    applyStateToControls();
    updateAdbIndicator(false);

    m_adbTimer->start();
    pollAdbStatus();

    appendLog(tr("VAPlayer UI ready."));
    if (EmulatorRunner::resolveEmulatorPath(m_config.emulatorPath).isEmpty())
        appendLog(tr("Warning: vaplayer-core was not found. Build core/ or set the path in Settings."));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupTheme()
{
    setStyleSheet(QString::fromUtf8(kStyleSheet));
}

void MainWindow::setupConnections()
{
    connect(ui->powerButton, &QPushButton::clicked,
            this, &MainWindow::onPowerClicked);
    connect(ui->shutdownButton, &QPushButton::clicked,
            this, &MainWindow::onShutdownClicked);
    connect(ui->resetButton, &QPushButton::clicked,
            this, &MainWindow::onResetClicked);
    connect(ui->volumeUpButton, &QPushButton::clicked,
            this, &MainWindow::onVolumeUpClicked);
    connect(ui->volumeDownButton, &QPushButton::clicked,
            this, &MainWindow::onVolumeDownClicked);
    connect(ui->rotateButton, &QPushButton::clicked,
            this, &MainWindow::onRotateClicked);
    connect(ui->orientationButton, &QPushButton::clicked,
            this, &MainWindow::onRotateClicked);
    connect(ui->settingsButton, &QPushButton::clicked,
            this, &MainWindow::onSettingsClicked);
    connect(ui->adbConnectButton, &QPushButton::clicked,
            this, &MainWindow::onAdbConnectClicked);

    connect(m_runner, &EmulatorRunner::stateChanged,
            this, &MainWindow::onRunnerStateChanged);
    connect(m_runner, &EmulatorRunner::errorOccurred,
            this, &MainWindow::onRunnerError);
    connect(m_runner, &EmulatorRunner::logLine,
            this, &MainWindow::onRunnerLog);

    connect(m_qmp, &QmpClient::connected,
            this, &MainWindow::onQmpConnected);
    connect(m_qmp, &QmpClient::errorOccurred,
            this, &MainWindow::onQmpError);
    connect(m_qmp, &QmpClient::disconnected, this, [this]() {
        appendLog(tr("QMP disconnected."));
    });
}

void MainWindow::onPowerClicked()
{
    if (m_runner->isRunning()) {
        appendLog(tr("The emulator is already running."));
        return;
    }

    if (m_config.diskImage.isEmpty()) {
        const QString image = QFileDialog::getOpenFileName(
            this, tr("Select a disk image"), QDir::homePath(),
            tr("Disk images (*.img *.qcow2 *.raw);;All files (*)"));
        if (image.isEmpty())
            return;
        m_config.diskImage = image;
        m_runner->setConfig(m_config);
    }

    m_qmp->disconnectFromServer();
    if (!m_runner->start()) {
        appendLog(tr("Failed to launch the emulator."));
        return;
    }

    setStatusMessage(tr("Starting…"));
}

void MainWindow::onShutdownClicked()
{
    if (!m_runner->isRunning()) {
        appendLog(tr("Nothing to shut down."));
        return;
    }

    // Prefer a graceful ACPI shutdown; fall back to terminating the process.
    if (m_qmp->isReady())
        m_qmp->systemPowerdown();
    setStatusMessage(tr("Shutting down…"));

    // Give the guest a moment to power down before stopping the process.
    QTimer::singleShot(1500, this, [this]() {
        if (m_runner->isRunning())
            m_runner->stop();
    });
}

void MainWindow::onResetClicked()
{
    if (!m_runner->isRunning()) {
        appendLog(tr("Nothing to reset."));
        return;
    }

    if (m_qmp->isReady()) {
        m_qmp->reset();
        appendLog(tr("Reset requested through QMP."));
    } else {
        appendLog(tr("QMP not ready; restarting the emulator process."));
        m_runner->kill();
        m_qmp->disconnectFromServer();
        // Restart shortly after the old process has exited.
        QTimer::singleShot(600, this, [this]() { m_runner->start(); });
    }
}

void MainWindow::onVolumeUpClicked()
{
    if (!m_qmp->isReady()) {
        appendLog(tr("Volume control requires a running emulator."));
        return;
    }
    m_qmp->sendKey(QString::fromLatin1(kKeyVolumeUp), true);
    appendLog(tr("Volume up."));
}

void MainWindow::onVolumeDownClicked()
{
    if (!m_qmp->isReady()) {
        appendLog(tr("Volume control requires a running emulator."));
        return;
    }
    m_qmp->sendKey(QString::fromLatin1(kKeyVolumeDown), true);
    appendLog(tr("Volume down."));
}

void MainWindow::onRotateClicked()
{
    setOrientation(m_orientation == Orientation::Portrait
                       ? Orientation::Landscape
                       : Orientation::Portrait);
}

void MainWindow::onSettingsClicked()
{
    SettingsDialog dialog(this);
    dialog.setConfig(m_config);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_config = dialog.config();

    QSettings settings;
    settings.setValue(QStringLiteral("emulator/ramMb"), m_config.ramMb);
    settings.setValue(QStringLiteral("emulator/cpuCores"), m_config.cpuCores);
    settings.setValue(QStringLiteral("emulator/diskImage"), m_config.diskImage);
    settings.setValue(QStringLiteral("emulator/enableGpu"), m_config.enableGpu);
    settings.setValue(QStringLiteral("emulator/enableSound"), m_config.enableSound);
    settings.setValue(QStringLiteral("emulator/enableNetwork"), m_config.enableNetwork);
    settings.setValue(QStringLiteral("emulator/enableTablet"), m_config.enableTablet);

    m_runner->setConfig(m_config);
    appendLog(tr("Settings applied: %1 MB RAM, %2 CPU cores.")
                  .arg(m_config.ramMb)
                  .arg(m_config.cpuCores));

    if (m_runner->isRunning())
        appendLog(tr("Note: changes take effect after the next power-on."));
}

void MainWindow::onAdbConnectClicked()
{
    if (m_adbConnected) {
        appendLog(tr("ADB already reachable on port %1.").arg(kAdbPort));
        return;
    }

    setStatusMessage(tr("Probing ADB…"));
    // Force a fresh probe even if the periodic timer is between attempts.
    if (m_adbProbe && m_adbProbe->state() != QAbstractSocket::UnconnectedState)
        m_adbProbe->abort();
    pollAdbStatus();
}

void MainWindow::onRunnerStateChanged(EmulatorRunner::State state)
{
    applyStateToControls();

    switch (state) {
    case EmulatorRunner::State::Starting:
        setStatusMessage(tr("Starting…"));
        break;
    case EmulatorRunner::State::Running:
        setStatusMessage(tr("Running"));
        ui->screenLabel->setText(tr("Guest running\n\n(display surface pending)"));
        // The core opens its QMP socket shortly after start; retry briefly.
        connectQmp();
        break;
    case EmulatorRunner::State::Stopping:
        setStatusMessage(tr("Stopping…"));
        break;
    case EmulatorRunner::State::Stopped:
        setStatusMessage(tr("Idle"));
        ui->screenLabel->setText(tr("No signal\n\nPress “Power On” to start the emulator."));
        disconnectQmp();
        break;
    }
}

void MainWindow::onRunnerError(const QString &message)
{
    appendLog(tr("Error: %1").arg(message));
    setStatusMessage(tr("Error"));
}

void MainWindow::onRunnerLog(const QString &line)
{
    appendLog(line);
}

void MainWindow::onQmpConnected()
{
    appendLog(tr("QMP connected."));
    setStatusMessage(tr("Running (QMP connected)"));
}

void MainWindow::onQmpError(const QString &message)
{
    appendLog(tr("QMP: %1").arg(message));
}

void MainWindow::connectQmp()
{
    if (m_qmp->state() != QmpClient::State::Disconnected)
        return;

    const QString endpoint = m_runner->qmpEndpoint();
    if (endpoint.isEmpty()) {
        // Not started yet; retry once the runner has assigned an endpoint.
        QTimer::singleShot(300, this, &MainWindow::connectQmp);
        return;
    }

    // The socket file appears a moment after the process starts, so retry a
    // few times before giving up.
    auto attempts = std::make_shared<int>(0);
    auto tryConnect = std::make_shared<std::function<void()>>();
    *tryConnect = [this, endpoint, attempts, tryConnect]() {
        if (!m_runner->isRunning() || m_qmp->state() != QmpClient::State::Disconnected)
            return;
        if (QFile::exists(endpoint) || (*attempts)++ >= 10) {
            m_qmp->connectToServer(endpoint);
            return;
        }
        QTimer::singleShot(200, this, *tryConnect);
    };
    (*tryConnect)();
}

void MainWindow::disconnectQmp()
{
    m_qmp->disconnectFromServer();
}

void MainWindow::setOrientation(Orientation orientation)
{
    m_orientation = orientation;
    ui->orientationButton->setText(orientation == Orientation::Portrait
                                       ? tr("Portrait")
                                       : tr("Landscape"));
    applyOrientation();
}

void MainWindow::applyOrientation()
{
    // Reshape the viewport so the black screen area matches the requested
    // aspect ratio; the layout keeps it centred.
    const bool portrait = m_orientation == Orientation::Portrait;
    ui->screenFrame->setMinimumSize(portrait ? QSize(270, 480) : QSize(480, 270));
    ui->screenFrame->setMaximumSize(portrait ? QSize(1080, 1920) : QSize(1920, 1080));
}

void MainWindow::updateAdbIndicator(bool connected)
{
    m_adbConnected = connected;
    ui->adbIndicator->setProperty("state",
                                  connected ? QStringLiteral("online")
                                            : QStringLiteral("offline"));
    // Re-evaluate the stylesheet so the property selector takes effect.
    ui->adbIndicator->style()->unpolish(ui->adbIndicator);
    ui->adbIndicator->style()->polish(ui->adbIndicator);

    ui->adbStatusLabel->setText(connected
                                    ? tr("ADB: connected (:%1)").arg(kAdbPort)
                                    : tr("ADB: offline"));
}

void MainWindow::pollAdbStatus()
{
    // A single persistent socket is reused for every probe.  Creating a new
    // socket per poll risks the deferred-delete racing the timeout below.
    if (!m_adbProbe) {
        m_adbProbe = new QTcpSocket(this);
        connect(m_adbProbe, &QTcpSocket::connected, this, [this]() {
            updateAdbIndicator(true);
        });
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        connect(m_adbProbe, &QTcpSocket::errorOccurred, this, [this]() {
            updateAdbIndicator(false);
        });
#else
        connect(m_adbProbe,
                QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
                this, [this]() { updateAdbIndicator(false); });
#endif
    }

    // Never stack probes: a pending connect would otherwise be restarted
    // before it has a chance to resolve.
    if (m_adbProbe->state() != QAbstractSocket::UnconnectedState)
        return;

    m_adbProbe->connectToHost(QHostAddress::LocalHost, kAdbPort);
}

void MainWindow::applyStateToControls()
{
    const bool running = m_runner->isRunning();
    const bool qmpReady = m_qmp->isReady();

    ui->powerButton->setEnabled(!running);
    ui->shutdownButton->setEnabled(running);
    ui->resetButton->setEnabled(running);
    ui->volumeUpButton->setEnabled(qmpReady);
    ui->volumeDownButton->setEnabled(qmpReady);
}

void MainWindow::setStatusMessage(const QString &message)
{
    ui->statusBarLabel->setText(message);
    statusBar()->showMessage(message, 4000);
}

void MainWindow::appendLog(const QString &line)
{
    // Console logging keeps the UI minimal; a future revision can route this
    // into an in-window log pane.
    qInfo("[VAPlayer] %s", qUtf8Printable(line));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_runner->isRunning()) {
        const auto answer = QMessageBox::question(
            this, tr("Quit VAPlayer"),
            tr("The emulator is still running. Stop it and quit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }

        if (m_qmp->isReady())
            m_qmp->quit();
        m_runner->stop(2000);
    }

    m_adbTimer->stop();
    event->accept();
}
