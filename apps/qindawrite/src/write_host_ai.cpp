// SPDX-License-Identifier: GPL-3.0-or-later
// WriteHost's assistant (conversion plan §3.8): the model catalog, the
// AssistantController the QML panel binds to, the two document seams
// over the control, and the inline actions (Rewrite, Shorten, …) whose
// proposal the window shows in Ai.ProposalDialog through
// proposalRequested / applyProposal / regenerateProposal. Privacy
// contract unchanged: nothing leaves the machine until Send.

#include "write_host.h"

#include "page_text_control.h"
#include "standard_actions.h"

#include "inline_action_flow.h"
#include "model_catalog.h"
#include "paths.h"

#include <QDir>
#include <QTextCursor>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

namespace {

class WriteContext final : public Ai::DocumentContextProvider {
public:
    WriteContext(PageTextControl *control, WriteDocument *document)
        : m_control(control)
        , m_document(document)
    {
    }
    [[nodiscard]] Ai::DocumentContext context(bool withDocument) override
    {
        Ai::DocumentContext out;
        const QTextCursor cursor = m_control->textCursor();
        if (cursor.hasSelection()) {
            out.selection = cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        }
        if (withDocument) {
            out.document = m_document->textDocument()->toPlainText();
        }
        return out;
    }

private:
    PageTextControl *m_control;
    WriteDocument *m_document;
};

class WriteApplier final : public Ai::EditApplier {
public:
    WriteApplier(PageTextControl *control, WriteDocument *document)
        : m_control(control)
        , m_document(document)
    {
    }
    void replaceSelection(const QString &text) override
    {
        QTextCursor cursor = m_control->textCursor();
        cursor.insertText(text);
        m_control->setTextCursor(cursor);
        m_document->setDirty(true);
    }
    void insertAtCursor(const QString &text) override
    {
        // AGENT-GUARD: insert at the live caret — an earlier release moved
        // to End first, so every AI "Insert" landed at document end.
        QTextCursor cursor = m_control->textCursor();
        cursor.beginEditBlock();
        if (!cursor.atBlockStart()) {
            cursor.insertText(QStringLiteral("\n"));
        }
        cursor.insertText(text);
        cursor.endEditBlock();
        m_control->setTextCursor(cursor);
        m_document->setDirty(true);
    }

private:
    PageTextControl *m_control;
    WriteDocument *m_document;
};

} // namespace

void WriteHost::createAssistant()
{
    const QString configRoot = Core::configDirFor(Core::appLeaf());
    QDir().mkpath(configRoot);
    m_catalog = new Ai::ModelCatalog(configRoot + QStringLiteral("/ai-providers.json"), this);
    m_aiContext = std::make_unique<WriteContext>(m_control, m_document);
    m_aiApplier = std::make_unique<WriteApplier>(m_control, m_document);
    m_assistant = new Ai::AssistantController(m_catalog, m_aiContext.get(), m_aiApplier.get(), this);
    ActionEntry *toggle = action(QString::fromLatin1(ActionId::kAiAssistant));
    toggle->setEnabled(true);
    toggle->setCheckable(true);
    connect(toggle, &ActionEntry::toggled, this,
            [this](bool visible) { emit assistantToggleRequested(visible); });

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

void WriteHost::applyProposal(const QString &text)
{
    // What Apply hands back is what the user saw, edits included.
    if (!text.isEmpty()) {
        m_aiApplier->replaceSelection(text);
    }
    m_proposalApply = nullptr;
    m_proposalRegenerate = nullptr;
}

void WriteHost::regenerateProposal()
{
    const std::function<void()> regenerate = std::move(m_proposalRegenerate);
    m_proposalApply = nullptr;
    m_proposalRegenerate = nullptr;
    if (regenerate) {
        regenerate();
    }
}

void WriteHost::discardProposal()
{
    m_proposalApply = nullptr;
    m_proposalRegenerate = nullptr;
}

} // namespace QindaOffice::Apps::Write
