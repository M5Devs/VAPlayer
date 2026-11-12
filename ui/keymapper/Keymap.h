/*
 * VAPlayer - keymapper profile model
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

#ifndef VAPLAYER_KEYMAP_H
#define VAPLAYER_KEYMAP_H

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

/**
 * A keyboard key bound to a single point on the screen.
 *
 * Coordinates are normalised to QEMU's absolute input range (0..0x7fff) so a
 * profile is independent of the current viewport size.
 */
struct KeyTap
{
    QString key;        ///< QEMU qcode, e.g. "space" or "mouse-left".
    int x = 0x4000;     ///< Horizontal position, 0..0x7fff.
    int y = 0x4000;     ///< Vertical position, 0..0x7fff.
    QString label;      ///< Optional label drawn on the overlay.

    bool operator==(const KeyTap &other) const
    {
        return key == other.key && x == other.x && y == other.y
               && label == other.label;
    }
};

/**
 * Virtual D-pad driven by four keys.  Holding a key drags a synthetic touch
 * from the stick origin towards the matching edge, which is how touch games
 * expect movement input.
 */
struct KeyDpad
{
    bool enabled = false;
    QString up;         ///< QEMU qcode for "up".
    QString down;
    QString left;
    QString right;
    int centerX = 0x4000;   ///< Stick origin, 0..0x7fff.
    int centerY = 0x5000;
    int radius = 0x1800;    ///< Travel radius, normalised units.

    bool operator==(const KeyDpad &other) const
    {
        return enabled == other.enabled && up == other.up && down == other.down
               && left == other.left && right == other.right
               && centerX == other.centerX && centerY == other.centerY
               && radius == other.radius;
    }
};

/**
 * Crosshair / shooting mode: the host cursor is locked and its deltas are
 * forwarded to the guest as relative mouse motion.
 */
struct KeyCrosshair
{
    bool enabled = false;
    QString toggleKey;      ///< qcode ("grave") or pseudo-key ("mouse-right").
    int sensitivity = 100;  ///< Percentage multiplier for the deltas.

    bool operator==(const KeyCrosshair &other) const
    {
        return enabled == other.enabled && toggleKey == other.toggleKey
               && sensitivity == other.sensitivity;
    }
};

/**
 * A complete keymapping profile, serialised to JSON so profiles can be shared
 * per game (``profiles/pubg.json``) or kept as a default.
 */
class Keymap
{
public:
    QString name;           ///< Human readable profile name.
    QString packageName;    ///< Target Android package (optional).
    QList<KeyTap> taps;
    KeyDpad dpad;
    KeyCrosshair crosshair;

    QJsonObject toJson() const;

    /**
     * Parse a profile.  Unknown keys are ignored and missing fields fall back
     * to their defaults, so older profiles keep loading.
     */
    static Keymap fromJson(const QJsonObject &object, bool *ok = nullptr);

    bool save(const QString &filePath, QString *error = nullptr) const;
    static Keymap load(const QString &filePath, bool *ok = nullptr,
                       QString *error = nullptr);

    /** True when the profile carries no active mapping at all. */
    bool isEmpty() const;

    /** Directory holding user profiles (``<app data>/profiles``). */
    static QString profilesDirectory();
    /** Path of the shipped fallback profile (``profiles/default.json``). */
    static QString defaultProfilePath();
    /** Profile path for a game, sanitised from its package name. */
    static QString profilePathFor(const QString &packageName);

    /**
     * Every qcode the keymapper accepts, in menu order.  Used by the editor
     * combo boxes and by the profile validation test.
     */
    static QStringList supportedKeys();

    /** True when *key* is a recognised qcode or mouse pseudo-key. */
    static bool isSupportedKey(const QString &key);

    /** QEMU qcode for a Qt key press, or an empty string when unmapped. */
    static QString qcodeForQtKey(int qtKey);

    bool operator==(const Keymap &other) const
    {
        return name == other.name && packageName == other.packageName
               && taps == other.taps && dpad == other.dpad
               && crosshair == other.crosshair;
    }
};

#endif // VAPLAYER_KEYMAP_H
