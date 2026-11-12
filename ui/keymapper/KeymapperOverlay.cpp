/*
 * VAPlayer - keymapper overlay
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

#include "keymapper/KeymapperOverlay.h"

#include "keymapper/KeymapperEngine.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

namespace {

/** Radius in pixels within which a click grabs an existing control point. */
constexpr int kGrabRadius = 18;
constexpr int kTapMarkerRadius = 14;
constexpr int kDpadHandleRadius = 10;

QColor kAccent(0x3f, 0xb9, 0x6a);
QColor kAccentSoft(0x3f, 0xb9, 0x6a, 0x55);
QColor kOutline(0xea, 0xf2, 0xec, 0xcc);
QColor kLabel(0xf2, 0xf4, 0xf8);

} // namespace

KeymapperOverlay::KeymapperOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::StrongFocus);
}

void KeymapperOverlay::setEngine(KeymapperEngine *engine)
{
    m_engine = engine;
}

void KeymapperOverlay::setKeymap(const Keymap &keymap)
{
    m_keymap = keymap;
    m_selectedTap = -1;
    emit selectionChanged(-1);
    update();
}

void KeymapperOverlay::setMappingMode(bool mapping)
{
    if (m_mappingMode == mapping)
        return;
    m_mappingMode = mapping;
    // Only mapping mode intercepts mouse input; during play the clicks must
    // reach the emulator viewport underneath.
    setAttribute(Qt::WA_TransparentForMouseEvents, !mapping);
    if (mapping)
        setCursor(Qt::CrossCursor);
    else
        unsetCursor();
    update();
}

void KeymapperOverlay::setArmedKey(const QString &key)
{
    m_armedKey = key;
    emit statusMessage(m_armedKey.isEmpty()
                           ? tr("Pick a key, then click the screen to place it.")
                           : tr("Click the screen to place “%1”.").arg(m_armedKey));
    update();
}

int KeymapperOverlay::toNormalised(int pixel, int extent)
{
    if (extent <= 1)
        return 0;
    const int clamped = qBound(0, pixel, extent - 1);
    return static_cast<int>(
        (static_cast<qint64>(clamped) * 0x7fff) / (extent - 1));
}

int KeymapperOverlay::toPixel(int normalised, int extent)
{
    if (extent <= 1)
        return 0;
    const int clamped = qBound(0, normalised, 0x7fff);
    return static_cast<int>(
        (static_cast<qint64>(clamped) * (extent - 1)) / 0x7fff);
}

int KeymapperOverlay::tapAt(const QPoint &pos) const
{
    for (int i = 0; i < m_keymap.taps.size(); ++i) {
        const KeyTap &tap = m_keymap.taps.at(i);
        const QPoint centre(toPixel(tap.x, width()), toPixel(tap.y, height()));
        if ((centre - pos).manhattanLength() <= kGrabRadius * 2)
            return i;
    }
    return -1;
}

QPoint KeymapperOverlay::dpadCenterPixel() const
{
    return QPoint(toPixel(m_keymap.dpad.centerX, width()),
                  toPixel(m_keymap.dpad.centerY, height()));
}

int KeymapperOverlay::dpadRadiusPixel() const
{
    const int horizontal = toPixel(m_keymap.dpad.radius, width());
    return qMax(24, horizontal);
}

void KeymapperOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (!m_mappingMode && !m_keymap.crosshair.enabled)
        return;

    // D-pad.
    if (m_keymap.dpad.enabled) {
        const QPoint centre = dpadCenterPixel();
        const int radius = dpadRadiusPixel();
        painter.setPen(QPen(kOutline, 2));
        painter.setBrush(kAccentSoft);
        painter.drawEllipse(centre, radius, radius);

        painter.setBrush(kAccent);
        painter.drawEllipse(centre, kDpadHandleRadius, kDpadHandleRadius);

        const QStringList directions {
            m_keymap.dpad.up, m_keymap.dpad.down,
            m_keymap.dpad.left, m_keymap.dpad.right,
        };
        const QPoint offsets[] = {
            QPoint(0, -radius), QPoint(0, radius),
            QPoint(-radius, 0), QPoint(radius, 0),
        };
        painter.setPen(Qt::NoPen);
        for (int i = 0; i < directions.size(); ++i) {
            const QPoint handle = centre + offsets[i];
            painter.setBrush(directions.at(i).isEmpty() ? QColor(0x5a, 0x62, 0x6e)
                                                        : kAccent);
            painter.drawEllipse(handle, kDpadHandleRadius, kDpadHandleRadius);
        }

        if (m_mappingMode) {
            painter.setPen(kLabel);
            painter.drawText(centre + QPoint(radius + 8, 4), tr("D-Pad"));
        }
    }

    // Tap points.
    for (int i = 0; i < m_keymap.taps.size(); ++i) {
        const KeyTap &tap = m_keymap.taps.at(i);
        const QPoint centre(toPixel(tap.x, width()), toPixel(tap.y, height()));

        const bool selected = (i == m_selectedTap);
        painter.setPen(QPen(selected ? kLabel : kOutline, selected ? 3 : 2));
        painter.setBrush(kAccentSoft);
        painter.drawEllipse(centre, kTapMarkerRadius, kTapMarkerRadius);

        painter.setPen(kLabel);
        const QString text = tap.label.isEmpty() ? tap.key : tap.label;
        painter.drawText(centre + QPoint(kTapMarkerRadius + 6, 5), text);
    }

    // Crosshair reticle.
    if (m_keymap.crosshair.enabled) {
        const QPoint centre(width() / 2, height() / 2);
        painter.setPen(QPen(kOutline, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(centre, 26, 26);
        painter.drawLine(centre + QPoint(-34, 0), centre + QPoint(-8, 0));
        painter.drawLine(centre + QPoint(8, 0), centre + QPoint(34, 0));
        painter.drawLine(centre + QPoint(0, -34), centre + QPoint(0, -8));
        painter.drawLine(centre + QPoint(0, 8), centre + QPoint(0, 34));
    }

    if (m_mappingMode) {
        painter.setPen(kLabel);
        painter.drawText(rect().adjusted(12, 12, -12, -12),
                         Qt::AlignTop | Qt::AlignLeft,
                         tr("Mapping mode — click to place, drag to move, "
                            "Delete to remove."));
    }
}

void KeymapperOverlay::mousePressEvent(QMouseEvent *event)
{
    if (!m_mappingMode || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    // Grab an existing tap point.
    const int index = tapAt(event->pos());
    if (index >= 0) {
        m_selectedTap = index;
        m_draggingTap = index;
        emit selectionChanged(index);
        update();
        return;
    }

    // Grab the D-pad centre.
    if (m_keymap.dpad.enabled
        && (dpadCenterPixel() - event->pos()).manhattanLength() <= kGrabRadius * 2) {
        m_draggingDpad = true;
        return;
    }

    // Otherwise place the armed key as a new tap point.
    if (!m_armedKey.isEmpty()) {
        KeyTap tap;
        tap.key = m_armedKey;
        tap.x = toNormalised(event->pos().x(), width());
        tap.y = toNormalised(event->pos().y(), height());
        m_keymap.taps.append(tap);
        m_selectedTap = m_keymap.taps.size() - 1;
        m_draggingTap = m_selectedTap;
        emit selectionChanged(m_selectedTap);
        emit keymapChanged(m_keymap);
        update();
    }
}

void KeymapperOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_mappingMode) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_draggingTap >= 0 && m_draggingTap < m_keymap.taps.size()) {
        KeyTap &tap = m_keymap.taps[m_draggingTap];
        tap.x = toNormalised(event->pos().x(), width());
        tap.y = toNormalised(event->pos().y(), height());
        emit keymapChanged(m_keymap);
        update();
    } else if (m_draggingDpad) {
        m_keymap.dpad.centerX = toNormalised(event->pos().x(), width());
        m_keymap.dpad.centerY = toNormalised(event->pos().y(), height());
        emit keymapChanged(m_keymap);
        update();
    }
}

void KeymapperOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_draggingTap = -1;
        m_draggingDpad = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void KeymapperOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}

void KeymapperOverlay::removeSelectedTap()
{
    if (m_selectedTap < 0 || m_selectedTap >= m_keymap.taps.size())
        return;
    m_keymap.taps.removeAt(m_selectedTap);
    m_selectedTap = -1;
    m_draggingTap = -1;
    emit selectionChanged(-1);
    emit keymapChanged(m_keymap);
    update();
}
