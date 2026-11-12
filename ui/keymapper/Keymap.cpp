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

#include "keymapper/Keymap.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

/** Normalised absolute input range shared by all coordinates. */
int clampNormalised(int value)
{
    if (value < 0)
        return 0;
    if (value > 0x7fff)
        return 0x7fff;
    return value;
}

QJsonObject tapToJson(const KeyTap &tap)
{
    QJsonObject obj;
    obj[QStringLiteral("key")] = tap.key;
    obj[QStringLiteral("x")] = tap.x;
    obj[QStringLiteral("y")] = tap.y;
    if (!tap.label.isEmpty())
        obj[QStringLiteral("label")] = tap.label;
    return obj;
}

KeyTap tapFromJson(const QJsonObject &obj)
{
    KeyTap tap;
    tap.key = obj.value(QStringLiteral("key")).toString();
    tap.x = clampNormalised(obj.value(QStringLiteral("x")).toInt(0x4000));
    tap.y = clampNormalised(obj.value(QStringLiteral("y")).toInt(0x4000));
    tap.label = obj.value(QStringLiteral("label")).toString();
    return tap;
}

} // namespace

QJsonObject Keymap::toJson() const
{
    QJsonObject root;
    root[QStringLiteral("name")] = name;
    if (!packageName.isEmpty())
        root[QStringLiteral("package")] = packageName;

    QJsonArray tapsArray;
    for (const KeyTap &tap : taps)
        tapsArray.append(tapToJson(tap));
    root[QStringLiteral("taps")] = tapsArray;

    QJsonObject dpadObj;
    dpadObj[QStringLiteral("enabled")] = dpad.enabled;
    dpadObj[QStringLiteral("up")] = dpad.up;
    dpadObj[QStringLiteral("down")] = dpad.down;
    dpadObj[QStringLiteral("left")] = dpad.left;
    dpadObj[QStringLiteral("right")] = dpad.right;
    dpadObj[QStringLiteral("x")] = dpad.centerX;
    dpadObj[QStringLiteral("y")] = dpad.centerY;
    dpadObj[QStringLiteral("radius")] = dpad.radius;
    root[QStringLiteral("dpad")] = dpadObj;

    QJsonObject crosshairObj;
    crosshairObj[QStringLiteral("enabled")] = crosshair.enabled;
    crosshairObj[QStringLiteral("toggleKey")] = crosshair.toggleKey;
    crosshairObj[QStringLiteral("sensitivity")] = crosshair.sensitivity;
    root[QStringLiteral("crosshair")] = crosshairObj;

    return root;
}

Keymap Keymap::fromJson(const QJsonObject &object, bool *ok)
{
    Keymap map;
    map.name = object.value(QStringLiteral("name")).toString();
    map.packageName = object.value(QStringLiteral("package")).toString();

    const QJsonArray tapsArray =
        object.value(QStringLiteral("taps")).toArray();
    for (const QJsonValue &value : tapsArray) {
        if (value.isObject())
            map.taps.append(tapFromJson(value.toObject()));
    }

    const QJsonObject dpadObj = object.value(QStringLiteral("dpad")).toObject();
    map.dpad.enabled = dpadObj.value(QStringLiteral("enabled")).toBool(false);
    map.dpad.up = dpadObj.value(QStringLiteral("up")).toString();
    map.dpad.down = dpadObj.value(QStringLiteral("down")).toString();
    map.dpad.left = dpadObj.value(QStringLiteral("left")).toString();
    map.dpad.right = dpadObj.value(QStringLiteral("right")).toString();
    map.dpad.centerX =
        clampNormalised(dpadObj.value(QStringLiteral("x")).toInt(0x4000));
    map.dpad.centerY =
        clampNormalised(dpadObj.value(QStringLiteral("y")).toInt(0x5000));
    map.dpad.radius = dpadObj.value(QStringLiteral("radius")).toInt(0x1800);

    const QJsonObject crosshairObj =
        object.value(QStringLiteral("crosshair")).toObject();
    map.crosshair.enabled =
        crosshairObj.value(QStringLiteral("enabled")).toBool(false);
    map.crosshair.toggleKey =
        crosshairObj.value(QStringLiteral("toggleKey")).toString();
    map.crosshair.sensitivity =
        crosshairObj.value(QStringLiteral("sensitivity")).toInt(100);
    if (map.crosshair.sensitivity <= 0)
        map.crosshair.sensitivity = 100;

    if (ok)
        *ok = true;
    return map;
}

bool Keymap::save(const QString &filePath, QString *error) const
{
    const QFileInfo info(filePath);
    if (!info.absolutePath().isEmpty() && !QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(info.absolutePath());
        return false;
    }

    // QSaveFile keeps the previous profile intact if the write fails.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

Keymap Keymap::load(const QString &filePath, bool *ok, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (ok)
            *ok = false;
        if (error)
            *error = file.errorString();
        return Keymap();
    }

    QJsonParseError parseError;
    const QJsonDocument doc =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (ok)
            *ok = false;
        if (error)
            *error = parseError.errorString();
        return Keymap();
    }

    return fromJson(doc.object(), ok);
}

bool Keymap::isEmpty() const
{
    return taps.isEmpty() && !dpad.enabled && !crosshair.enabled;
}

QString Keymap::profilesDirectory()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString root = base.isEmpty()
                             ? QDir::homePath() + QStringLiteral("/.vaplayer")
                             : base;
    return QDir(root).absoluteFilePath(QStringLiteral("profiles"));
}

QString Keymap::defaultProfilePath()
{
    // Shipped alongside the sources; the build copies it next to the binary.
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("profiles/default.json"));
}

QString Keymap::profilePathFor(const QString &packageName)
{
    // Package names are dotted identifiers; keep them filesystem-safe.
    QString safe = packageName;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
                 QStringLiteral("_"));
    if (safe.isEmpty())
        safe = QStringLiteral("default");
    return QDir(profilesDirectory()).absoluteFilePath(safe + QStringLiteral(".json"));
}

QStringList Keymap::supportedKeys()
{
    return {
        QStringLiteral("space"), QStringLiteral("return"), QStringLiteral("tab"),
        QStringLiteral("esc"), QStringLiteral("backspace"),
        QStringLiteral("shift"), QStringLiteral("ctrl"), QStringLiteral("alt"),
        QStringLiteral("up"), QStringLiteral("down"), QStringLiteral("left"),
        QStringLiteral("right"),
        QStringLiteral("q"), QStringLiteral("w"), QStringLiteral("e"),
        QStringLiteral("r"), QStringLiteral("t"), QStringLiteral("y"),
        QStringLiteral("u"), QStringLiteral("i"), QStringLiteral("o"),
        QStringLiteral("p"), QStringLiteral("a"), QStringLiteral("s"),
        QStringLiteral("d"), QStringLiteral("f"), QStringLiteral("g"),
        QStringLiteral("h"), QStringLiteral("j"), QStringLiteral("k"),
        QStringLiteral("l"), QStringLiteral("z"), QStringLiteral("x"),
        QStringLiteral("c"), QStringLiteral("v"), QStringLiteral("b"),
        QStringLiteral("n"), QStringLiteral("m"),
        QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
        QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"),
        QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9"),
        QStringLiteral("0"),
        QStringLiteral("f1"), QStringLiteral("f2"), QStringLiteral("f3"),
        QStringLiteral("f4"), QStringLiteral("f5"), QStringLiteral("f6"),
        QStringLiteral("f7"), QStringLiteral("f8"), QStringLiteral("f9"),
        QStringLiteral("f10"), QStringLiteral("f11"), QStringLiteral("f12"),
        QStringLiteral("mouse-left"), QStringLiteral("mouse-right"),
        QStringLiteral("mouse-middle"), QStringLiteral("grave"),
    };
}

bool Keymap::isSupportedKey(const QString &key)
{
    return supportedKeys().contains(key.trimmed().toLower());
}

QString Keymap::qcodeForQtKey(int qtKey)
{
    // Letters and digits map onto their own qcode.
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
        return QString(QChar(QLatin1Char('a' + (qtKey - Qt::Key_A))));
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
        return QString(QChar(QLatin1Char('0' + (qtKey - Qt::Key_0))));

    switch (qtKey) {
    case Qt::Key_Space:     return QStringLiteral("space");
    case Qt::Key_Return:
    case Qt::Key_Enter:     return QStringLiteral("return");
    case Qt::Key_Tab:       return QStringLiteral("tab");
    case Qt::Key_Escape:    return QStringLiteral("esc");
    case Qt::Key_Backspace: return QStringLiteral("backspace");
    case Qt::Key_Shift:     return QStringLiteral("shift");
    case Qt::Key_Control:   return QStringLiteral("ctrl");
    case Qt::Key_Alt:       return QStringLiteral("alt");
    case Qt::Key_Up:        return QStringLiteral("up");
    case Qt::Key_Down:      return QStringLiteral("down");
    case Qt::Key_Left:      return QStringLiteral("left");
    case Qt::Key_Right:     return QStringLiteral("right");
    case Qt::Key_QuoteLeft: return QStringLiteral("grave");
    default:
        break;
    }

    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12)
        return QStringLiteral("f%1").arg(qtKey - Qt::Key_F1 + 1);

    return QString();
}
