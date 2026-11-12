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

#ifndef VAPLAYER_TOOLRUNNER_H
#define VAPLAYER_TOOLRUNNER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

/**
 * Runs one of the host provisioning tools in `../tools` and reports its
 * output line by line.
 *
 * The tools are Python programs (``fetch_image.py``, ``inject_arm.py``,
 * ``vflash/cli.py``) plus the ``bin/vflash`` shell entry point.  The runner
 * resolves them relative to the repository, launches them asynchronously with
 * QProcess and never blocks the UI thread.
 *
 * Parsing helpers are static so the output formats can be tested without
 * spawning a process.
 */
class ToolRunner : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,
        Running,
        Succeeded,
        Failed,
    };
    Q_ENUM(State)

    explicit ToolRunner(QObject *parent = nullptr);
    ~ToolRunner() override;

    /** Directory holding the provisioning tools, or empty if not found. */
    static QString toolsDirectory();

    /** Interpreter used for the Python entry points ("python3" by default). */
    static QString pythonInterpreter();

    /**
     * Command that runs a logical tool: "vflash", "fetch_image" or
     * "inject_arm".  Returns an empty list when the tool cannot be located.
     */
    static QStringList resolveTool(const QString &name);

    /** Human-readable command line, for logging. */
    static QString describeCommand(const QStringList &command);

    /**
     * Run a logical tool (see resolveTool) with *arguments*.
     * Returns false and emits failed() when the tool cannot be located.
     */
    bool start(const QString &tool, const QStringList &arguments);

    /**
     * Run an explicit command.  Exposed for tests and for tools that are not
     * part of the standard set.
     */
    bool startProgram(const QString &program, const QStringList &arguments);

    /** Ask the process to stop; it is killed if it does not exit promptly. */
    void cancel();

    State state() const { return m_state; }
    bool isRunning() const;
    bool succeeded() const { return m_state == State::Succeeded; }

    int exitCode() const { return m_exitCode; }
    QString errorString() const { return m_errorString; }

    /** Every output line seen so far, in order. */
    QStringList log() const { return m_log; }

    /** The full captured output as one string. */
    QString output() const { return m_log.join(QLatin1Char('\n')); }

    /** The command line that is (or was) executed. */
    QString commandLine() const { return describeCommand(m_command); }

    // -- Output parsing (static so they are directly testable) ------------

    /**
     * Parse the ``key: value`` blocks printed by the tools (``vflash
     * --inspect`` and friends) into a map.  Keys are lower-cased with
     * whitespace collapsed.
     */
    static QHash<QString, QString> parseKeyValues(const QString &text);

    /** The value for *key* from a parseKeyValues() result, or an empty string. */
    static QString lookup(const QHash<QString, QString> &map, const QString &key);

    /**
     * Extract a percentage from a progress line, or -1 when the line carries
     * no percentage.  Understands ``qemu-img -p`` ("12.34%") and curl's
     * progress meter.
     */
    static int parsePercent(const QString &line);

    /**
     * Map a tool's stage message onto an overall progress value (0-100), or
     * -1 when the line is not a stage marker.  Used to drive the progress bar
     * for tools that report stages rather than a percentage.
     */
    static int stageProgress(const QString &line);

    /** True when a line looks like a fatal error from one of the tools. */
    static bool looksLikeError(const QString &line);

    /** True when a line confirms the operation succeeded. */
    static bool looksLikeSuccess(const QString &line);

signals:
    void started();
    void outputLine(const QString &line);
    void progressChanged(int percent);
    void finished(bool success, int exitCode);
    void failed(const QString &message);

private slots:
    void onReadyRead();
    void onProcessFinished(int exitCode, int exitStatus);
    void onProcessError();

private:
    void setState(State state);
    void handleLine(const QString &line);
    void reset();

    QProcess *m_process = nullptr;
    State m_state = State::Idle;
    int m_exitCode = -1;
    QString m_errorString;
    QStringList m_command;
    QStringList m_log;
    QString m_pending;      ///< Partial line carried between reads.
};

#endif // VAPLAYER_TOOLRUNNER_H
