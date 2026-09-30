// SPDX-License-Identifier: GPL-3.0-or-later
// NoteHost's assistant (conversion plan §3.8): the model catalog, the
// AssistantController the QML panel binds to, and the two document seams.
// Context is the live editor's selection and the page's typed blocks;
// ink is not text and is deliberately never sent. Replies go into the
// editor the window owns (assistantInsertRequested /
// assistantReplaceRequested), which opens a fresh text block when none
// is being edited, so Insert is never a silent no-op. With "Edit the
// document" on (D-058) the assistant changes the page through NoteTools.

#include "note_host.h"

#include "note_blocks.h"
#include "note_page_item.h"
#include "note_tools.h"
#include "note_agent.h"
#include "note_evidence.h"
#include "standard_actions.h"

#include "model_catalog.h"
#include "paths.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>

namespace QindaOffice::Apps::Notes {

using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

namespace {

class NoteContext final : public Ai::DocumentContextProvider {
public:
    explicit NoteContext(NoteHost *host)
        : m_host(host)
    {
    }
    [[nodiscard]] Ai::DocumentContext context(bool withDocument) override
    {
        Ai::DocumentContext out;
        out.selection = m_host->editorSelection();
        if (withDocument) {
            out.document = noteEvidence(m_host, 60000);
        }
        return out;
    }

private:
    NoteHost *m_host;
};

class NoteApplier final : public Ai::EditApplier {
public:
    explicit NoteApplier(NoteHost *host)
        : m_host(host)
    {
    }
    void insertAtCursor(const QString &text) override { emit m_host->assistantInsertRequested(text); }
    void replaceSelection(const QString &text) override
    {
        emit m_host->assistantReplaceRequested(text);
    }

private:
    NoteHost *m_host;
};

} // namespace

void NoteHost::createAssistant()
{
    const QString configRoot = Core::configDirFor(Core::appLeaf());
    QDir().mkpath(configRoot);
    m_catalog = new Ai::ModelCatalog(configRoot + QStringLiteral("/ai-providers.json"), this);
    m_aiContext = std::make_unique<NoteContext>(this);
    m_aiApplier = std::make_unique<NoteApplier>(this);
    m_assistant = new Ai::AssistantController(m_catalog, m_aiContext.get(), m_aiApplier.get(), this);
    // D-058: the assistant edits the open page itself (one undo step).
    m_aiTools = std::make_unique<NoteTools>(this);
    m_assistant->setDocumentTools(m_aiTools.get());
    m_assistant->setDocumentScopeLabel(tr("Page + speech"));
    m_assistant->setScope(2); // no request is sent until the human presses Send
    auto *tools = static_cast<NoteTools *>(m_aiTools.get());
    auto *agent = new NoteAgent(this, tools);
    setAgentProvider(agent);
    tools->setRevisionProvider([agent] { return agent->revision(); },
        [agent] { return agent->undoRevisionToken(); }, [agent] { return agent->blockedReason(); });
    ActionEntry *toggle = action(QString::fromLatin1(ActionId::kAiAssistant));
    toggle->setEnabled(true);
    toggle->setCheckable(true);
    connect(toggle, &ActionEntry::toggled, this,
            [this](bool visible) { emit assistantToggleRequested(visible); });
}

void NoteHost::setEditorSelection(const QString &text)
{
    if (m_editorSelection == text) {
        return;
    }
    m_editorSelection = text;
    emit editorSelectionChanged();
}

QString NoteHost::currentPageText() const
{
    const NotePage *page = currentPage();
    if (page == nullptr) {
        return {};
    }
    QStringList blocks;
    for (const QJsonValue &value : blocksArray(page->envelope.blocksJson)) {
        const QJsonObject object = value.toObject();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text") || type == QLatin1String("check")
            || type == QLatin1String("markdown")) {
            blocks.append(object.value(QStringLiteral("text")).toString());
        }
    }
    return blocks.join(QLatin1Char('\n'));
}

} // namespace QindaOffice::Apps::Notes
