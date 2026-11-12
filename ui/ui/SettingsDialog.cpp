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

#include "ui/SettingsDialog.h"
#include "ui/ui_SettingsDialog.h"

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);

    connect(ui->ramSlider, &QSlider::valueChanged,
            this, &SettingsDialog::onRamSliderMoved);
    connect(ui->ramSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::onRamSpinChanged);
    connect(ui->applyButton, &QPushButton::clicked,
            this, &SettingsDialog::onAccepted);
    connect(ui->cancelButton, &QPushButton::clicked,
            this, &SettingsDialog::onRejected);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::setConfig(const EmulatorConfig &config)
{
    m_config = config;

    ui->cpuCoresSpinBox->setValue(qBound(1, config.cpuCores, 16));

    // RAM is constrained to the range supported by the spin box.
    const int ram = qBound(ui->ramSpinBox->minimum(),
                           config.ramMb,
                           ui->ramSpinBox->maximum());
    ui->ramSpinBox->setValue(ram);
    ui->ramSlider->setValue(ram);

    ui->gpuCheckBox->setChecked(config.enableGpu);
    ui->soundCheckBox->setChecked(config.enableSound);
    ui->networkCheckBox->setChecked(config.enableNetwork);
    ui->tabletCheckBox->setChecked(config.enableTablet);
}

EmulatorConfig SettingsDialog::config() const
{
    EmulatorConfig result = m_config;
    result.cpuCores = ui->cpuCoresSpinBox->value();
    result.ramMb = ui->ramSpinBox->value();
    result.enableGpu = ui->gpuCheckBox->isChecked();
    result.enableSound = ui->soundCheckBox->isChecked();
    result.enableNetwork = ui->networkCheckBox->isChecked();
    result.enableTablet = ui->tabletCheckBox->isChecked();
    return result;
}

void SettingsDialog::onRamSliderMoved(int value)
{
    // Keep the spin box and slider in lock-step without recursion.
    if (ui->ramSpinBox->value() != value) {
        const QSignalBlocker blocker(ui->ramSpinBox);
        ui->ramSpinBox->setValue(value);
    }
}

void SettingsDialog::onRamSpinChanged(int value)
{
    if (ui->ramSlider->value() != value) {
        const QSignalBlocker blocker(ui->ramSlider);
        ui->ramSlider->setValue(value);
    }
}

void SettingsDialog::onAccepted()
{
    accept();
}

void SettingsDialog::onRejected()
{
    reject();
}
