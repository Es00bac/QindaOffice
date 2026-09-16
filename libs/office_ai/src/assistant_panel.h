// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// AssistantPanel (plan section 5.3): per-window AI dock - model picker,
// context selector (none / selection / document with a size estimate), a
// markdown conversation view, streaming with Stop, and reply actions
// (Insert / Replace selection / Copy) through the EditApplier seam.
// Privacy contract: nothing is sent without an explicit Send press;
// offMachine models show a badge.

#include "chat_types.h"
#include "document_context.h"

#include <QWidget>

class QComboBox;
class QLineEdit;
class QTextBrowser;
class QToolButton;

namespace QindaOffice::Ai {

class ModelCatalog;

class AssistantPanel final : public QWidget {
    Q_OBJECT
public:
    AssistantPanel(ModelCatalog *catalog, DocumentContextProvider *context,
                   EditApplier *applier, QWidget *parent = nullptr);

    [[nodiscard]] QString currentModelId() const;

public slots:
    void sendPrompt(const QString &prompt);
    void stop();

private:
    void appendMessage(const QString &role, const QString &text);
    void runRequest(const ChatRequest &request);

    ModelCatalog *m_catalog;
    DocumentContextProvider *m_context;
    EditApplier *m_applier;
    QComboBox *m_modelPicker;
    QComboBox *m_contextScope;
    QTextBrowser *m_conversation;
    QLineEdit *m_input;
    QToolButton *m_send;
    QToolButton *m_stop;
    QToolButton *m_insert;
    QToolButton *m_replace;
    QToolButton *m_copy;
    ChatRun *m_run = nullptr;
    QString m_lastReply;
    // Rolling conversation history (user/assistant pairs) sent with every
    // request — without it each turn started a context-free conversation.
    QList<ChatMessage> m_history;
};

} // namespace QindaOffice::Ai
