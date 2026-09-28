// SPDX-License-Identifier: GPL-3.0-or-later
// ShowHost's assistant (conversion plan §3.8): the model catalog, the
// AssistantController the QML panel binds to, the deck-outline seams
// (show_ai.h), the inline actions whose proposal the window shows in
// Ai.ProposalDialog, and deck generation from an outline the window asks
// for. Privacy contract unchanged: nothing leaves the machine until Send.

#include "show_host.h"
#include "show_tools.h"

#include "show_ai.h"
#include "standard_actions.h"

#include "inline_action_flow.h"
#include "model_catalog.h"
#include "paths.h"

#include <QDir>

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

void ShowHost::createAssistant()
{
    const QString configRoot = Core::configDirFor(Core::appLeaf());
    QDir().mkpath(configRoot);
    m_catalog = new Ai::ModelCatalog(configRoot + QStringLiteral("/ai-providers.json"), this);
    m_aiContext = std::make_unique<ShowContext>(this);
    m_aiApplier = std::make_unique<ShowApplier>(this);
    m_assistant = new Ai::AssistantController(m_catalog, m_aiContext.get(), m_aiApplier.get(), this);
    // D-058: the assistant rewrites, adds and reorders slides itself.
    m_aiTools = std::make_unique<ShowTools>(this);
    m_assistant->setDocumentTools(m_aiTools.get());
    ActionEntry *toggle = action(QString::fromLatin1(ActionId::kAiAssistant));
    toggle->setEnabled(true);
    toggle->setCheckable(true);
    connect(toggle, &ActionEntry::toggled, this, [this](bool visible) { emit assistantToggleRequested(visible); });

    m_inline = new Ai::InlineActionFlow(m_catalog, m_aiContext.get(), m_aiApplier.get(), this);
    Ai::ProposalPresenter presenter;
    presenter.show = [this](const QString &original, const QString &proposal, std::function<void()> apply,
                            std::function<void()> regenerate) {
        m_proposalApply = std::move(apply);
        m_proposalRegenerate = std::move(regenerate);
        emit proposalRequested(original, proposal);
    };
    presenter.failed = [this](const QString &message) { showStatusMessage(message, 8000); };
    m_inline->setPresenter(std::move(presenter));
}

void ShowHost::generateDeck(const QString &outline)
{
    if (!generateDeckFromOutline(this, m_catalog, outline)) {
        showStatusMessage(tr("No AI providers are configured."), 8000);
    }
}

void ShowHost::applyProposal(const QString &text)
{
    if (!text.isEmpty()) {
        m_aiApplier->replaceSelection(text);
    }
    m_proposalApply = nullptr;
    m_proposalRegenerate = nullptr;
}

void ShowHost::regenerateProposal()
{
    const std::function<void()> regenerate = std::move(m_proposalRegenerate);
    m_proposalApply = nullptr;
    m_proposalRegenerate = nullptr;
    if (regenerate) {
        regenerate();
    }
}

void ShowHost::discardProposal()
{
    m_proposalApply = nullptr;
    m_proposalRegenerate = nullptr;
}

} // namespace QindaOffice::Apps::Show
