// SPDX-License-Identifier: GPL-3.0-or-later
#include "inline_action_flow.h"

#include "model_catalog.h"
#include "secret_store.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPointer>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

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

ProposalDialog::ProposalDialog(const QString &original, const QString &proposal, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("AI Proposal"));
    auto *layout = new QVBoxLayout(this);
    auto *columns = new QHBoxLayout;
    m_original = new QTextEdit(this);
    m_original->setPlainText(original);
    m_original->setReadOnly(true);
    m_proposal = new QTextEdit(this);
    m_proposal->setPlainText(proposal);
    columns->addWidget(m_original);
    columns->addWidget(m_proposal);
    layout->addLayout(columns);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *apply = buttons->addButton(QDialogButtonBox::Apply);
    QPushButton *regenerate = buttons->addButton(tr("Regenerate"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Discard);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(regenerate, &QPushButton::clicked, this, [this] {
        emit regenerateRequested();
        // AGENT-GUARD: Regenerate must NOT close as Accepted — the caller
        // applies the proposal on Accepted, which replaced the selection
        // with exactly the text the user asked to regenerate.
        done(QDialog::Rejected);
    });
    connect(apply, &QPushButton::clicked, this, [this] {
        done(QDialog::Accepted);
    });
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

void InlineActionFlow::run(const QString &actionId, QWidget *dialogParent)
{
    for (const InlineAction &action : inlineActions()) {
        if (action.id == actionId) {
            m_pending = action;
            execute(action, dialogParent);
            return;
        }
    }
}

void InlineActionFlow::execute(const InlineAction &action, QWidget *dialogParent)
{
    if (dialogParent == nullptr) {
        dialogParent = qobject_cast<QWidget *>(parent());
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
    ProviderConfig config;
    for (const ProviderConfig &candidate : m_catalog->providers()) {
        if (modelId == candidate.id) {
            config = candidate;
        }
    }
    auto *store = new SecretStore(this);
    store->retrieve(config.secretKey, [this, action, request, modelId, store, context, dialogParent](const QString &secret) {
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
                    if (proposal.isEmpty()) {
                        return;
                    }
                    // AGENT-GUARD: the dialog must be parented — a parentless
                    // dialog outlived this flow, and the running exec()
                    // nested loop then dereferenced a destroyed `this`
                    // through the applier call below (use-after-free).
                    auto *dialog = new ProposalDialog(context.selection, proposal, dialogParent);
                    connect(dialog, &ProposalDialog::regenerateRequested, this,
                            [this, action, dialogParent] { execute(action, dialogParent); });
                    const QPointer<ProposalDialog> dialogGuard(dialog);
                    if (dialog->exec() == QDialog::Accepted && dialogGuard != nullptr) {
                        m_applier->replaceSelection(proposal);
                    }
                    delete dialog;
                });
        // Surface failures: several failure modes previously looked exactly
        // like "the button did nothing".
        connect(run, &ChatRun::failed, this,
                [state, dialogParent](const ChatError &error) {
                    delete state;
                    if (dialogParent != nullptr) {
                        QMessageBox::warning(dialogParent, QObject::tr("AI action failed"),
                                             error.message);
                    }
                });
    });
}

} // namespace QindaOffice::Ai
