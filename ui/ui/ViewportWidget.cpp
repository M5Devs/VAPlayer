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

#include "ui/ViewportWidget.h"

#include "keymapper/KeymapperOverlay.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

ViewportWidget::ViewportWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("screenFrame"));
    setAcceptDrops(true);
    setMinimumSize(270, 480);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_placeholder = new QLabel(this);
    m_placeholder->setObjectName(QStringLiteral("screenLabel"));
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setText(tr("No signal\n\nPress “Power On” to start the emulator."));
    m_placeholder->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(m_placeholder);

    m_overlay = new KeymapperOverlay(this);
    m_overlay->setMappingMode(false);
    m_overlay->raise();

    setMouseTracking(true);
}

void ViewportWidget::setKeymapperEngine(KeymapperEngine *engine)
{
    m_engine = engine;
}

QString ViewportWidget::buttonName(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return QStringLiteral("left");
    case Qt::RightButton:
        return QStringLiteral("right");
    case Qt::MiddleButton:
        return QStringLiteral("middle");
    default:
        return QString();
    }
}

void ViewportWidget::setPlaceholderText(const QString &text)
{
    m_placeholder->setText(text);
}

void ViewportWidget::setFrame(const QPixmap &frame)
{
    m_frame = frame;
    m_placeholder->setVisible(frame.isNull());
    update();
}

void ViewportWidget::clearFrame()
{
    m_frame = QPixmap();
    m_placeholder->setVisible(true);
    update();
}

QStringList ViewportWidget::localFilesFrom(const QMimeData *mime)
{
    if (!mime || !mime->hasUrls())
        return QStringList();

    QStringList paths;
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &url : urls) {
        // Only real local files; remote URLs and directories are ignored.
        if (url.isLocalFile()) {
            const QString path = url.toLocalFile();
            if (!path.isEmpty())
                paths << path;
        }
    }
    return paths;
}

void ViewportWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (localFilesFrom(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    m_dragActive = true;
    emit dragActiveChanged(true);
    event->acceptProposedAction();
    update();
}

void ViewportWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (localFilesFrom(event->mimeData()).isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
}

void ViewportWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event);
    m_dragActive = false;
    emit dragActiveChanged(false);
    update();
}

void ViewportWidget::dropEvent(QDropEvent *event)
{
    const QStringList paths = localFilesFrom(event->mimeData());
    m_dragActive = false;
    emit dragActiveChanged(false);
    update();

    if (paths.isEmpty()) {
        event->ignore();
        return;
    }

    event->acceptProposedAction();
    emit filesDropped(paths);
}

void ViewportWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    if (!m_frame.isNull()) {
        // Scale the guest frame to fit while preserving its aspect ratio.
        const QPixmap scaled = m_frame.scaled(size(), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation);
        const QPoint topLeft((width() - scaled.width()) / 2,
                             (height() - scaled.height()) / 2);
        painter.drawPixmap(topLeft, scaled);
    }

    if (m_dragActive) {
        // A dashed accent border signals that the viewport accepts the drop.
        QPen pen(QColor(0x3f, 0xb9, 0x6a), 2, Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(3, 3, -4, -4));
    }
}

void ViewportWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    syncOverlayGeometry();
}

void ViewportWidget::syncOverlayGeometry()
{
    m_overlay->setGeometry(rect());
    m_overlay->raise();
}

void ViewportWidget::mousePressEvent(QMouseEvent *event)
{
    const QString button = buttonName(event->button());
    if (!m_engine || button.isEmpty()) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_mouseDown = true;
    m_lastMousePos = event->pos();
    emit mousePressed(button);
    event->accept();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent *event)
{
    const QString button = buttonName(event->button());
    if (!m_engine || button.isEmpty()) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    m_mouseDown = false;
    emit mouseReleased(button);
    event->accept();
}

void ViewportWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_engine) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    // Only forward movement while a button is held, matching a locked pointer.
    const QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();
    if (m_mouseDown && (delta.x() != 0 || delta.y() != 0))
        emit mouseMoved(delta.x(), delta.y());

    event->accept();
}
