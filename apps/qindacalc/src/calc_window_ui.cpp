// SPDX-License-Identifier: GPL-3.0-or-later
// CalcWindow's interaction wiring: the status-bar aggregates, the grid
// context menu, the sheet tab bar's own gestures and the AI dock. Split out
// of calc_window.cpp to keep both inside the line budget.
//
// Rationale (D-015): the sheet tabs could only be switched — not renamed,
// reordered or removed; the grid had no context menu; nothing ever told the
// user the sum of what they had selected, which is the single most-used
// readout in a spreadsheet; and the AI menu offered an Assistant that was
// permanently disabled, which D-012 calls a defect, not a placeholder.

#include "calc_window.h"

#include "calc_structure_actions.h"
#include "spreadsheet_document.h"
#include "spreadsheet_view.h"

#include "assistant_panel.h"
#include "document_context.h"
#include "model_catalog.h"
#include "preferences.h"

#include "number_format.h"
#include "sheet.h"

#include <QDir>
#include <QDockWidget>
#include <QFont>
#include <QLocale>
#include <QMenu>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

// The selection readout is bounded: a whole-column selection can cover the
// used range, and walking millions of addresses on every cursor move would
// make the grid feel broken. The used range is what the header click gives,
// so this cap is generous in practice.
constexpr qint64 kMaxAggregateCells = 200000;

// Document context for the assistant: the selected block as TSV, or the
// whole used range. Keeps the panel's privacy contract intact — nothing
// leaves the machine until the user presses Send.
class CalcContext final : public Ai::DocumentContextProvider {
public:
    CalcContext(SpreadsheetDocument *document, SpreadsheetView *view)
        : m_document(document)
        , m_view(view)
    {
    }

    [[nodiscard]] Ai::DocumentContext context(bool withDocument) override
    {
        Ai::DocumentContext out;
        out.selection = rangeText(m_view->selectionRange());
        if (withDocument) {
            out.document = rangeText(m_view->usedRange());
        }
        return out;
    }

private:
    [[nodiscard]] QString rangeText(const CellRange &range) const
    {
        if (!range.isValid() || range.cellCount() > kMaxAggregateCells) {
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
    SpreadsheetView *m_view;
};

// Replies land in the cursor cell; a spreadsheet has no "insert at caret".
class CalcApplier final : public Ai::EditApplier {
public:
    CalcApplier(SpreadsheetDocument *document, SpreadsheetView *view)
        : m_document(document)
        , m_view(view)
    {
    }

    void insertAtCursor(const QString &text) override { replaceSelection(text); }
    void replaceSelection(const QString &text) override
    {
        m_document->applyInput(m_view->cursorCell(), text);
        m_view->refreshAll();
    }

private:
    SpreadsheetDocument *m_document;
    SpreadsheetView *m_view;
};

} // namespace

void CalcWindow::updateSelectionStatus()
{
    const CellRange range = m_view->selectionRange();
    const CellAddr cursor = m_view->cursorCell();
    const Workbook *wb = m_document->workbook();
    QString summary = addrText(cursor);
    if (range.cellCount() > 1) {
        summary = QStringLiteral("%1:%2").arg(addrText(range.topLeft), addrText(range.bottomRight));
    }
    if (range.cellCount() <= kMaxAggregateCells) {
        double sum = 0;
        qint64 numbers = 0;
        qint64 filled = 0;
        for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
            for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
                const Value value = wb->cellValue(m_document->activeSheet(), r, c);
                if (isBlank(value)) {
                    continue;
                }
                ++filled;
                if (const auto number = toNumber(value); number && isNumber(value)) {
                    sum += *number;
                    ++numbers;
                }
            }
        }
        const QLocale locale;
        if (numbers > 0) {
            summary += tr("   Sum: %1   Average: %2")
                           .arg(locale.toString(sum, 'g', 10),
                                locale.toString(sum / double(numbers), 'g', 10));
        }
        if (filled > 0) {
            summary += tr("   Count: %1").arg(filled);
        }
    }
    summary += tr("   Zoom: %1%").arg(qRound(m_view->zoom() * 100));
    setStatusInfo(summary);
}

void CalcWindow::wireGridInteraction()
{
    connect(m_view, &SpreadsheetView::selectionChanged, this,
            [this](const CellRange &) { updateSelectionStatus(); });
    connect(m_view, &SpreadsheetView::contextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.setObjectName(QStringLiteral("calcGridContextMenu"));
        m_structure->populateContextMenu(&menu);
        menu.exec(pos);
    });
    updateSelectionStatus();
}

void CalcWindow::wireSheetTabs()
{
    // Reordering by drag is the gesture people reach for first; the model
    // move has to follow it or the tabs and the workbook disagree.
    m_tabs->setMovable(true);
    m_tabs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs, &QTabBar::customContextMenuRequested, this, [this](const QPoint &pos) {
        m_structure->showSheetTabMenu(m_tabs->tabAt(pos), m_tabs->mapToGlobal(pos));
    });
    connect(m_tabs, &QTabBar::tabBarDoubleClicked, this,
            [this](int index) { m_structure->renameSheet(index); });
    connect(m_tabs, &QTabBar::tabMoved, this, [this](int from, int to) {
        // AGENT-GUARD: rebuildSheetTabs() runs on structureChanged and would
        // fight the drag, so move the model and let the rebuild settle it.
        QSignalBlocker blocker(m_tabs);
        m_document->moveSheet(qint32(from), qint32(to));
    });
}

void CalcWindow::wireAssistant()
{
    // Same shape as QindaWrite's and QindaShow's: a per-window catalog, a
    // right-hand dock, the standard Assistant… action toggling it. Before
    // this, QindaCalc's AI menu held one permanently disabled entry.
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/qindaoffice/QindaCalc");
    QDir().mkpath(configRoot);
    m_catalog = new Ai::ModelCatalog(configRoot + QStringLiteral("/ai-providers.json"));
    m_catalog->setParent(this);
    m_panel = new Ai::AssistantPanel(m_catalog, new CalcContext(m_document, m_view),
                                     new CalcApplier(m_document, m_view), this);
    auto *dock = new QDockWidget(tr("Assistant"), this);
    dock->setObjectName(QStringLiteral("assistantDock"));
    dock->setWidget(m_panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->hide();
    m_actions.aiAssistant->setEnabled(true);
    m_actions.aiAssistant->setCheckable(true);
    connect(m_actions.aiAssistant, &QAction::toggled, dock, &QDockWidget::setVisible);
    connect(dock, &QDockWidget::visibilityChanged, m_actions.aiAssistant, &QAction::setChecked);
}

void CalcWindow::applyPreferences()
{
    const Shell::Preferences &prefs = Shell::Preferences::instance();
    QFont cellFont = m_view->font();
    if (!prefs.documentFontFamily().isEmpty()) {
        cellFont.setFamily(prefs.documentFontFamily());
    }
    if (prefs.documentFontSize() > 0) {
        cellFont.setPointSizeF(prefs.documentFontSize());
    }
    m_view->setFont(cellFont);
    m_view->setZoom(prefs.defaultZoomPercent() / 100.0);
    m_view->refreshAll();
    updateSelectionStatus();
}

} // namespace QindaOffice::Apps::QindaCalc
