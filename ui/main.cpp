/*
 * VAPlayer - application entry point
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

#include "ui/MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("VAPlayer"));
    QApplication::setApplicationDisplayName(QStringLiteral("VAPlayer"));
    QApplication::setOrganizationName(QStringLiteral("VAPlayer"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // Prefer a dark base style where available; the widget stylesheet refines
    // colours on top of it.
    if (QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive))
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/vaplayer.svg")));

    MainWindow window;
    window.show();

    return app.exec();
}
