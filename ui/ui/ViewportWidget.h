/*
 * VAPlayer - emulator viewport
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

#ifndef VAPLAYER_VIEWPORTWIDGET_H
#define VAPLAYER_VIEWPORTWIDGET_H

#include <QPixmap>
#include <QWidget>

class QLabel;
class KeymapperEngine;
class KeymapperOverlay;

/**
 * The emulator screen.
 *
 * Renders the guest frame (or a placeholder while it is stopped), accepts
 * files dropped onto it and hosts the keymapper overlay.  The widget is
 * deliberately self-contained so the drop routing can be exercised in tests.
 */
class ViewportWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget *parent = nullptr);

    /** Text shown while no guest frame is available. */
    void setPlaceholderText(const QString &text);

    /** Display a captured guest frame. */
    void setFrame(const QPixmap &frame);
    /** Drop the current frame and show the placeholder again. */
    void clearFrame();

    /** The keymapper overlay drawn on top of the screen. */
    KeymapperOverlay *overlay() const { return m_overlay; }

    /**
     * Route mouse input from the screen to the keymapper engine.  Without an
     * engine the widget ignores mouse events, so a plain viewport stays inert.
     */
    void setKeymapperEngine(KeymapperEngine *engine);
    KeymapperEngine *keymapperEngine() const { return m_engine; }

signals:
    /**
     * Emitted with the local paths of the files dropped on the viewport.
     * Non-file drops (text, URLs) are ignored.
     */
    void filesDropped(const QStringList &paths);
    /** Emitted when a drag carrying files enters or leaves the viewport. */
    void dragActiveChanged(bool active);

    /** Raw mouse press/release with *button* ("left"/"right"/"middle"). */
    void mousePressed(const QString &button);
    void mouseReleased(const QString &button);
    /** Relative mouse movement, forwarded while crosshair mode is active. */
    void mouseMoved(int dx, int dy);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    /** Local file paths carried by *mime*, ignoring anything else. */
    static QStringList localFilesFrom(const class QMimeData *mime);
    /** Qt button to the keymapper's button name, or an empty string. */
    static QString buttonName(Qt::MouseButton button);

    void syncOverlayGeometry();

    QLabel *m_placeholder = nullptr;
    KeymapperOverlay *m_overlay = nullptr;
    KeymapperEngine *m_engine = nullptr;
    QPixmap m_frame;
    bool m_dragActive = false;
    bool m_mouseDown = false;
    QPoint m_lastMousePos;
};

#endif // VAPLAYER_VIEWPORTWIDGET_H
