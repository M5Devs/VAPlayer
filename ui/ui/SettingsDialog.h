/*
 * VAPlayer - settings dialog
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

#ifndef VAPLAYER_SETTINGSDIALOG_H
#define VAPLAYER_SETTINGSDIALOG_H

#include <QDialog>

#include "process/EmulatorRunner.h"

QT_BEGIN_NAMESPACE
namespace Ui { class SettingsDialog; }
QT_END_NAMESPACE

/**
 * Modal dialog for editing CPU and RAM allocation and the set of VirtIO
 * devices attached to the guest.
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

    /** Load the dialog fields from a configuration. */
    void setConfig(const EmulatorConfig &config);

    /** Read the edited values back out. */
    EmulatorConfig config() const;

private slots:
    void onRamSliderMoved(int value);
    void onRamSpinChanged(int value);
    void onAccepted();
    void onRejected();

private:
    Ui::SettingsDialog *ui = nullptr;
    EmulatorConfig m_config;
};

#endif // VAPLAYER_SETTINGSDIALOG_H
