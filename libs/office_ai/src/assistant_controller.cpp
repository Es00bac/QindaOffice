// SPDX-License-Identifier: GPL-3.0-or-later
#include "assistant_controller.h"

#include "model_catalog.h"
#include "api_key_resolver.h"

#include <QClipboard>
#include <QGuiApplication>

namespace QindaOffice::Ai {

namespace {
// The three roles the conversation shows. ChatMessage::role stays on the
// wire vocabulary ("user"/"assistant") and is not the same namespace.
const QLatin1String kRoleYou("You");
const QLatin1String kRoleAssistant("Assistant");
const QLatin1String kRoleError("Error");
// Rolling history bound: 10 turns of context, so a long-lived panel cannot
// grow the request until the provider rejects it.
constexpr int kMaxHistoryMessages = 20;

// Markdown-safe: a reply is shown as the model wrote it (it may well be
// markdown), but the user's prompt and error text are quoted verbatim.
QString escapeMarkdown(const QString &text)
{
    QString out = text;
    for (const QChar c : QStringLiteral("\\`*_{}[]()#+-.!<>")) {
        out.replace(c, QStringLiteral("\\") + c);
    }
    return out;
}
} // namespace

AssistantController::AssistantController(ModelCatalog *catalog, DocumentContextProvider *context,
                                         EditApplier *applier, QObject *parent)
    : QObject(parent)
    , m_catalog(catalog)
    , m_context(context)
    , m_applier(applier)
{
    const QStringList ids = m_catalog != nullptr ? m_catalog->modelIds() : QStringList();
    if (!ids.isEmpty()) {
        m_modelId = ids.first();
    }
    if (m_catalog != nullptr) {
        // Providers added in the settings dialog appear in the picker now.
        connect(m_catalog, &ModelCatalog::changed, this, [this] {
            const QStringList current = m_catalog->modelIds();
            if (!current.contains(m_modelId) && !current.isEmpty()) {
                m_modelId = current.first();
                emit modelIdChanged();
            }
            emit modelsChanged();
        });
    }
}

QVariantList AssistantController::models() const
{
    QVariantList list;
    if (m_catalog == nullptr) {
        return list;
    }
    for (const QString &id : m_catalog->modelIds()) {
        const auto spec = m_catalog->specFor(id);
        list.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), spec.has_value() ? spec->displayName : id},
            {QStringLiteral("offMachine"), spec.has_value() && spec->offMachine},
        });
    }
    return list;
}

void AssistantController::setModelId(const QString &id)
{
    if (m_modelId == id) {
        return;
    }
    m_modelId = id;
    emit modelIdChanged();
}

bool AssistantController::offMachine() const
{
    if (m_catalog == nullptr) {
        return false;
    }
    const auto spec = m_catalog->specFor(m_modelId);
    return spec.has_value() && spec->offMachine;
}

void AssistantController::setScope(int scope)
{
    scope = qBound(0, scope, 2);
    if (m_scope == scope) {
        return;
    }
    m_scope = scope;
    emit scopeChanged();
}

void AssistantController::appendMessage(const QString &role, const QString &text)
{
    if (!m_conversation.isEmpty()) {
        m_conversation += QStringLiteral("\n\n");
    }
    m_conversation += QStringLiteral("**%1**\n\n").arg(role);
    m_conversation += role == kRoleAssistant ? text : escapeMarkdown(text);
    emit conversationChanged();
}

void AssistantController::send(const QString &rawPrompt)
{
    const QString prompt = rawPrompt.trimmed();
    if (m_run != nullptr || prompt.isEmpty()) {
        return; // one conversation turn at a time
    }
    appendMessage(kRoleYou, prompt);

    ChatRequest request;
    // Multi-turn: replay the rolling history before the new prompt so the
    // provider sees the conversation, not a context-free one-shot.
    request.messages = m_history;
    request.messages.append({QStringLiteral("user"), prompt, {}});
    if (m_context != nullptr && m_scope > 0) {
        const DocumentContext context = m_context->context(m_scope == 2);
        QString material;
        if (m_scope == 1 && !context.selection.isEmpty()) {
            material = tr("Selected text:\n%1").arg(context.selection);
        } else if (m_scope == 2) {
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

void AssistantController::runRequest(const ChatRequest &request)
{
    const QString modelId = m_modelId;
    auto startWith = [this, modelId, request](const QString &secret) {
        ChatProvider *provider =
            m_catalog != nullptr ? m_catalog->createProvider(modelId, secret, this) : nullptr;
        if (provider == nullptr) {
            appendMessage(kRoleError, tr("provider unavailable"));
            return;
        }
        // AGENT-GUARD: per-turn reset. m_lastReply accumulated across turns
        // (turn 2's copy returned turn1+partial-turn2) and a failed or
        // cancelled turn left turn 1's actions enabled on that text.
        m_lastReply.clear();
        emit replyChanged();
        // Label the turn before any token arrives so the reply never runs
        // into the prompt line.
        appendMessage(kRoleAssistant, QString());
        m_streamed = false;
        m_run = provider->start(request);
        emit streamingChanged();
        connect(m_run, &ChatRun::textDelta, this, [this](const QString &delta) {
            m_streamed = true;
            m_lastReply += delta;
            m_conversation += delta;
            emit conversationChanged();
        });
        connect(m_run, &ChatRun::finished, this, [this](const ChatResult &result) {
            m_run = nullptr;
            m_lastReply = result.text;
            // AGENT-GUARD: a provider that answers in one shot (no textDelta
            // at all) still has to be shown.
            if (!m_streamed && !m_lastReply.isEmpty()) {
                m_conversation += m_lastReply;
                emit conversationChanged();
            }
            m_history.append({QStringLiteral("assistant"), m_lastReply, {}});
            trimHistory();
            emit streamingChanged();
            emit replyChanged();
        });
        connect(m_run, &ChatRun::failed, this, [this](const ChatError &error) {
            m_run = nullptr;
            m_lastReply.clear();
            appendMessage(kRoleError, error.message);
            emit streamingChanged();
            emit replyChanged();
        });
    };
    const auto config = m_catalog != nullptr ? m_catalog->configFor(modelId) : std::nullopt;
    if (!config.has_value()) {
        startWith(QString());
        return;
    }
    // D-057: keyring first, then the row's {env:}/{file:} reference, the
    // provider's conventional env variables and OpenCode's auth.json. An
    // empty result surfaces as an auth error from the provider.
    ApiKeyResolver::resolve(ModelCatalog::keySources(*config), this,
                            [startWith](const QString &value, const QString &) { startWith(value); });
}

void AssistantController::stop()
{
    if (m_run != nullptr) {
        m_run->cancel();
    }
}

void AssistantController::insert()
{
    if (m_applier != nullptr && !m_lastReply.isEmpty()) {
        m_applier->insertAtCursor(m_lastReply);
    }
}

void AssistantController::replaceSelection()
{
    if (m_applier != nullptr && !m_lastReply.isEmpty()) {
        m_applier->replaceSelection(m_lastReply);
    }
}

void AssistantController::copy()
{
    if (!m_lastReply.isEmpty()) {
        QGuiApplication::clipboard()->setText(m_lastReply);
    }
}

void AssistantController::clearConversation()
{
    m_conversation.clear();
    m_history.clear();
    emit conversationChanged();
}

void AssistantController::trimHistory()
{
    while (m_history.size() > kMaxHistoryMessages) {
        m_history.removeFirst();
    }
}

} // namespace QindaOffice::Ai
