/*
 * VAPlayer - keymapper engine
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

#ifndef VAPLAYER_KEYMAPPERENGINE_H
#define VAPLAYER_KEYMAPPERENGINE_H

#include <QObject>
#include <QSet>

#include "keymapper/Keymap.h"

class QmpClient;

/**
 * Translates host keyboard and mouse events into guest touch input according
 * to a Keymap profile.
 *
 * The engine is deliberately free of any widget dependency so it can be unit
 * tested: it only needs a QmpClient to talk to the guest.  A KeymapperOverlay
 * drives it from the viewport's mouse and key events.
 */
class KeymapperEngine : public QObject
{
    Q_OBJECT

public:
    explicit KeymapperEngine(QObject *parent = nullptr);

    void setQmpClient(QmpClient *client);

    /** Install a profile and clear any transient key state. */
    void setKeymap(const Keymap &keymap);
    Keymap keymap() const { return m_keymap; }

    /** Whether synthetic input is currently forwarded to the guest. */
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    /** True while crosshair (cursor-locked) mode is active. */
    bool isCrosshairActive() const { return m_crosshairActive; }

    /**
     * Feed a key press/release.  *qcode* is a QEMU qcode (see
     * Keymap::qcodeForQtKey).  Returns true when the event was consumed by a
     * mapping, false when the caller should handle it normally.
     */
    bool handleKeyPress(const QString &qcode);
    bool handleKeyRelease(const QString &qcode);

    /** Feed a mouse press/release with *button* ("left"/"right"/"middle"). */
    bool handleMousePress(const QString &button);
    bool handleMouseRelease(const QString &button);

    /**
     * Feed a relative mouse move while crosshair mode is active.  Deltas are
     * scaled by the profile sensitivity and forwarded to the guest.
     */
    void handleMouseMove(int dx, int dy);

    /** Leave crosshair mode (e.g. when the overlay loses focus). */
    void releaseCrosshair();

signals:
    void crosshairChanged(bool active);
    /** Emitted for status messages the overlay can show. */
    void statusMessage(const QString &message);

private:
    /** Normalised D-pad centre plus the travel vector for held directions. */
    void updateDpad();

    Keymap m_keymap;
    QmpClient *m_qmp = nullptr;

    bool m_enabled = false;
    bool m_crosshairActive = false;

    QSet<QString> m_pressed;    ///< Currently held qcodes.
};

#endif // VAPLAYER_KEYMAPPERENGINE_H
