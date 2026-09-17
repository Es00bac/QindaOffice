// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_window.h"

#include "spreadsheet_document.h"
#include "spreadsheet_view.h"
#include "template_loader.h"

#ifdef QQO_WITH_CHARTS
#include "chart_dialog.h"
#endif

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabBar>
#include <QVBoxLayout>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QIcon>
#include <QPainter>
#include <QPrinter>
#include <QLayout>
#include <QToolBar>
#include <QVBoxLayout>

#include "calc_clipboard.h"
#include "calc_find_bar.h"
#include "calc_format_controller.h"
#include "calc_rules_actions.h"
#include "calc_structure_actions.h"
#include "print_helper.h"
#include "standard_actions.h"
#include "wrapping_tool_bar.h"
#include <QVBoxLayout>
#include <QWidget>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {
// Turns a template display name into a stable, valid objectName suffix.
QString sanitizedId(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        out.append(c.isLetterOrNumber() ? c : QLatin1Char('_'));
    }
    return out;
}
} // namespace

CalcWindow::CalcWindow()
    : m_document(new SpreadsheetDocument)
    , m_view(new SpreadsheetView(m_document))
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *bar = new QWidget(central);
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(4, 2, 4, 2);
    m_nameBox = new QLineEdit(bar);
    m_nameBox->setFixedWidth(72);
    m_nameBox->setAlignment(Qt::AlignCenter);
    m_formulaBar = new QLineEdit(bar);
    barLayout->addWidget(m_nameBox);
    barLayout->addWidget(m_formulaBar, 1);
    layout->addWidget(bar);

    layout->addWidget(m_view, 1);

    m_tabs = new QTabBar(central);
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    layout->addWidget(m_tabs);

    m_findBar = new CalcFindBar(m_document, m_view, central);
    m_findBar->hide();
    layout->addWidget(m_findBar);

    setDocumentHost(central, m_document);

    connect(m_view, &SpreadsheetView::cursorChanged, this,
            [this](const CellAddr &addr, const QString &formulaText, const QString &) {
                m_syncingBar = true;
                m_nameBox->setText(addrText(addr));
                m_formulaBar->setText(formulaText);
                m_syncingBar = false;
            });
    connect(m_view, &SpreadsheetView::editCommitted, this, &CalcWindow::commitInput);
    connect(m_formulaBar, &QLineEdit::returnPressed, this, [this] {
        if (!m_syncingBar) {
            m_view->commitFormulaBar(m_formulaBar->text());
        }
    });
    connect(m_nameBox, &QLineEdit::returnPressed, this, [this] {
        if (!m_syncingBar) {
            commitNameBox(m_nameBox->text().trimmed());
        }
    });
    m_nameBox->setToolTip(tr("Go to a cell or named range, or name the selection"));
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0) {
            m_document->setActiveSheet(index);
            m_view->refreshAll();
        }
    });
    rebuildSheetTabs();

    createCalcActions();
    Shell::WrappingToolBar *mainToolBar = addMainToolBar();
    if (m_insertChartAction != nullptr) {
        mainToolBar->addStripSeparator();
        mainToolBar->addStripAction(m_insertChartAction);
    }
    mainToolBar->addStripAction(m_insertSheetAction);

    m_format = new CalcFormatController(m_document, m_view, this);
    m_structure = new CalcStructureActions(this, m_document, m_view);
    m_rules = new CalcRuleActions(this, m_document, m_view);
    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(Qt::TopToolBarArea, m_format->toolBar());
    m_format->appendStripActions(m_structure->stripActions() + m_rules->stripActions());

    wireEditingActions();
    wireGridInteraction();
    wireSheetTabs();
    wireAssistant();
    connect(m_findBar, &CalcFindBar::message, this,
            [this](const QString &text) { statusBar()->showMessage(text, 5000); });
    // A lossy-but-successful save (CSV keeps one sheet) has to be visible.
    connect(m_document, &SpreadsheetDocument::saveWarning, this,
            [this](const QString &text) { statusBar()->showMessage(text, 12000); });
    // Undo of a sheet insert/delete (or a load) changes the sheet list.
    connect(m_document, &SpreadsheetDocument::structureChanged, this, [this] {
        rebuildSheetTabs();
        m_view->refreshAll();
        rebuildChartWindows();
    });
    // Data edits change what charts visualize, so the open chart windows
    // must re-render — but only their CONTENTS. AGENT-GUARD: never call
    // rebuildChartWindows() from contentChanged. It deletes and re-creates
    // the floating windows, so every committed cell edit made each chart
    // window blink out and come back at its computed default position,
    // discarding wherever the user had moved or resized it (and re-raising
    // it over the grid they were typing in).
    connect(m_document, &SpreadsheetDocument::contentChanged, this, [this] {
        refreshChartWindows();
    });

    // App menus (Insert/Sheet/Format) need the fully constructed window.
    finalizeAppMenuHooks();
}

void CalcWindow::wireEditingActions()
{
    using namespace Shell::ActionId;
    QAction *undo = m_actions.editUndo;
    QAction *redo = m_actions.editRedo;
    undo->setEnabled(false);
    redo->setEnabled(false);
    connect(m_document->undoStack(), &QUndoStack::canUndoChanged, undo, &QAction::setEnabled);
    connect(m_document->undoStack(), &QUndoStack::canRedoChanged, redo, &QAction::setEnabled);
    connect(undo, &QAction::triggered, m_document->undoStack(), &QUndoStack::undo);
    connect(redo, &QAction::triggered, m_document->undoStack(), &QUndoStack::redo);

    m_actions.editCut->setEnabled(true);
    m_actions.editCopy->setEnabled(true);
    connect(m_actions.editCut, &QAction::triggered, this, [this] {
        cutRangeToClipboard(m_document, m_view->selectionRange());
        m_view->refreshAll();
    });
    connect(m_actions.editCopy, &QAction::triggered, this, [this] {
        copyRangeToClipboard(m_document, m_view->selectionRange());
    });
    m_actions.editPaste->setEnabled(false);
    auto updatePaste = [this] {
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        m_actions.editPaste->setEnabled(mime != nullptr
                                        && (mime->hasText()
                                            || mime->hasFormat(
                                                QString::fromLatin1(richCellsMimeType()))));
    };
    updatePaste();
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, updatePaste);
    connect(m_actions.editPaste, &QAction::triggered, this, [this] {
        if (pasteClipboardAt(m_document, m_view->cursorCell())) {
            m_view->refreshAll();
        }
    });
    m_actions.editSelectAll->setEnabled(true);
    connect(m_actions.editSelectAll, &QAction::triggered, m_view,
            &SpreadsheetView::selectUsedRange);

    // Zoom: created disabled and never wired, so View > Zoom sat greyed out.
    for (QAction *action : {m_actions.viewZoomIn, m_actions.viewZoomOut,
                            m_actions.viewZoomReset}) {
        action->setEnabled(true);
    }
    connect(m_actions.viewZoomIn, &QAction::triggered, this,
            [this] { m_view->setZoom(m_view->zoom() * 1.25); });
    connect(m_actions.viewZoomOut, &QAction::triggered, this,
            [this] { m_view->setZoom(m_view->zoom() / 1.25); });
    connect(m_actions.viewZoomReset, &QAction::triggered, this,
            [this] { m_view->setZoom(1.0); });

    // Print and preview share the grid renderer, so paper matches the sheet.
    m_actions.filePrint->setEnabled(true);
    connect(m_actions.filePrint, &QAction::triggered, this,
            [this] { Shell::PrintHelper::print(this, printRenderCallback()); });
    m_actions.filePrintPreview->setEnabled(true);
    connect(m_actions.filePrintPreview, &QAction::triggered, this,
            [this] { Shell::PrintHelper::printPreview(this, printRenderCallback()); });

    m_actions.editFind->setEnabled(true);
    m_actions.editFindNext->setEnabled(true);
    m_actions.editFindPrevious->setEnabled(true);
    m_actions.editReplace->setEnabled(true);
    connect(m_actions.editFind, &QAction::triggered, m_findBar, &CalcFindBar::openFind);
    connect(m_actions.editReplace, &QAction::triggered, m_findBar, &CalcFindBar::openReplace);
    connect(m_actions.editFindNext, &QAction::triggered, m_findBar, &CalcFindBar::findNext);
    connect(m_actions.editFindPrevious, &QAction::triggered, m_findBar,
            &CalcFindBar::findPrevious);
}

CalcWindow::~CalcWindow() = default;

void CalcWindow::createCalcActions()
{
    // Persistent named actions (action discipline, AGENTS.md rule 5) shared
    // by the menus and the main toolbar.
    m_insertChartAction = new QAction(QIcon::fromTheme(QStringLiteral("insert-chart")),
                                      tr("&Chart…"), this);
    m_insertChartAction->setObjectName(
        QString::fromLatin1(Shell::ActionId::kInsertChart));
    connect(m_insertChartAction, &QAction::triggered, this, [this] { insertChart(); });
#ifndef QQO_WITH_CHARTS
    m_insertChartAction->setEnabled(false);
#endif

    m_insertSheetAction = new QAction(QIcon::fromTheme(QStringLiteral("list-add")),
                                      tr("&Insert Sheet"), this);
    m_insertSheetAction->setObjectName(
        QString::fromLatin1(Shell::ActionId::kInsertSheet));
    connect(m_insertSheetAction, &QAction::triggered, this, [this] {
        // Undoable at the document layer; structureChanged rebuilds the tabs.
        m_document->insertSheet(tr("Sheet%1").arg(m_document->workbook()->sheetCount() + 1));
        emit m_view->sheetCountChanged();
    });

    m_thousandsAction = new QAction(tr("Number format: #,##0.00"), this);
    m_thousandsAction->setObjectName(QStringLiteral("calcThousandsFormatAction"));
    // QToolButton labels itself from iconText(), so the strip gets the short
    // form while the menu keeps the full sentence. The long one was the
    // widest thing on the main toolbar by a wide margin.
    m_thousandsAction->setIconText(QStringLiteral("#,##0.00"));
    connect(m_thousandsAction, &QAction::triggered, this, [this] {
        // Route through restyleRange (undoable, journaled, contentChanged) —
        // the direct mutation here bypassed Ctrl+Z and the recovery journal.
        StylePatch patch;
        patch.numberFormat = QStringLiteral("#,##0.00");
        const CellAddr addr = m_view->cursorCell();
        m_document->restyleRange({addr, addr}, patch, tr("Number format"));
        m_view->viewport()->update();
    });
}

void CalcWindow::loadWorkbookPath(const QString &path)
{
    QString error;
    if (m_document->load(path, &error)) {
        m_document->adoptPath(path);
        rebuildSheetTabs();
        m_view->refreshAll();
        statusBar()->showMessage(tr("Opened %1").arg(path), 5000);
    } else {
        statusBar()->showMessage(tr("Open failed: %1").arg(error), 8000);
    }
}




void CalcWindow::commitInput(const CellAddr &addr, const QString &input)
{
    m_document->applyInput(addr, input);
    m_view->refreshAll();
}

void CalcWindow::rebuildSheetTabs()
{
    QSignalBlocker blocker(m_tabs);
    while (m_tabs->count() > 0) {
        m_tabs->removeTab(0);
    }
    const Workbook *wb = m_document->workbook();
    for (qint32 i = 0; i < wb->sheetCount(); ++i) {
        m_tabs->addTab(wb->sheetName(i));
    }
    m_tabs->setCurrentIndex(int(m_document->activeSheet()));
}

void CalcWindow::insertChart()
{
#ifdef QQO_WITH_CHARTS
    Workbook *wb = m_document->workbook();
    const CalcEng::CellAddr cursor = m_view->cursorCell();
    // Engine colName: the local lambda here mis-named columns that were
    // multiples of 26 (col 25 -> "AZ" instead of "Z"), preselecting wrong
    // ranges in the dialog.
    const auto colName = [](qint32 col) { return CalcEng::colName(col); };
    const QString initial = QStringLiteral("%1%2:%3%4")
        .arg(colName(cursor.col), QString::number(cursor.row + 1),
             colName(cursor.col + 1), QString::number(cursor.row + 6));
    ChartDialog dialog(wb, initial, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    CalcEng::ChartSpec spec = dialog.spec();
    spec.sheet = m_document->activeSheet();
    spec.anchorRow = cursor.row;
    spec.anchorCol = cursor.col;
    // Route through the document: snapshot + undo command + dirty + notify.
    // Appending into Workbook::charts directly left chart insertion outside
    // Ctrl+Z and outside the recovery journal. structureChanged() rebuilds
    // the chart windows, so there is nothing to do here afterwards.
    m_document->addChart(spec);
#endif
}

void CalcWindow::rebuildChartWindows()
{
#ifdef QQO_WITH_CHARTS
    qDeleteAll(m_chartWindows);
    m_chartWindows.clear();
    const Workbook *wb = m_document->workbook();
    for (const CalcEng::ChartSpec &spec : wb->charts) {
        // AGENT-GUARD (ADR-0117): a chart window is an ordinary Tool window.
        // Size and position are set BEFORE show() and never touched after —
        // the desktop's window containers own grouped geometry afterwards.
        // No WindowStaysOnTopHint: forcing stacking fights the compositor
        // and is not honoured on Wayland anyway.
        auto *floating = new QWidget(this, Qt::Tool);
        floating->setWindowTitle(spec.title.isEmpty() ? tr("Chart") : spec.title);
        auto *layout = new QVBoxLayout(floating);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(chartWidgetFor(spec, *wb, floating));
        floating->resize(spec.widthPx, spec.heightPx);
        floating->move(140 + spec.anchorCol * 24, 120 + spec.anchorRow * 22);
        floating->show();
        m_chartWindows.append(floating);
    }
#endif
}

void CalcWindow::refreshChartWindows()
{
#ifdef QQO_WITH_CHARTS
    const Workbook *wb = m_document->workbook();
    if (m_chartWindows.size() != wb->charts.size()) {
        // The chart list itself changed under us; a full rebuild is the only
        // consistent answer (structureChanged normally gets here first).
        rebuildChartWindows();
        return;
    }
    for (int i = 0; i < m_chartWindows.size(); ++i) {
        QWidget *floating = m_chartWindows.at(i);
        if (floating == nullptr || floating->layout() == nullptr) {
            continue;
        }
        // Swap only the chart widget: the window keeps its identity, its
        // geometry and its place in whatever container the user put it in.
        QLayout *layout = floating->layout();
        while (QLayoutItem *item = layout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        const CalcEng::ChartSpec &spec = wb->charts.at(i);
        floating->setWindowTitle(spec.title.isEmpty() ? tr("Chart") : spec.title);
        layout->addWidget(chartWidgetFor(spec, *wb, floating));
    }
#endif
}

void CalcWindow::insertFileMenuActions(QMenu *fileMenu)
{
    const QList<Shell::TemplateEntry> templates = Shell::templatesFor(QStringLiteral("QindaCalc"));
    if (templates.isEmpty()) {
        return;
    }
    // AGENT-GUARD: insert before the shell's File-menu anchor. addMenu()
    // appends, which landed "New from Template" BELOW Quit.
    auto *submenu = new QMenu(tr("New from &Template"), this);
    submenu->setObjectName(QStringLiteral("fileTemplateMenu"));
    fileMenu->insertMenu(fileMenuAnchor(), submenu);
    for (const Shell::TemplateEntry &entry : templates) {
        QAction *item = submenu->addAction(entry.name);
        // AGENT-GUARD: every menu command needs a stable objectName — the
        // desktop global menu resolves activations exclusively by it, so an
        // unnamed action is dead from the panel (AGENTS.md rule 5).
        item->setObjectName(QStringLiteral("calcTemplate_%1").arg(sanitizedId(entry.name)));
        connect(item, &QAction::triggered, this, [this, path = entry.path] {
            // Loading a template replaces the workbook: ask first when the
            // current one has unsaved changes (same contract as closing).
            if (!confirmDiscardChanges()) {
                return;
            }
            QString error;
            if (!m_document->load(path, &error)) {
                // Fail honestly: the old code swallowed the error, so a
                // broken template looked exactly like a no-op click.
                statusBar()->showMessage(tr("Template failed: %1").arg(error), 8000);
                return;
            }
            m_document->adoptPath(QString());
            m_document->setDirty(true);
            rebuildSheetTabs();
            m_view->refreshAll();
            rebuildChartWindows();
        });
    }
}


void CalcWindow::insertApplicationMenus(QMenu *before)
{
    auto *insertMenu = new QMenu(tr("&Insert"), this);
    insertMenu->setObjectName(QStringLiteral("calcInsertMenu"));
    menuBar()->insertMenu(before->menuAction(), insertMenu);
    m_structure->populateInsertMenu(insertMenu);
    insertMenu->addSeparator();
    insertMenu->addAction(action(QString::fromLatin1(Shell::ActionId::kCalcComment)));
#ifdef QQO_WITH_CHARTS
    insertMenu->addSeparator();
    insertMenu->addAction(m_insertChartAction);
#endif

    auto *formatMenu = new QMenu(tr("F&ormat"), this);
    formatMenu->setObjectName(QStringLiteral("calcFormatMenu"));
    menuBar()->insertMenu(before->menuAction(), formatMenu);
    m_format->populateFormatMenu(formatMenu);
    formatMenu->addSeparator();
    formatMenu->addAction(m_thousandsAction);
    formatMenu->addSeparator();
    m_structure->populateFormatMenu(formatMenu);
    formatMenu->addSeparator();
    m_rules->populateFormatMenu(formatMenu);

    auto *dataMenu = new QMenu(tr("&Data"), this);
    dataMenu->setObjectName(QStringLiteral("calcDataMenu"));
    menuBar()->insertMenu(before->menuAction(), dataMenu);
    m_structure->populateDataMenu(dataMenu);
    dataMenu->addSeparator();
    m_rules->populateDataMenu(dataMenu);

    auto *sheetMenu = new QMenu(tr("&Sheet"), this);
    sheetMenu->setObjectName(QStringLiteral("calcSheetMenu"));
    menuBar()->insertMenu(before->menuAction(), sheetMenu);
    sheetMenu->addAction(m_insertSheetAction);
    m_structure->populateSheetMenu(sheetMenu);
}

QString CalcWindow::filterString() const
{
    // Mirrors SpreadsheetDocument::mimeTypes() and the .desktop MimeType
    // list: ods/xls open through the LibreOffice bridge.
    return tr("Spreadsheets (*.xlsx *.ods *.xls *.xlsm);;CSV (*.csv *.tsv);;"
              "All files (*)");
}

QString CalcWindow::saveFilterString() const
{
    // xlsx first: it is the native format, so Save As defaults to a
    // round-trip that keeps formulas, styles, names and charts.
    return tr("Excel Workbook (*.xlsx);;OpenDocument Spreadsheet (*.ods);;CSV (*.csv)");
}

} // namespace QindaOffice::Apps::QindaCalc
