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

#ifndef VAPLAYER_KEYMAPPEROVERLAY_H
#define VAPLAYER_KEYMAPPEROVERLAY_H

#include <QWidget>

#include "keymapper/Keymap.h"

class KeymapperEngine;

/**
 * Transparent widget drawn on top of the emulator viewport.
 *
 * In *mapping* mode it visualises the profile and lets the user place and drag
 * control points with the mouse: the currently armed key (chosen in the
 * sidebar) is dropped wherever the user clicks.  In *play* mode it is click
 * through — the engine consumes keyboard input and the overlay only renders
 * the crosshair hint.
 */
class KeymapperOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit KeymapperOverlay(QWidget *parent = nullptr);

    void setEngine(KeymapperEngine *engine);

    /** Replace the rendered profile. */
    void setKeymap(const Keymap &keymap);
    Keymap keymap() const { return m_keymap; }

    /** Show the profile and accept placement clicks. */
    bool isMappingMode() const { return m_mappingMode; }
    void setMappingMode(bool mapping);

    /** Key that the next placement click binds ("space", "mouse-left", ...). */
    void setArmedKey(const QString &key);
    QString armedKey() const { return m_armedKey; }

    /** Index of the selected tap point, or -1. */
    int selectedTap() const { return m_selectedTap; }

    // -- Coordinate conversion (static so they are directly testable) -----

    /** Pixel coordinate (0..extent-1) to QEMU's 0..0x7fff range. */
    static int toNormalised(int pixel, int extent);
    /** QEMU's 0..0x7fff range back to a pixel coordinate. */
    static int toPixel(int normalised, int extent);

public slots:
    /** Remove the selected tap point, if any. */
    void removeSelectedTap();

signals:
    /** Emitted whenever the profile is modified by the user. */
    void keymapChanged(const Keymap &keymap);
    void statusMessage(const QString &message);
    /** Emitted when the selected tap point changes (-1 when none). */
    void selectionChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    /** Index of the tap point under *pos*, or -1. */
    int tapAt(const QPoint &pos) const;
    /** D-pad centre in widget coordinates. */
    QPoint dpadCenterPixel() const;
    int dpadRadiusPixel() const;

    Keymap m_keymap;
    KeymapperEngine *m_engine = nullptr;

    bool m_mappingMode = false;
    QString m_armedKey;
    int m_selectedTap = -1;
    int m_draggingTap = -1;
    bool m_draggingDpad = false;
};

#endif // VAPLAYER_KEYMAPPEROVERLAY_H
