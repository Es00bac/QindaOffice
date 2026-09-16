// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli_provider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <signal.h>

namespace QindaOffice::Ai {

namespace {

QString binaryFor(CliKind kind)
{
    switch (kind) {
    case CliKind::Claude: return QStringLiteral("claude");
    case CliKind::Codex: return QStringLiteral("codex");
    case CliKind::Kimi: return QStringLiteral("kimi");
    }
    return {};
}

// Builds the argv for the given kind per plan section 5.3.
QStringList argumentsFor(CliKind kind, const QString &model, const QString &extraArgs,
                         bool wantsSchema, QString *lastTxtPath)
{
    QStringList args;
    switch (kind) {
    case CliKind::Claude:
        args << QStringLiteral("-p") << QStringLiteral("--output-format")
             << QStringLiteral("stream-json") << QStringLiteral("--verbose")
             << QStringLiteral("--include-partial-messages")
             << QStringLiteral("--safe-mode") << QStringLiteral("--tools")
             << QString() << QStringLiteral("--no-session-persistence")
             << QStringLiteral("--permission-prompts") << QStringLiteral("none")
             << QStringLiteral("--strict-mcp-config")
             << QStringLiteral("--disable-slash-commands");
        if (!model.isEmpty()) {
            args << QStringLiteral("--model") << model;
        }
        if (!extraArgs.isEmpty()) {
            args << extraArgs.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
        break;
    case CliKind::Codex: {
        args << QStringLiteral("exec") << QStringLiteral("--json")
             << QStringLiteral("--ephemeral") << QStringLiteral("--skip-git-repo-check")
             << QStringLiteral("--ignore-user-config") << QStringLiteral("--ignore-rules")
             << QStringLiteral("-s") << QStringLiteral("read-only")
             << QStringLiteral("--color") << QStringLiteral("never");
        if (!model.isEmpty()) {
            args << QStringLiteral("-m") << model;
        }
        // The authoritative final answer lands in the scratch dir.
        *lastTxtPath = QStringLiteral("last.txt");
        args << QStringLiteral("-o") << *lastTxtPath;
        if (wantsSchema) {
            args << QStringLiteral("--output-schema") << QStringLiteral("schema.json");
        }
        if (!extraArgs.isEmpty()) {
            args << extraArgs.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
        args << QStringLiteral("-"); // prompt on stdin
        break;
    }
    case CliKind::Kimi:
        // Prompt and --skills-dir are appended by the runner once the
        // scratch directory exists.
        args << QStringLiteral("--output-format") << QStringLiteral("stream-json");
        if (!model.isEmpty()) {
            args << QStringLiteral("-m") << model;
        }
        break;
    }
    return args;
}

} // namespace

class CliChatRun final : public ChatRun {
    Q_OBJECT
public:
    CliChatRun(CliChatProvider *provider, const ChatRequest &request)
        : ChatRun(provider)
        , m_kind(provider->kind())
    {
        // Scratch cwd: kimi print mode cannot disable tools, so the model
        // can only touch files inside this directory (plan section 5.3).
        m_scratch.reset(new QTemporaryDir);
        m_scratch->setAutoRemove(true);

        QString lastTxt;
        m_args = argumentsFor(m_kind, provider->model(), QString(), request.jsonSchema.has_value(),
                              &lastTxt);
        m_lastTxtPath = m_scratch->filePath(lastTxt);

        // Render the conversation into one prompt document: the CLI CLIs are
        // single-shot here; multi-turn stays in the HTTP providers' domain.
        QString prompt;
        if (!request.systemPrompt.isEmpty()) {
            prompt += request.systemPrompt + QStringLiteral("\n\n");
        }
        for (const ChatMessage &message : request.messages) {
            prompt += message.text + QStringLiteral("\n");
        }
        prompt = prompt.trimmed();

        if (m_kind == CliKind::Kimi) {
            // Prompt is an argument (<= 200 KB); skills dir must be an
            // empty scratch dir so tool use cannot roam (plan 5.3).
            if (prompt.size() > 200 * 1024) {
                // AGENT-GUARD: defer — consumers connect to failed() only
                // after start() returns; a constructor-time emission vanished
                // into the void and permanently jammed the assistant panel.
                QMetaObject::invokeMethod(
                    this,
                    [this] { emitFailed({ChatError::Unknown, tr("kimi prompt exceeds 200 KB"), false}); },
                    Qt::QueuedConnection);
                return;
            }
            m_args << prompt << QStringLiteral("--skills-dir") << m_scratch->path();
        }

        // Images: write the first one as ink.png into the scratch dir; CLI
        // vision paths differ per tool (claude via stream-json input, codex
        // -i ink.png) - v1 supports a single image.
        if (!request.messages.isEmpty() && !request.messages.last().images.isEmpty()) {
            const QImage image = request.messages.last().images.first();
            image.save(m_scratch->filePath(QStringLiteral("ink.png")));
            if (m_kind == CliKind::Codex) {
                // -i ink.png before the stdin '-' positional.
                const int dash = m_args.indexOf(QStringLiteral("-"));
                if (dash >= 0) {
                    m_args.insert(dash, QStringLiteral("-i"));
                    m_args.insert(dash + 1, QStringLiteral("ink.png"));
                }
            }
        }

        auto *process = new QProcess(this);
        m_process = process;
        process->setWorkingDirectory(m_scratch->path());
        // Own session so cancel can kill helper children too (probe #12).
        QProcess::UnixProcessParameters unixParams;
        unixParams.flags = QProcess::UnixProcessFlag::CreateNewSession;
        process->setUnixProcessParameters(unixParams);
        connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
            m_stdoutBuffer += process->readAllStandardOutput();
            handleLines();
        });
        connect(process, &QProcess::readyReadStandardError, this, [this, process] {
            m_stderrTail += process->readAllStandardError();
            if (m_stderrTail.size() > 4096) {
                m_stderrTail = m_stderrTail.right(4096);
            }
        });
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int exitCode, QProcess::ExitStatus status) {
                    handleLines();
                    handleProcessFinished(exitCode, status);
                });
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError err) {
            if (err == QProcess::FailedToStart) {
                // Queued for symmetry with the guard above; errorOccurred can
                // fire synchronously from start() on some platforms.
                QMetaObject::invokeMethod(
                    this,
                    [this] { emitFailed({ChatError::Unavailable, tr("%1 could not start").arg(binaryFor(m_kind)), false}); },
                    Qt::QueuedConnection);
            }
        });
        process->start(binaryFor(m_kind), m_args);
        if (m_kind != CliKind::Kimi) {
            connect(process, &QProcess::started, this, [this, prompt, process] {
                process->write(prompt.toUtf8());
                process->closeWriteChannel();
            });
        }
        m_killTimer.setSingleShot(true);
        m_killTimer.start(300000); // 5 min overall budget for CLI runs
        connect(&m_killTimer, &QTimer::timeout, this, [this] {
            emitFailed({ChatError::Timeout, tr("CLI run timed out"), true});
            killGroup(SIGKILL);
        });
    }

    void cancel() override
    {
        m_cancelled = true;
        killGroup(SIGTERM);
        // Escalate to SIGKILL after 3 s if the group ignores TERM.
        QTimer::singleShot(3000, this, [this] { killGroup(SIGKILL); });
    }

private:
    void killGroup(int signal)
    {
        if (m_process != nullptr && m_process->state() == QProcess::Running) {
            const auto pid = static_cast<pid_t>(m_process->processId());
            ::kill(-pid, signal);
        }
    }

    void handleLines()
    {
        int newline = 0;
        while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
            m_stdoutBuffer.remove(0, newline + 1);
            if (!line.isEmpty()) {
                handleLine(line);
            }
        }
    }

    void handleLine(const QByteArray &line)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            return; // keep-alive / banner lines are diagnostics only
        }
        const QJsonObject object = doc.object();
        switch (m_kind) {
        case CliKind::Claude: {
            const QString type = object.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("stream_event")) {
                const QJsonObject event = object.value(QStringLiteral("event")).toObject();
                if (event.value(QStringLiteral("type")).toString()
                    == QLatin1String("content_block_delta")) {
                    const QJsonObject delta = event.value(QStringLiteral("delta")).toObject();
                    if (delta.value(QStringLiteral("type")).toString()
                        == QLatin1String("text_delta")) {
                        const QString piece = delta.value(QStringLiteral("text")).toString();
                        m_text += piece;
                        emit textDelta(piece);
                    }
                }
            } else if (type == QLatin1String("assistant")) {
                // Reconcile full text if the terminal result never arrives.
                const QJsonArray content =
                    object.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toArray();
                QString full;
                for (const QJsonValue &block : content) {
                    full += block.toObject().value(QStringLiteral("text")).toString();
                }
                if (!full.isEmpty()) {
                    m_assistantText = full;
                }
            } else if (type == QLatin1String("result")) {
                m_seenResult = true;
                const QString subtype = object.value(QStringLiteral("subtype")).toString();
                if (subtype == QLatin1String("success")) {
                    m_resultText = object.value(QStringLiteral("result")).toString();
                } else {
                    m_resultError = object.value(QStringLiteral("error")).toString();
                    if (m_resultError.isEmpty()) {
                        m_resultError = subtype;
                    }
                }
            }
            break;
        }
        case CliKind::Codex: {
            const QString type = object.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("item.updated")
                || type == QLatin1String("item.completed")) {
                const QJsonObject item = object.value(QStringLiteral("item")).toObject();
                if (item.value(QStringLiteral("type")).toString()
                    == QLatin1String("agent_message")) {
                    const QString full = item.value(QStringLiteral("text")).toString();
                    if (full.size() > m_text.size()) {
                        const QString piece = full.mid(m_text.size());
                        m_text = full;
                        emit textDelta(piece);
                    }
                }
            } else if (type == QLatin1String("turn.failed")) {
                m_resultError = object.value(QStringLiteral("error"))
                                    .toObject()
                                    .value(QStringLiteral("message"))
                                    .toString();
            } else if (type == QLatin1String("turn.completed")) {
                m_seenResult = true;
            }
            break;
        }
        case CliKind::Kimi: {
            // JSONL keyed by role; assistant lines carry the full text.
            const QString role = object.value(QStringLiteral("role")).toString();
            if (role == QLatin1String("assistant")) {
                m_text = object.value(QStringLiteral("content")).toString();
            }
            break;
        }
        }
    }

    void handleProcessFinished(int exitCode, QProcess::ExitStatus status)
    {
        m_killTimer.stop();
        if (m_cancelled) {
            emitFailed({ChatError::Cancelled, QStringLiteral("cancelled"), false});
            return;
        }
        if (status != QProcess::NormalExit) {
            emitFailed({ChatError::Unknown,
                         tr("process crashed: %1").arg(QString::fromUtf8(m_stderrTail)), true});
            return;
        }
        if (!m_resultError.isEmpty()) {
            emitFailed({ChatError::Unknown, m_resultError, true});
            return;
        }
        switch (m_kind) {
        case CliKind::Claude:
            if (!m_resultText.isEmpty()) {
                emitFinished({m_resultText, {}});
            } else if (!m_assistantText.isEmpty()) {
                emitFinished({m_assistantText, {}});
            } else {
                emitFailed({ChatError::Unknown, tr("no answer produced"), true});
            }
            break;
        case CliKind::Codex: {
            // -o last.txt is authoritative (plan section 5.3).
            QFile file(m_lastTxtPath);
            QString text;
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                text = QString::fromUtf8(file.readAll()).trimmed();
            }
            if (text.isEmpty()) {
                text = m_text;
            }
            if (exitCode != 0 && text.isEmpty()) {
                emitFailed({ChatError::Unknown,
                             tr("codex exited with %1: %2")
                                 .arg(exitCode)
                                 .arg(QString::fromUtf8(m_stderrTail)),
                             true});
            } else {
                emitFinished({text, {}});
            }
            break;
        }
        case CliKind::Kimi:
            if (!m_text.isEmpty()) {
                emitFinished({m_text, {}});
            } else {
                emitFailed({ChatError::Unknown, tr("no answer produced"), true});
            }
            break;
        }
    }

    CliKind m_kind;
    QStringList m_args;
    QProcess *m_process = nullptr;
    std::unique_ptr<QTemporaryDir> m_scratch;
    QString m_lastTxtPath;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrTail;
    QString m_text;
    QString m_assistantText;
    QString m_resultText;
    QString m_resultError;
    bool m_seenResult = false;
    bool m_cancelled = false;
    QTimer m_killTimer{this};
};

CliChatProvider::CliChatProvider(CliKind kind, QString model, QString extraArgs, QObject *parent)
    : ChatProvider(parent)
    , m_kind(kind)
    , m_model(std::move(model))
    , m_extraArgs(std::move(extraArgs))
{
}

bool CliChatProvider::isAvailable() const
{
    return !QStandardPaths::findExecutable(binaryFor(m_kind)).isEmpty();
}

ChatRun *CliChatProvider::start(const ChatRequest &request)
{
    return new CliChatRun(this, request);
}

} // namespace QindaOffice::Ai

#include "cli_provider.moc"
