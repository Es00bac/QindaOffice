// SPDX-License-Identifier: GPL-3.0-or-later
#include "assistant_panel.h"

#include "model_catalog.h"
#include "secret_store.h"

#include <QComboBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

namespace QindaOffice::Ai {

namespace {
// The three roles this panel renders. appendMessage() keys its palette role
// off these exact strings; ChatMessage::role stays on the wire vocabulary
// ("user"/"assistant") and is not the same namespace.
const QLatin1String kRoleYou("you");
const QLatin1String kRoleAssistant("assistant");
const QLatin1String kRoleError("error");
// Rolling history bound: 10 turns of context, so a long-lived panel cannot
// grow the request until the provider rejects it.
constexpr int kMaxHistoryMessages = 20;
} // namespace

AssistantPanel::AssistantPanel(ModelCatalog *catalog, DocumentContextProvider *context,
                               EditApplier *applier, QWidget *parent)
    : QWidget(parent)
    , m_catalog(catalog)
    , m_context(context)
    , m_applier(applier)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    auto *top = new QHBoxLayout;
    m_modelPicker = new QComboBox(this);
    for (const QString &id : catalog->modelIds()) {
        const auto spec = catalog->specFor(id);
        QString label = spec.has_value() ? spec->displayName : id;
        if (spec.has_value() && spec->offMachine) {
            label += tr(" (leaves this machine)");
        }
        m_modelPicker->addItem(label, id);
    }
    top->addWidget(m_modelPicker, 1);
    m_contextScope = new QComboBox(this);
    m_contextScope->addItem(tr("No context"));
    m_contextScope->addItem(tr("Selection"));
    m_contextScope->addItem(tr("Whole document"));
    top->addWidget(m_contextScope);
    layout->addLayout(top);

    m_conversation = new QTextBrowser(this);
    layout->addWidget(m_conversation, 1);

    auto *replyActions = new QHBoxLayout;
    m_insert = new QToolButton(this);
    m_insert->setText(tr("Insert"));
    m_replace = new QToolButton(this);
    m_replace->setText(tr("Replace selection"));
    m_copy = new QToolButton(this);
    m_copy->setText(tr("Copy"));
    for (QToolButton *button : {m_insert, m_replace, m_copy}) {
        button->setEnabled(false);
        replyActions->addWidget(button);
    }
    layout->addLayout(replyActions);

    auto *bottom = new QHBoxLayout;
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Ask the assistant…"));
    m_send = new QToolButton(this);
    m_send->setText(tr("Send"));
    m_stop = new QToolButton(this);
    m_stop->setText(tr("Stop"));
    m_stop->setEnabled(false);
    bottom->addWidget(m_input, 1);
    bottom->addWidget(m_send);
    bottom->addWidget(m_stop);
    layout->addLayout(bottom);

    connect(m_input, &QLineEdit::returnPressed, m_send, &QToolButton::click);
    connect(m_send, &QToolButton::clicked, this, [this] {
        const QString prompt = m_input->text().trimmed();
        if (!prompt.isEmpty()) {
            sendPrompt(prompt);
        }
    });
    connect(m_stop, &QToolButton::clicked, this, &AssistantPanel::stop);
    connect(m_copy, &QToolButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_lastReply);
    });
    connect(m_insert, &QToolButton::clicked, this, [this] {
        if (m_applier != nullptr && !m_lastReply.isEmpty()) {
            m_applier->insertAtCursor(m_lastReply);
        }
    });
    connect(m_replace, &QToolButton::clicked, this, [this] {
        if (m_applier != nullptr && !m_lastReply.isEmpty()) {
            m_applier->replaceSelection(m_lastReply);
        }
    });
}

QString AssistantPanel::currentModelId() const
{
    return m_modelPicker->currentData().toString();
}

void AssistantPanel::appendMessage(const QString &role, const QString &text)
{
    // AGENT-GUARD: role colours come from the live widget palette, never from
    // hex literals (ADR-0115 / AGENTS.md rule 3). The previous #884400 /
    // #004488 pair was unreadable on a dark platform palette, and it keyed on
    // a role string ("user") this panel never passes — every message, the
    // user's own prompt included, rendered in the assistant colour.
    const QPalette palette = m_conversation->palette();
    QPalette::ColorRole colourRole = QPalette::LinkVisited; // assistant
    if (role == kRoleYou) {
        colourRole = QPalette::Link;
    } else if (role == kRoleError) {
        colourRole = QPalette::Text;
    }
    const QString body =
        text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    m_conversation->append(QStringLiteral("<b style='color:%1'>%2</b><br>%3")
                               .arg(palette.color(colourRole).name(), role, body));
}

void AssistantPanel::sendPrompt(const QString &prompt)
{
    if (m_run != nullptr) {
        return; // one conversation turn at a time
    }
    appendMessage(kRoleYou, prompt);
    m_input->clear();

    ChatRequest request;
    // Multi-turn: replay the rolling history before the new prompt so the
    // provider sees the conversation, not a context-free one-shot.
    request.messages = m_history;
    request.messages.append({QStringLiteral("user"), prompt, {}});
    const int scope = m_contextScope->currentIndex();
    if (m_context != nullptr && scope > 0) {
        const DocumentContext context = m_context->context(scope == 2);
        QString material;
        if (scope == 1 && !context.selection.isEmpty()) {
            material = tr("Selected text:\n%1").arg(context.selection);
        } else if (scope == 2) {
            material = tr("Document:\n%1").arg(context.document);
        }
        if (!material.isEmpty()) {
            request.messages.prepend({QStringLiteral("user"), material, {}});
        }
        if (!context.region.isNull()) {
            request.messages.last().images.append(context.region);
        }
    }
    m_history.append({QStringLiteral("user"), prompt, {}});
    trimHistory();
    runRequest(request);
}

void AssistantPanel::runRequest(const ChatRequest &request)
{
    const QString modelId = currentModelId();
    // Resolve the secret (if any) then start the run.
    auto startWith = [this, modelId, request](const QString &secret) {
        ChatProvider *provider = m_catalog->createProvider(modelId, secret, this);
        if (provider == nullptr) {
            appendMessage(kRoleError, tr("provider unavailable"));
            return;
        }
        // AGENT-GUARD: per-turn reset. m_lastReply accumulated across turns
        // (turn 2's copy returned turn1+partial-turn2) and a failed/cancelled
        // turn left turn 1's buttons enabled acting on that corrupted text.
        m_lastReply.clear();
        m_insert->setEnabled(false);
        m_replace->setEnabled(false);
        m_copy->setEnabled(false);
        // Label the turn before any token arrives: the deltas below insert
        // at the end of the view, so without this header the reply ran into
        // the user's own prompt line with nothing separating them.
        appendMessage(kRoleAssistant, QString());
        m_streamed = false;
        m_run = provider->start(request);
        m_stop->setEnabled(true);
        connect(m_run, &ChatRun::textDelta, this, [this](const QString &delta) {
            m_streamed = true;
            m_lastReply += delta;
            m_conversation->moveCursor(QTextCursor::End);
            m_conversation->insertPlainText(delta);
        });
        connect(m_run, &ChatRun::finished, this, [this](const ChatResult &result) {
            m_run = nullptr;
            m_stop->setEnabled(false);
            m_lastReply = result.text;
            // AGENT-GUARD: a provider that answers in one shot (no textDelta
            // at all — ModelSpec::caps.streaming is false for it) still has
            // to be shown. The view previously only ever grew from deltas,
            // so such a reply was invisible while Insert/Copy silently held
            // text the user never saw.
            if (!m_streamed && !m_lastReply.isEmpty()) {
                m_conversation->moveCursor(QTextCursor::End);
                m_conversation->insertPlainText(m_lastReply);
            }
            m_history.append({QStringLiteral("assistant"), m_lastReply, {}});
            trimHistory();
            m_insert->setEnabled(!m_lastReply.isEmpty());
            m_replace->setEnabled(!m_lastReply.isEmpty());
            m_copy->setEnabled(!m_lastReply.isEmpty());
        });
        connect(m_run, &ChatRun::failed, this, [this](const ChatError &error) {
            m_run = nullptr;
            m_stop->setEnabled(false);
            m_lastReply.clear();
            appendMessage(kRoleError, error.message);
        });
    };
    const auto config = m_catalog->configFor(modelId);
    if (!config.has_value() || config->secretKey.isEmpty()) {
        startWith(QString());
        return;
    }
    // Resolve through the keychain-backed store (async but near-instant on
    // the session bus; a empty secret surfaces as an auth error later).
    auto *store = new SecretStore(this);
    store->retrieve(config->secretKey, [this, store, startWith](const QString &value) {
        startWith(value);
        store->deleteLater();
    });
}

void AssistantPanel::stop()
{
    if (m_run != nullptr) {
        m_run->cancel();
    }
}

void AssistantPanel::trimHistory()
{
    while (m_history.size() > kMaxHistoryMessages) {
        m_history.removeFirst();
    }
}

} // namespace QindaOffice::Ai
