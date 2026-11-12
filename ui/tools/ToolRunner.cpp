/*
 * VAPlayer - host tool process runner
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

#include "tools/ToolRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace {

/** Environment override pointing at the tools directory. */
constexpr char kToolsEnv[] = "VAPLAYER_TOOLS";
/** Environment override selecting the Python interpreter. */
constexpr char kPythonEnv[] = "VAPLAYER_PYTHON";

#ifdef Q_OS_WIN
constexpr char kDefaultPython[] = "python";
#else
constexpr char kDefaultPython[] = "python3";
#endif

/**
 * Directory names that may hold the tools relative to a candidate base.
 * A developer build lives at <repo>/ui/build, an installed one may sit
 * beside a top-level `tools/`.
 */
QStringList toolsCandidates()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString cwd = QDir::currentPath();

    QStringList bases = { appDir, cwd };
    // Walk a few levels up from each base looking for a sibling `tools/`.
    QStringList roots;
    for (const QString &base : bases) {
        QDir dir(base);
        for (int level = 0; level < 5; ++level) {
            roots << dir.absolutePath();
            if (!dir.cdUp())
                break;
        }
    }

    QStringList candidates;
    for (const QString &root : roots)
        candidates << QDir(root).absoluteFilePath(QStringLiteral("tools"));
    return candidates;
}

} // namespace

ToolRunner::ToolRunner(QObject *parent)
    : QObject(parent)
{
}

ToolRunner::~ToolRunner()
{
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(2000);
        }
    }
}

QString ToolRunner::toolsDirectory()
{
    const QString override = qEnvironmentVariable(kToolsEnv);
    if (!override.isEmpty()) {
        const QFileInfo info(override);
        if (info.isDir())
            return info.absoluteFilePath();
    }

    const QStringList candidates = toolsCandidates();
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        // Require a real entry point so we do not pick up an unrelated `tools`.
        if (info.isDir()
            && QFileInfo(candidate + QStringLiteral("/fetch_image.py")).isFile()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

QString ToolRunner::pythonInterpreter()
{
    const QString override = qEnvironmentVariable(kPythonEnv);
    if (!override.isEmpty())
        return override;

    const QString onPath = QStandardPaths::findExecutable(
        QString::fromLatin1(kDefaultPython));
    if (!onPath.isEmpty())
        return onPath;
    // A bare name lets QProcess try PATH resolution itself.
    return QString::fromLatin1(kDefaultPython);
}

QStringList ToolRunner::resolveTool(const QString &name)
{
    const QString dir = toolsDirectory();
    if (dir.isEmpty())
        return QStringList();

    if (name == QLatin1String("vflash")) {
        // Prefer the shipped entry point so behaviour matches the CLI docs.
#ifndef Q_OS_WIN
        const QString script = dir + QStringLiteral("/bin/vflash");
        if (QFileInfo(script).isFile())
            return QStringList{ script };
#endif
        const QString cli = dir + QStringLiteral("/vflash/cli.py");
        if (QFileInfo(cli).isFile())
            return QStringList{ pythonInterpreter(), cli };
        return QStringList();
    }

    if (name == QLatin1String("fetch_image") || name == QLatin1String("inject_arm")) {
        const QString script = dir + QLatin1Char('/') + name + QStringLiteral(".py");
        if (QFileInfo(script).isFile())
            return QStringList{ pythonInterpreter(), script };
        return QStringList();
    }

    return QStringList();
}

QString ToolRunner::describeCommand(const QStringList &command)
{
    QStringList parts;
    parts.reserve(command.size());
    for (const QString &arg : command) {
        if (arg.contains(QLatin1Char(' ')))
            parts << QLatin1Char('"') + arg + QLatin1Char('"');
        else
            parts << arg;
    }
    return parts.join(QLatin1Char(' '));
}

bool ToolRunner::start(const QString &tool, const QStringList &arguments)
{
    const QStringList command = resolveTool(tool);
    if (command.isEmpty()) {
        m_errorString = tr("Could not locate the '%1' tool. Expected it under "
                           "../tools relative to the application.")
                            .arg(tool);
        setState(State::Failed);
        emit failed(m_errorString);
        return false;
    }

    const QString program = command.first();
    QStringList rest = command.mid(1);
    rest += arguments;
    return startProgram(program, rest);
}

bool ToolRunner::startProgram(const QString &program, const QStringList &arguments)
{
    if (isRunning()) {
        m_errorString = tr("A tool is already running.");
        emit failed(m_errorString);
        return false;
    }

    reset();

    if (!m_process) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyRead,
                this, &ToolRunner::onReadyRead);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        connect(m_process, &QProcess::finished,
                this, &ToolRunner::onProcessFinished);
        connect(m_process, &QProcess::errorOccurred,
                this, &ToolRunner::onProcessError);
#else
        connect(m_process,
                QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &ToolRunner::onProcessFinished);
        connect(m_process,
                QOverload<QProcess::ProcessError>::of(&QProcess::error),
                this, &ToolRunner::onProcessError);
#endif
    }

    // Unbuffered, UTF-8 output keeps the console live and free of mojibake.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    m_process->setProcessEnvironment(env);

    m_command = QStringList{ program } + arguments;
    setState(State::Running);
    emit started();
    m_process->start(program, arguments);
    return true;
}

void ToolRunner::cancel()
{
    if (!isRunning())
        return;

    m_process->terminate();
    QTimer::singleShot(2000, this, [this]() {
        if (isRunning())
            m_process->kill();
    });
}

bool ToolRunner::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void ToolRunner::reset()
{
    m_log.clear();
    m_pending.clear();
    m_exitCode = -1;
    m_errorString.clear();
    m_state = State::Idle;
}

void ToolRunner::setState(State state)
{
    m_state = state;
}

void ToolRunner::onReadyRead()
{
    if (!m_process)
        return;

    m_pending += QString::fromUtf8(m_process->readAll());

    // Both newline and carriage return delimit lines: curl's progress meter
    // rewrites a single line with \r, which we surface as separate updates.
    static const QRegularExpression separator(QStringLiteral("[\\r\\n]"));
    int index = m_pending.indexOf(separator);
    while (index >= 0) {
        const QString line = m_pending.left(index);
        m_pending.remove(0, index + 1);
        if (!line.isEmpty())
            handleLine(line);
        index = m_pending.indexOf(separator);
    }
}

void ToolRunner::handleLine(const QString &line)
{
    m_log << line;
    emit outputLine(line);

    const int percent = parsePercent(line);
    if (percent >= 0) {
        emit progressChanged(percent);
    } else {
        const int stage = stageProgress(line);
        if (stage >= 0)
            emit progressChanged(stage);
    }
}

void ToolRunner::onProcessFinished(int exitCode, int exitStatus)
{
    Q_UNUSED(exitStatus)

    // Flush any trailing partial line.
    if (!m_pending.isEmpty()) {
        handleLine(m_pending);
        m_pending.clear();
    }

    m_exitCode = exitCode;
    const bool success = (exitCode == 0);

    if (success) {
        setState(State::Succeeded);
        emit progressChanged(100);
    } else {
        setState(State::Failed);
        if (m_errorString.isEmpty()) {
            // Surface the most relevant line: the last error-looking one, or
            // otherwise the tail of the output.
            QString message;
            for (int i = m_log.size() - 1; i >= 0; --i) {
                if (looksLikeError(m_log.at(i))) {
                    message = m_log.at(i);
                    break;
                }
            }
            if (message.isEmpty() && !m_log.isEmpty())
                message = m_log.last();
            m_errorString = message.isEmpty()
                                ? tr("The tool exited with code %1.").arg(exitCode)
                                : message;
        }
    }

    emit finished(success, exitCode);
}

void ToolRunner::onProcessError()
{
    if (!m_process)
        return;

    // "Failed to start" is the only error that is not followed by finished().
    if (m_process->error() == QProcess::FailedToStart) {
        m_errorString = tr("Failed to start the tool: %1")
                            .arg(m_process->errorString());
        setState(State::Failed);
        emit failed(m_errorString);
        emit finished(false, -1);
    }
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

QHash<QString, QString> ToolRunner::parseKeyValues(const QString &text)
{
    QHash<QString, QString> result;
    static const QRegularExpression spaces(QStringLiteral("\\s+"));

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;

        QString key = line.left(colon).trimmed().toLower();
        key.replace(spaces, QStringLiteral(" "));
        const QString value = line.mid(colon + 1).trimmed();
        if (!key.isEmpty())
            result.insert(key, value);
    }
    return result;
}

QString ToolRunner::lookup(const QHash<QString, QString> &map, const QString &key)
{
    return map.value(key.toLower());
}

int ToolRunner::parsePercent(const QString &line)
{
    static const QRegularExpression percent(
        QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*%"));
    const QRegularExpressionMatch match = percent.match(line);
    if (!match.hasMatch())
        return -1;

    bool ok = false;
    const double value = match.captured(1).toDouble(&ok);
    if (!ok)
        return -1;
    return qBound(0, static_cast<int>(value + 0.5), 100);
}

int ToolRunner::stageProgress(const QString &line)
{
    const QString text = line.toLower();

    // Order matters: later stages must not be shadowed by earlier keywords.
    struct Stage { const char *needle; int percent; };
    static const Stage stages[] = {
        { "provisioned image",   100 },
        { "bridge installed",    100 },
        { "actions:",            100 },
        { "ok:",                 100 },
        { "verified",             98 },
        { "repacking",            92 },
        { "create data disk",     88 },
        { "qemu-img convert",     80 },
        { "converting",           80 },
        { "splicing",             70 },
        { "installing",           60 },
        { "unsquashfs",           40 },
        { "extracting from iso",  35 },
        { "fetching bridge",      30 },
        { "unpacking",            25 },
        { "flattened",            20 },
        { "flattening",           20 },
        { "reusing cached",       20 },
        { "downloading",           5 },
    };

    int best = -1;
    for (const Stage &stage : stages) {
        if (text.contains(QLatin1String(stage.needle)))
            best = qMax(best, stage.percent);
    }
    return best;
}

bool ToolRunner::looksLikeError(const QString &line)
{
    const QString text = line.trimmed().toLower();
    if (text.isEmpty())
        return false;

    return text.startsWith(QLatin1String("error:"))
        || text.startsWith(QLatin1String("error "))
        || text.startsWith(QLatin1String("fatal"))
        || text.startsWith(QLatin1String("traceback"))
        || text.contains(QLatin1String("command failed:"))
        || text.contains(QLatin1String("no such file"))
        || text.contains(QLatin1String("not found"))
        || text.contains(QLatin1String("permission denied"));
}

bool ToolRunner::looksLikeSuccess(const QString &line)
{
    const QString text = line.trimmed().toLower();
    if (text.isEmpty())
        return false;

    return text.contains(QLatin1String("bridge installed"))
        || text.contains(QLatin1String("provisioned image"))
        || text.contains(QLatin1String("installed: true"))
        || text.startsWith(QLatin1String("ok:"))
        || text.contains(QLatin1String("actions:"));
}
