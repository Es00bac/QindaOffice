// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// AssistantController (conversion plan §3.8): the per-window assistant as
// STATE for a QML panel (qml/AssistantPanel.qml) — the model list and the
// chosen model, the context scope (none / selection / document), the
// conversation as markdown, the streaming turn with Stop, and the reply
// actions (Insert / Replace selection / Copy) through the EditApplier
// seam. Privacy contract unchanged from the Widgets panel it replaces:
// nothing is sent without an explicit send(); off-machine models say so.

#include "chat_types.h"
#include "document_context.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Ai {

class ModelCatalog;

class AssistantController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application over its catalog and document seams.")
    // [{id, name, offMachine}] in catalog order.
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QString modelId READ modelId WRITE setModelId NOTIFY modelIdChanged)
    Q_PROPERTY(bool offMachine READ offMachine NOTIFY modelIdChanged)
    // 0 = no context, 1 = selection, 2 = whole document.
    Q_PROPERTY(int scope READ scope WRITE setScope NOTIFY scopeChanged)
    Q_PROPERTY(QString conversation READ conversation NOTIFY conversationChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
    Q_PROPERTY(bool hasReply READ hasReply NOTIFY replyChanged)
    Q_PROPERTY(QString lastReply READ lastReply NOTIFY replyChanged)
public:
    AssistantController(ModelCatalog *catalog, DocumentContextProvider *context,
                        EditApplier *applier, QObject *parent = nullptr);

    [[nodiscard]] QVariantList models() const;
    [[nodiscard]] QString modelId() const { return m_modelId; }
    void setModelId(const QString &id);
    [[nodiscard]] bool offMachine() const;
    [[nodiscard]] int scope() const { return m_scope; }
    void setScope(int scope);
    [[nodiscard]] QString conversation() const { return m_conversation; }
    [[nodiscard]] bool streaming() const { return m_run != nullptr; }
    [[nodiscard]] bool hasReply() const { return !m_lastReply.isEmpty(); }
    [[nodiscard]] QString lastReply() const { return m_lastReply; }

    // The explicit user action: nothing leaves the machine before it.
    Q_INVOKABLE void send(const QString &prompt);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void insert();
    Q_INVOKABLE void replaceSelection();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void clearConversation();

signals:
    void modelsChanged();
    void modelIdChanged();
    void scopeChanged();
    void conversationChanged();
    void streamingChanged();
    void replyChanged();

private:
    void appendMessage(const QString &role, const QString &text);
    void runRequest(const ChatRequest &request);
    // Bounds the rolling history so the request cannot grow without limit.
    void trimHistory();

    ModelCatalog *m_catalog;
    DocumentContextProvider *m_context;
    EditApplier *m_applier;
    QString m_modelId;
    int m_scope = 0;
    QString m_conversation;
    ChatRun *m_run = nullptr;
    // True once the running turn has produced at least one textDelta; a
    // non-streaming provider's finished() text is rendered instead.
    bool m_streamed = false;
    QString m_lastReply;
    // Rolling conversation history (user/assistant pairs) sent with every
    // request — without it each turn started a context-free conversation.
    QList<ChatMessage> m_history;
};

} // namespace QindaOffice::Ai
