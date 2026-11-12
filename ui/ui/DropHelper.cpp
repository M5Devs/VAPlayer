/*
 * VAPlayer - drag & drop classification helpers
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

#include "ui/DropHelper.h"

#include <QFileInfo>

DropAction classifyDrop(const QString &fileName)
{
    const QString suffix =
        QFileInfo(fileName).suffix().toLower();
    if (suffix.isEmpty())
        return DropAction::Ignore;

    if (suffix == QLatin1String("apk"))
        return DropAction::InstallApk;
    if (suffix == QLatin1String("zip"))
        return DropAction::FlashZip;

    // Media, documents and anything else the guest can open from Downloads.
    return DropAction::PushFile;
}

QString dropDestination(const QString &fileName)
{
    const QString base = QFileInfo(fileName).fileName();
    return QStringLiteral("/sdcard/Download/") + base;
}

QString dropActionLabel(DropAction action)
{
    switch (action) {
    case DropAction::InstallApk:
        return QStringLiteral("install");
    case DropAction::FlashZip:
        return QStringLiteral("flash");
    case DropAction::PushFile:
        return QStringLiteral("push");
    case DropAction::Ignore:
        break;
    }
    return QStringLiteral("ignore");
}
