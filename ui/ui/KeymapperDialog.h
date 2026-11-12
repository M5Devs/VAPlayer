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

#ifndef VAPLAYER_KEYMAPPERDIALOG_H
#define VAPLAYER_KEYMAPPERDIALOG_H

#include <QDialog>

#include "keymapper/Keymap.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

/**
 * Editor for keymapper profiles.
 *
 * Edits a Keymap in place: single tap points, the D-pad bindings and the
 * crosshair settings.  Placement of tap points happens on the overlay (the
 * dialog exposes the armed key); this dialog handles naming, the key bindings
 * and loading/saving the JSON profile.
 */
class KeymapperDialog : public QDialog
{
    Q_OBJECT

public:
    explicit KeymapperDialog(QWidget *parent = nullptr);

    void setKeymap(const Keymap &keymap);
    Keymap keymap() const { return m_keymap; }

    /** Key currently armed for placement on the overlay. */
    QString armedKey() const;

    /** Path of the profile most recently loaded or saved. */
    QString currentProfilePath() const { return m_currentProfile; }

signals:
    /** The edited profile changed and should be applied live. */
    void keymapChanged(const Keymap &keymap);
    /** The key armed for the next placement click changed. */
    void armKeyChanged(const QString &key);
    /** Emitted on Save so the caller can persist the profile. */
    void profileSaved(const Keymap &keymap, const QString &path);

private slots:
    void onKeymapEdited();
    void onDpadToggled();
    void onCrosshairToggled();
    void onArmKeyChanged();
    void onRemoveTap();
    void onLoad();
    void onSave();
    void onSaveAs();
    void onReset();

private:
    void buildUi();
    void applyTheme();
    void refreshTapList();
    void refreshFromKeymap();
    void refreshProfileList();
    void setStatus(const QString &text, bool error = false);

    Keymap m_keymap;
    QString m_currentProfile;

    QComboBox *m_profileCombo = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_packageEdit = nullptr;
    QComboBox *m_armCombo = nullptr;
    QListWidget *m_tapList = nullptr;
    QPushButton *m_removeTapButton = nullptr;

    QCheckBox *m_dpadCheck = nullptr;
    QComboBox *m_dpadUp = nullptr;
    QComboBox *m_dpadDown = nullptr;
    QComboBox *m_dpadLeft = nullptr;
    QComboBox *m_dpadRight = nullptr;

    QCheckBox *m_crosshairCheck = nullptr;
    QComboBox *m_crosshairKey = nullptr;
    QSpinBox *m_sensitivity = nullptr;

    QPushButton *m_loadButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_saveAsButton = nullptr;
    QPushButton *m_resetButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QLabel *m_statusLabel = nullptr;

    bool m_updating = false;
};

#endif // VAPLAYER_KEYMAPPERDIALOG_H
