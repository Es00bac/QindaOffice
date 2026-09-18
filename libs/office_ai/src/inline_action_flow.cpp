// SPDX-License-Identifier: GPL-3.0-or-later
#include "inline_action_flow.h"

#include "model_catalog.h"
#include "secret_store.h"

#include <QPointer>
#include <QRegularExpression>

#ifdef QQO_WIDGETS_AI
#include "proposal_dialog_widgets.h"
#endif

namespace QindaOffice::Ai {

const QVector<InlineAction> &inlineActions()
{
    static const QVector<InlineAction> actions = {
        {QStringLiteral("rewrite"), QStringLiteral("Rewrite"),
         QStringLiteral("Rewrite the following text to read better. "
                        "Answer with ONLY the rewritten text.\n\n%1")},
        {QStringLiteral("shorten"), QStringLiteral("Shorten"),
         QStringLiteral("Make the following text shorter without losing meaning. "
                        "Answer with ONLY the shortened text.\n\n%1")},
        {QStringLiteral("expand"), QStringLiteral("Expand"),
         QStringLiteral("Expand the following text with more detail. "
                        "Answer with ONLY the expanded text.\n\n%1")},
        {QStringLiteral("grammar"), QStringLiteral("Fix grammar"),
         QStringLiteral("Fix the grammar and spelling of the following text. "
                        "Answer with ONLY the corrected text.\n\n%1")},
        {QStringLiteral("summarize"), QStringLiteral("Summarize"),
         QStringLiteral("Summarize the following text in one or two sentences.\n\n%1")},
        {QStringLiteral("continue"), QStringLiteral("Continue writing"),
         QStringLiteral("Continue the following text by one or two sentences. "
                        "Answer with ONLY the continuation.\n\n%1")},
        {QStringLiteral("explain"), QStringLiteral("Explain"),
         QStringLiteral("Explain the following text in plain language.\n\n%1")},
    };
    return actions;
}

InlineActionFlow::InlineActionFlow(ModelCatalog *catalog, DocumentContextProvider *context,
                                   EditApplier *applier, QObject *parent)
    : QObject(parent)
    , m_catalog(catalog)
    , m_context(context)
    , m_applier(applier)
{
}

QString InlineActionFlow::extractProposal(const QString &modelText)
{
    // Structured output v1: prefer a fenced block when present (plan 5.3).
    static const QRegularExpression fence(QStringLiteral("```(?:\\w*)\\n([\\s\\S]*?)```"));
    auto it = fence.globalMatch(modelText);
    QStringList blocks;
    while (it.hasNext()) {
        const auto match = it.next();
        blocks.append(match.captured(1));
    }
    if (!blocks.isEmpty()) {
        return blocks.join(QStringLiteral("\n\n")).trimmed();
    }
    return modelText.trimmed();
}

void InlineActionFlow::run(const QString &actionId, QObject *dialogParent)
{
    for (const InlineAction &action : inlineActions()) {
        if (action.id == actionId) {
            m_pending = action;
            execute(action, dialogParent);
            return;
        }
    }
}

void InlineActionFlow::present(const InlineAction &action, const QString &original,
                               const QString &proposal, QObject *dialogParent)
{
    emit proposalReady(original, proposal);
    ProposalPresenter presenter = m_presenter;
#ifdef QQO_WIDGETS_AI
    if (!presenter.show) {
        presenter = widgetsProposalPresenter(dialogParent);
    }
#endif
    if (!presenter.show) {
        return; // nobody to show it to: the signal above is all there is
    }
    // AGENT-GUARD: the callbacks capture `this` through a QPointer-like
    // guard — the presenter may outlive a window's flow.
    const QPointer<InlineActionFlow> guard(this);
    presenter.show(
        original, proposal,
        [guard, proposal] {
            if (guard != nullptr && guard->m_applier != nullptr) {
                guard->m_applier->replaceSelection(proposal);
            }
        },
        [guard, action, dialogParent] {
            if (guard != nullptr) {
                guard->execute(action, dialogParent);
            }
        });
}

void InlineActionFlow::execute(const InlineAction &action, QObject *dialogParent)
{
    if (dialogParent == nullptr) {
        dialogParent = parent();
    }
    if (m_context == nullptr || m_applier == nullptr) {
        return;
    }
    const DocumentContext context = m_context->context(false);
    if (context.selection.isEmpty()) {
        return; // inline actions operate on selections
    }
    ChatRequest request;
    request.maxTokens = 2048;
    request.temperature = 0.3;
    request.messages.append({QStringLiteral("user"), action.prompt.arg(context.selection), {}});

    // Use the first catalog provider for inline work (v1; a per-task
    // default model arrives with the preferences dialog).
    const QStringList ids = m_catalog->modelIds();
    if (ids.isEmpty()) {
        return;
    }
    const QString modelId = ids.first();
    // AGENT-GUARD: resolve through configFor(). Matching only `candidate.id`
    // never hit — modelIds() returns "<id>/<model>" — so config stayed
    // default-constructed and the provider's API key was never fetched:
    // every inline AI action against a key-based endpoint failed on auth.
    const ProviderConfig config = m_catalog->configFor(modelId).value_or(ProviderConfig{});
    auto *store = new SecretStore(this);
    store->retrieve(config.secretKey, [this, action, request, modelId, store, context,
                                      dialogParent](const QString &secret) {
        store->deleteLater();
        ChatProvider *provider = m_catalog->createProvider(modelId, secret, this);
        if (provider == nullptr) {
            return;
        }
        ChatRun *run = provider->start(request);
        auto *state = new QString;
        connect(run, &ChatRun::textDelta, this, [state](const QString &delta) { *state += delta; });
        connect(run, &ChatRun::finished, this,
                [this, action, context, state, dialogParent](const ChatResult &result) {
                    Q_UNUSED(result);
                    const QString proposal = extractProposal(*state);
                    delete state;
                    if (!proposal.isEmpty()) {
                        present(action, context.selection, proposal, dialogParent);
                    }
                });
        // Surface failures: several failure modes previously looked exactly
        // like "the button did nothing".
        connect(run, &ChatRun::failed, this, [this, state, dialogParent](const ChatError &error) {
            delete state;
            emit failed(error.message);
            ProposalPresenter presenter = m_presenter;
#ifdef QQO_WIDGETS_AI
            if (!presenter.failed) {
                presenter = widgetsProposalPresenter(dialogParent);
            }
#else
            Q_UNUSED(dialogParent);
#endif
            if (presenter.failed) {
                presenter.failed(error.message);
            }
        });
    });
}

} // namespace QindaOffice::Ai
