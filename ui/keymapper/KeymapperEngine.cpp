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

#include "keymapper/KeymapperEngine.h"

#include "core_bridge/QmpClient.h"

namespace {

/** Qt::Key values for the mouse pseudo-keys used by the crosshair toggle. */
constexpr char kMouseLeft[] = "mouse-left";
constexpr char kMouseRight[] = "mouse-right";
constexpr char kMouseMiddle[] = "mouse-middle";

bool isMouseKey(const QString &key)
{
    return key == QLatin1String(kMouseLeft)
           || key == QLatin1String(kMouseRight)
           || key == QLatin1String(kMouseMiddle);
}

} // namespace

KeymapperEngine::KeymapperEngine(QObject *parent)
    : QObject(parent)
{
}

void KeymapperEngine::setQmpClient(QmpClient *client)
{
    m_qmp = client;
}

void KeymapperEngine::setKeymap(const Keymap &keymap)
{
    m_keymap = keymap;
    m_pressed.clear();
    // A new profile may not define a crosshair; drop out of the mode.
    if (m_crosshairActive && !m_keymap.crosshair.enabled)
        releaseCrosshair();
}

void KeymapperEngine::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    if (!m_enabled) {
        m_pressed.clear();
        releaseCrosshair();
    }
}

bool KeymapperEngine::handleKeyPress(const QString &qcode)
{
    if (!m_enabled || !m_qmp)
        return false;

    const QString key = qcode.trimmed().toLower();
    if (key.isEmpty())
        return false;

    // Crosshair toggle.
    if (m_keymap.crosshair.enabled
        && !m_keymap.crosshair.toggleKey.isEmpty()
        && key == m_keymap.crosshair.toggleKey.toLower()) {
        if (m_crosshairActive)
            releaseCrosshair();
        else {
            m_crosshairActive = true;
            emit crosshairChanged(true);
            emit statusMessage(tr("Crosshair mode active — press the toggle key to exit."));
        }
        return true;
    }

    // Single tap points.
    for (const KeyTap &tap : m_keymap.taps) {
        if (tap.key.toLower() != key)
            continue;
        if (isMouseKey(key))
            m_qmp->sendMouseButton(key.mid(6), true);   // strip "mouse-"
        else
            m_qmp->sendTap(tap.x, tap.y);
        m_pressed.insert(key);
        return true;
    }

    // D-pad directions.
    if (m_keymap.dpad.enabled) {
        const bool isDirection =
            key == m_keymap.dpad.up.toLower()
            || key == m_keymap.dpad.down.toLower()
            || key == m_keymap.dpad.left.toLower()
            || key == m_keymap.dpad.right.toLower();
        if (isDirection) {
            const bool wasIdle = m_pressed.isEmpty();
            m_pressed.insert(key);
            // The first direction re-anchors the contact at the stick centre
            // so a held combination always drags from the origin.
            if (wasIdle)
                m_qmp->sendTouchDown(m_keymap.dpad.centerX, m_keymap.dpad.centerY);
            updateDpad();
            return true;
        }
    }

    return false;
}

bool KeymapperEngine::handleKeyRelease(const QString &qcode)
{
    if (!m_enabled || !m_qmp)
        return false;

    const QString key = qcode.trimmed().toLower();
    if (!m_pressed.contains(key))
        return false;

    m_pressed.remove(key);

    if (m_keymap.dpad.enabled) {
        const bool isDirection =
            key == m_keymap.dpad.up.toLower()
            || key == m_keymap.dpad.down.toLower()
            || key == m_keymap.dpad.left.toLower()
            || key == m_keymap.dpad.right.toLower();
        if (isDirection) {
            // Re-anchor at the centre when the last direction is released so
            // the next press starts from a known origin.
            if (m_pressed.isEmpty()) {
                m_qmp->sendTouchMove(m_keymap.dpad.centerX, m_keymap.dpad.centerY);
                m_qmp->sendTouchUp();
            } else {
                updateDpad();
            }
            return true;
        }
    }

    // A released tap point lifts the synthetic touch.
    for (const KeyTap &tap : m_keymap.taps) {
        if (tap.key.toLower() != key)
            continue;
        if (isMouseKey(key))
            m_qmp->sendMouseButton(key.mid(6), false);
        else
            m_qmp->sendMouseButton(QStringLiteral("touch"), false);
        return true;
    }

    return false;
}

bool KeymapperEngine::handleMousePress(const QString &button)
{
    if (!m_enabled || !m_qmp)
        return false;

    // A right-click toggles crosshair mode when the profile enables it, which
    // mirrors the conventional "aim" binding.
    if (m_keymap.crosshair.enabled
        && m_keymap.crosshair.toggleKey.toLower() == QStringLiteral("mouse-") + button) {
        if (m_crosshairActive)
            releaseCrosshair();
        else {
            m_crosshairActive = true;
            emit crosshairChanged(true);
            emit statusMessage(tr("Crosshair mode active."));
        }
        return true;
    }

    m_qmp->sendMouseButton(button, true);
    return true;
}

bool KeymapperEngine::handleMouseRelease(const QString &button)
{
    if (!m_enabled || !m_qmp)
        return false;

    if (m_crosshairActive
        && m_keymap.crosshair.toggleKey.toLower()
               == QStringLiteral("mouse-") + button)
        return true;    // Consumed by the toggle; no guest button to release.

    m_qmp->sendMouseButton(button, false);
    return true;
}

void KeymapperEngine::handleMouseMove(int dx, int dy)
{
    if (!m_enabled || !m_qmp || !m_crosshairActive)
        return;
    if (dx == 0 && dy == 0)
        return;

    const int scale = qMax(1, m_keymap.crosshair.sensitivity);
    m_qmp->sendMouseDelta(dx * scale / 100, dy * scale / 100);
}

void KeymapperEngine::releaseCrosshair()
{
    if (!m_crosshairActive)
        return;
    m_crosshairActive = false;
    emit crosshairChanged(false);
}

void KeymapperEngine::updateDpad()
{
    const KeyDpad &dpad = m_keymap.dpad;

    // Sum the held directions into a unit-ish vector, then scale by radius.
    int dx = 0;
    int dy = 0;
    if (m_pressed.contains(dpad.left.toLower()))
        dx -= 1;
    if (m_pressed.contains(dpad.right.toLower()))
        dx += 1;
    if (m_pressed.contains(dpad.up.toLower()))
        dy -= 1;
    if (m_pressed.contains(dpad.down.toLower()))
        dy += 1;

    const int x = qBound(0, dpad.centerX + dx * dpad.radius, 0x7fff);
    const int y = qBound(0, dpad.centerY + dy * dpad.radius, 0x7fff);

    // The contact is already down (see handleKeyPress); just steer it.  When
    // opposing keys cancel out the stick snaps back to its origin.
    m_qmp->sendTouchMove(x, y);
}
