// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's assistant (conversion plan §3.8): the model catalog, the
// AssistantController the QML panel binds to, and the two document seams.
// Context is the selected block as TSV, or the whole used range; replies
// land in the cursor cell (a spreadsheet has no "insert at caret").
// Privacy contract unchanged: nothing leaves the machine until Send.

#include "calc_host.h"
#include "calc_tools.h"

#include "spreadsheet_item.h"
#include "standard_actions.h"

#include "model_catalog.h"
#include "paths.h"

#include <QDir>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

namespace {

constexpr qint64 kMaxContextCells = 200000;

class CalcContext final : public Ai::DocumentContextProvider {
public:
    CalcContext(SpreadsheetDocument *document, CalcHost *host)
        : m_document(document)
        , m_host(host)
    {
    }
    [[nodiscard]] Ai::DocumentContext context(bool withDocument) override
    {
        Ai::DocumentContext out;
        const SpreadsheetItem *grid = m_host->grid();
        if (grid == nullptr) {
            return out;
        }
        out.selection = rangeText(grid->selectionRange());
        if (withDocument) {
            out.document = rangeText(grid->usedRange());
        }
        return out;
    }

private:
    [[nodiscard]] QString rangeText(const CellRange &range) const
    {
        if (!range.isValid() || range.cellCount() > kMaxContextCells) {
            return {};
        }
        QStringList rows;
        for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
            QStringList cells;
            for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
                cells.append(m_document->displayTextAt({r, c}));
            }
            rows.append(cells.join(QLatin1Char('\t')));
        }
        return rows.join(QLatin1Char('\n'));
    }

    SpreadsheetDocument *m_document;
    CalcHost *m_host;
};

class CalcApplier final : public Ai::EditApplier {
public:
    CalcApplier(SpreadsheetDocument *document, CalcHost *host)
        : m_document(document)
        , m_host(host)
    {
    }
    void insertAtCursor(const QString &text) override { replaceSelection(text); }
    void replaceSelection(const QString &text) override
    {
        SpreadsheetItem *grid = m_host->grid();
        const CellAddr at = grid != nullptr ? grid->cursorCell() : CellAddr{0, 0};
        m_document->applyInput(at, text);
        if (grid != nullptr) {
            grid->refreshAll();
        }
    }

private:
    SpreadsheetDocument *m_document;
    CalcHost *m_host;
};

} // namespace

void CalcHost::createAssistant()
{
    const QString configRoot = Core::configDirFor(Core::appLeaf());
    QDir().mkpath(configRoot);
    m_catalog = new Ai::ModelCatalog(configRoot + QStringLiteral("/ai-providers.json"), this);
    m_aiContext = std::make_unique<CalcContext>(m_document, this);
    m_aiApplier = std::make_unique<CalcApplier>(m_document, this);
    m_assistant = new Ai::AssistantController(m_catalog, m_aiContext.get(), m_aiApplier.get(), this);
    // D-058: the assistant fills and fixes cells itself (one undo step).
    m_aiTools = std::make_unique<CalcTools>(m_document, this);
    m_assistant->setDocumentTools(m_aiTools.get());
    ActionEntry *toggle = action(QString::fromLatin1(ActionId::kAiAssistant));
    toggle->setEnabled(true);
    toggle->setCheckable(true);
    connect(toggle, &ActionEntry::toggled, this,
            [this](bool visible) { emit assistantToggleRequested(visible); });
}

} // namespace QindaOffice::Apps::QindaCalc
