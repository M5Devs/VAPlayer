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

#ifndef VAPLAYER_DROPHELPER_H
#define VAPLAYER_DROPHELPER_H

#include <QString>
#include <QStringList>

/**
 * Classification of a file dropped onto the emulator viewport.
 *
 * Kept separate from the widget so the routing rules can be unit tested
 * without constructing a MainWindow.
 */
enum class DropAction {
    Ignore,     ///< Not something we can route.
    InstallApk, ///< ``adb install -r``.
    FlashZip,   ///< Open V-Flash with the package pre-selected.
    PushFile,   ///< ``adb push`` into /sdcard/Download.
};

/** Classify a single dropped file by its name. */
DropAction classifyDrop(const QString &fileName);

/**
 * The remote path a media/document drop is pushed to.  Always rooted under
 * Android's shared Downloads directory so the user can find it.
 */
QString dropDestination(const QString &fileName);

/** Human readable label for a drop action, used in status messages. */
QString dropActionLabel(DropAction action);

#endif // VAPLAYER_DROPHELPER_H
