/*
 * VAPlayer - keymapper profile dialog
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

#include "ui/KeymapperDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
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
QLineEdit, QComboBox, QSpinBox, QListWidget {
    background-color: #1b1e24;
    border: 1px solid #2f343d;
    border-radius: 6px;
    padding: 5px 8px;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #2f7d4f; }
QComboBox::drop-down { border: none; width: 18px; }
QListWidget::item { padding: 4px 6px; }
QListWidget::item:selected { background-color: #2f7d4f; color: #ffffff; }
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

QComboBox *makeKeyCombo(QWidget *parent, bool allowEmpty = false)
{
    auto *combo = new QComboBox(parent);
    if (allowEmpty)
        combo->addItem(QObject::tr("(none)"), QString());
    for (const QString &key : Keymap::supportedKeys())
        combo->addItem(key, key);
    return combo;
}

void selectKey(QComboBox *combo, const QString &key)
{
    const int index = combo->findData(key);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

} // namespace

KeymapperDialog::KeymapperDialog(QWidget *parent)
    : QDialog(parent)
{
    buildUi();
    applyTheme();
    refreshProfileList();
    refreshFromKeymap();
}

void KeymapperDialog::buildUi()
{
    setWindowTitle(tr("Keymapper"));
    setMinimumSize(720, 620);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto *header = new QLabel(tr("Keymapper"), this);
    header->setObjectName(QStringLiteral("dialogHeader"));
    root->addWidget(header);

    auto *hint = new QLabel(
        tr("Bind keyboard keys and the mouse to on-screen touch controls. "
           "Arm a key, then click the emulator screen to place it."),
        this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    // Profile row.
    auto *profileGroup = new QGroupBox(tr("Profile"), this);
    auto *profileLayout = new QFormLayout(profileGroup);
    auto *profileRow = new QHBoxLayout();
    m_profileCombo = new QComboBox(profileGroup);
    m_loadButton = new QPushButton(tr("Load"), profileGroup);
    profileRow->addWidget(m_profileCombo, 1);
    profileRow->addWidget(m_loadButton);
    profileLayout->addRow(tr("Saved profiles"), profileRow);

    m_nameEdit = new QLineEdit(profileGroup);
    m_nameEdit->setPlaceholderText(tr("Profile name"));
    profileLayout->addRow(tr("Name"), m_nameEdit);

    m_packageEdit = new QLineEdit(profileGroup);
    m_packageEdit->setPlaceholderText(tr("com.example.game (optional)"));
    profileLayout->addRow(tr("Game package"), m_packageEdit);
    root->addWidget(profileGroup);

    // Tap points.
    auto *tapGroup = new QGroupBox(tr("Single tap points"), this);
    auto *tapLayout = new QVBoxLayout(tapGroup);
    m_tapList = new QListWidget(tapGroup);
    m_tapList->setMinimumHeight(110);
    tapLayout->addWidget(m_tapList);

    auto *tapRow = new QHBoxLayout();
    m_armCombo = makeKeyCombo(tapGroup, true);
    m_armCombo->setCurrentIndex(0);
    m_removeTapButton = new QPushButton(tr("Remove selected"), tapGroup);
    tapRow->addWidget(new QLabel(tr("Arm key:"), tapGroup));
    tapRow->addWidget(m_armCombo, 1);
    tapRow->addWidget(m_removeTapButton);
    tapLayout->addLayout(tapRow);
    root->addWidget(tapGroup);

    // D-pad.
    auto *dpadGroup = new QGroupBox(tr("D-Pad (WASD)"), this);
    auto *dpadLayout = new QFormLayout(dpadGroup);
    m_dpadCheck = new QCheckBox(tr("Enable D-pad"), dpadGroup);
    dpadLayout->addRow(m_dpadCheck);
    m_dpadUp = makeKeyCombo(dpadGroup);
    m_dpadDown = makeKeyCombo(dpadGroup);
    m_dpadLeft = makeKeyCombo(dpadGroup);
    m_dpadRight = makeKeyCombo(dpadGroup);
    dpadLayout->addRow(tr("Up"), m_dpadUp);
    dpadLayout->addRow(tr("Down"), m_dpadDown);
    dpadLayout->addRow(tr("Left"), m_dpadLeft);
    dpadLayout->addRow(tr("Right"), m_dpadRight);
    root->addWidget(dpadGroup);

    // Crosshair.
    auto *crossGroup = new QGroupBox(tr("Crosshair / shooting mode"), this);
    auto *crossLayout = new QFormLayout(crossGroup);
    m_crosshairCheck = new QCheckBox(tr("Enable crosshair mode"), crossGroup);
    crossLayout->addRow(m_crosshairCheck);
    m_crosshairKey = makeKeyCombo(crossGroup, true);
    crossLayout->addRow(tr("Toggle key"), m_crosshairKey);
    m_sensitivity = new QSpinBox(crossGroup);
    m_sensitivity->setRange(10, 500);
    m_sensitivity->setSuffix(QStringLiteral(" %"));
    crossLayout->addRow(tr("Sensitivity"), m_sensitivity);
    root->addWidget(crossGroup);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    auto *buttonRow = new QHBoxLayout();
    m_resetButton = new QPushButton(tr("Reset"), this);
    m_saveAsButton = new QPushButton(tr("Save as…"), this);
    m_saveButton = new QPushButton(tr("Save"), this);
    m_closeButton = new QPushButton(tr("Close"), this);
    m_closeButton->setDefault(true);
    buttonRow->addWidget(m_resetButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_saveAsButton);
    buttonRow->addWidget(m_saveButton);
    buttonRow->addWidget(m_closeButton);
    root->addLayout(buttonRow);

    connect(m_nameEdit, &QLineEdit::textChanged, this, &KeymapperDialog::onKeymapEdited);
    connect(m_packageEdit, &QLineEdit::textChanged, this, &KeymapperDialog::onKeymapEdited);
    connect(m_armCombo, &QComboBox::currentIndexChanged,
            this, &KeymapperDialog::onArmKeyChanged);
    connect(m_removeTapButton, &QPushButton::clicked,
            this, &KeymapperDialog::onRemoveTap);
    connect(m_dpadCheck, &QCheckBox::toggled, this, &KeymapperDialog::onDpadToggled);
    connect(m_dpadUp, &QComboBox::currentIndexChanged, this, &KeymapperDialog::onKeymapEdited);
    connect(m_dpadDown, &QComboBox::currentIndexChanged, this, &KeymapperDialog::onKeymapEdited);
    connect(m_dpadLeft, &QComboBox::currentIndexChanged, this, &KeymapperDialog::onKeymapEdited);
    connect(m_dpadRight, &QComboBox::currentIndexChanged, this, &KeymapperDialog::onKeymapEdited);
    connect(m_crosshairCheck, &QCheckBox::toggled,
            this, &KeymapperDialog::onCrosshairToggled);
    connect(m_crosshairKey, &QComboBox::currentIndexChanged, this, &KeymapperDialog::onKeymapEdited);
    connect(m_sensitivity, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &KeymapperDialog::onKeymapEdited);
    connect(m_loadButton, &QPushButton::clicked, this, &KeymapperDialog::onLoad);
    connect(m_saveButton, &QPushButton::clicked, this, &KeymapperDialog::onSave);
    connect(m_saveAsButton, &QPushButton::clicked, this, &KeymapperDialog::onSaveAs);
    connect(m_resetButton, &QPushButton::clicked, this, &KeymapperDialog::onReset);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void KeymapperDialog::applyTheme()
{
    setStyleSheet(QString::fromUtf8(kStyleSheet));
}

void KeymapperDialog::setKeymap(const Keymap &keymap)
{
    m_keymap = keymap;
    refreshFromKeymap();
}

void KeymapperDialog::refreshFromKeymap()
{
    m_updating = true;

    m_nameEdit->setText(m_keymap.name);
    m_packageEdit->setText(m_keymap.packageName);

    m_dpadCheck->setChecked(m_keymap.dpad.enabled);
    selectKey(m_dpadUp, m_keymap.dpad.up);
    selectKey(m_dpadDown, m_keymap.dpad.down);
    selectKey(m_dpadLeft, m_keymap.dpad.left);
    selectKey(m_dpadRight, m_keymap.dpad.right);

    m_crosshairCheck->setChecked(m_keymap.crosshair.enabled);
    selectKey(m_crosshairKey, m_keymap.crosshair.toggleKey);
    m_sensitivity->setValue(m_keymap.crosshair.sensitivity);

    refreshTapList();

    m_updating = false;
}

void KeymapperDialog::refreshTapList()
{
    m_tapList->clear();
    for (const KeyTap &tap : m_keymap.taps) {
        const QString label = tap.label.isEmpty() ? tap.key : tap.label;
        m_tapList->addItem(tr("%1  →  (%2, %3)")
                               .arg(label)
                               .arg(tap.x)
                               .arg(tap.y));
    }
}

void KeymapperDialog::refreshProfileList()
{
    m_profileCombo->clear();

    const QDir dir(Keymap::profilesDirectory());
    const QStringList files =
        dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name);
    for (const QString &file : files)
        m_profileCombo->addItem(file, dir.absoluteFilePath(file));

    // Always offer the shipped default, even before the directory exists.
    const QString fallback = Keymap::defaultProfilePath();
    if (QFileInfo(fallback).isFile()
        && m_profileCombo->findData(fallback) < 0)
        m_profileCombo->addItem(tr("default.json"), fallback);
}

void KeymapperDialog::onKeymapEdited()
{
    if (m_updating)
        return;

    m_keymap.name = m_nameEdit->text();
    m_keymap.packageName = m_packageEdit->text();
    m_keymap.dpad.enabled = m_dpadCheck->isChecked();
    m_keymap.dpad.up = m_dpadUp->currentData().toString();
    m_keymap.dpad.down = m_dpadDown->currentData().toString();
    m_keymap.dpad.left = m_dpadLeft->currentData().toString();
    m_keymap.dpad.right = m_dpadRight->currentData().toString();
    m_keymap.crosshair.enabled = m_crosshairCheck->isChecked();
    m_keymap.crosshair.toggleKey = m_crosshairKey->currentData().toString();
    m_keymap.crosshair.sensitivity = m_sensitivity->value();

    emit keymapChanged(m_keymap);
}

void KeymapperDialog::onDpadToggled()
{
    m_dpadUp->setEnabled(m_dpadCheck->isChecked());
    m_dpadDown->setEnabled(m_dpadCheck->isChecked());
    m_dpadLeft->setEnabled(m_dpadCheck->isChecked());
    m_dpadRight->setEnabled(m_dpadCheck->isChecked());
    onKeymapEdited();
}

void KeymapperDialog::onCrosshairToggled()
{
    m_crosshairKey->setEnabled(m_crosshairCheck->isChecked());
    m_sensitivity->setEnabled(m_crosshairCheck->isChecked());
    onKeymapEdited();
}

void KeymapperDialog::onArmKeyChanged()
{
    emit armKeyChanged(armedKey());
}

QString KeymapperDialog::armedKey() const
{
    return m_armCombo->currentData().toString();
}

void KeymapperDialog::onRemoveTap()
{
    const int row = m_tapList->currentRow();
    if (row < 0 || row >= m_keymap.taps.size())
        return;
    m_keymap.taps.removeAt(row);
    refreshTapList();
    emit keymapChanged(m_keymap);
}

void KeymapperDialog::onLoad()
{
    const QString path = m_profileCombo->currentData().toString();
    if (path.isEmpty()) {
        setStatus(tr("No profile selected."), true);
        return;
    }

    bool ok = false;
    QString error;
    const Keymap loaded = Keymap::load(path, &ok, &error);
    if (!ok) {
        setStatus(tr("Could not load %1: %2").arg(path, error), true);
        return;
    }

    m_keymap = loaded;
    m_currentProfile = path;
    refreshFromKeymap();
    emit keymapChanged(m_keymap);
    setStatus(tr("Loaded %1.").arg(QFileInfo(path).fileName()));
}

void KeymapperDialog::onSave()
{
    QString path = m_currentProfile;
    if (path.isEmpty()) {
        // Fall back to the profile directory, named after the package or name.
        const QString stem = !m_keymap.packageName.isEmpty()
                                 ? m_keymap.packageName
                                 : (m_keymap.name.isEmpty()
                                        ? QStringLiteral("default")
                                        : m_keymap.name);
        path = Keymap::profilePathFor(stem);
    }

    QString error;
    if (!m_keymap.save(path, &error)) {
        setStatus(tr("Could not save: %1").arg(error), true);
        return;
    }

    m_currentProfile = path;
    refreshProfileList();
    emit profileSaved(m_keymap, path);
    setStatus(tr("Saved %1.").arg(QFileInfo(path).fileName()));
}

void KeymapperDialog::onSaveAs()
{
    const QString suggested =
        m_currentProfile.isEmpty() ? Keymap::profilePathFor(QStringLiteral("default"))
                                   : m_currentProfile;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save profile as"), suggested,
        tr("Keymapper profiles (*.json)"));
    if (path.isEmpty())
        return;

    m_currentProfile = path;
    onSave();
}

void KeymapperDialog::onReset()
{
    m_keymap = Keymap();
    m_keymap.name = m_nameEdit->text();
    m_currentProfile.clear();
    refreshFromKeymap();
    emit keymapChanged(m_keymap);
    setStatus(tr("Profile cleared."));
}

void KeymapperDialog::setStatus(const QString &text, bool error)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color: #e0796b;")
                                       : QString());
}
