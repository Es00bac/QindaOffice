// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Chat wire types shared by every provider (plan section 5.3). A ChatRun
// is one streaming request: textDelta() as tokens arrive, finished() with
// the complete answer and usage, failed() with a typed error; cancel()
// aborts promptly (SIGKILL for CLI children, abort() for HTTP).

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QVariant>

#include <optional>

namespace QindaOffice::Ai {

struct ChatMessage {
    QString role; // "user" | "assistant" | "system"
    QString text;
    QList<QImage> images;
};

struct ChatRequest {
    QString systemPrompt;
    QList<ChatMessage> messages;
    int maxTokens = 4096;
    double temperature = 0.7;
    QString effort;                    // reasoning effort where supported
    std::optional<QJsonObject> jsonSchema; // structured output, optional
};

struct ChatUsage {
    int inputTokens = 0;
    int outputTokens = 0;
};

struct ChatResult {
    QString text;
    ChatUsage usage;
};

struct ChatError {
    enum Kind { Unknown, Auth, RateLimited, Cancelled, Timeout, Unavailable };
    Kind kind = Unknown;
    QString message;
    bool retryable = false;
};

class ChatRun : public QObject {
    Q_OBJECT
public:
    explicit ChatRun(QObject *parent = nullptr) : QObject(parent) {}

public slots:
    virtual void cancel() = 0;

signals:
    void textDelta(const QString &delta);
    void finished(const QindaOffice::Ai::ChatResult &result);
    void failed(const QindaOffice::Ai::ChatError &error);

protected:
    // AGENT-GUARD: every terminal outcome MUST go through these guards, never
    // a bare `emit finished/failed`. Providers previously emitted failed()
    // and then triggered a second terminal signal through the abort()/kill()
    // cascade (timeout → abort → finished handler), and InlineActionFlow's
    // delete-state-on-failed turned the double emission into a double free.
    // Monotonic: the first terminal emission wins; later ones are dropped.
    bool emitFinished(const ChatResult &result);
    bool emitFailed(const ChatError &error);

private:
    bool m_terminalEmitted = false;
};

class ChatProvider : public QObject {
    Q_OBJECT
public:
    explicit ChatProvider(QObject *parent = nullptr) : QObject(parent) {}
    ~ChatProvider() override = default;

    // Starts a run; the run is owned by the caller. Providers must emit
    // finished/failed exactly once per run.
    [[nodiscard]] virtual ChatRun *start(const ChatRequest &request) = 0;
};

// Model identity for the catalog and the pickers.
struct ModelSpec {
    QString id;
    QString displayName;
    QString providerId;
    struct {
        bool streaming = true;
        bool vision = false;
        bool jsonSchema = false;
    } caps;
    bool offMachine = false; // data leaves this machine (badge in the UI)
};

} // namespace QindaOffice::Ai
