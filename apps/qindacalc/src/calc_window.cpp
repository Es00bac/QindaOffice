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
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabBar>
#include <QVBoxLayout>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QIcon>
#include <QToolBar>
#include <QVBoxLayout>

#include "calc_clipboard.h"
#include "calc_find_bar.h"
#include "calc_format_controller.h"
#include "standard_actions.h"
#include <QVBoxLayout>
#include <QWidget>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

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
        if (m_syncingBar) {
            return;
        }
        // Name box navigation: type a reference to jump.
        ParseResult parsed = parseFormula(m_nameBox->text());
        if (parsed.root && parsed.root->kind == Node::Kind::Ref && parsed.root->ref.isSingleCell()) {
            m_view->setCursorCell(parsed.root->ref.range.topLeft);
        }
    });
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0) {
            m_document->setActiveSheet(index);
            m_view->refreshAll();
        }
    });
    rebuildSheetTabs();

    createCalcActions();
    QToolBar *mainToolBar = addMainToolBar();
    if (m_insertChartAction != nullptr) {
        mainToolBar->addSeparator();
        mainToolBar->addAction(m_insertChartAction);
    }
    mainToolBar->addAction(m_insertSheetAction);
    mainToolBar->addAction(m_thousandsAction);

    m_format = new CalcFormatController(m_document, m_view, this);
    addToolBarBreak(Qt::TopToolBarArea);
    addToolBar(Qt::TopToolBarArea, m_format->toolBar());

    wireEditingActions();
    connect(m_findBar, &CalcFindBar::message, this,
            [this](const QString &text) { statusBar()->showMessage(text, 5000); });
    // Undo of a sheet insert/delete (or a load) changes the sheet list.
    connect(m_document, &SpreadsheetDocument::structureChanged, this, [this] {
        rebuildSheetTabs();
        m_view->refreshAll();
        rebuildChartWindows();
    });
    // CC: data edits changed what charts visualize — the floating windows
    // previously only refreshed on structural changes and went stale.
    connect(m_document, &SpreadsheetDocument::contentChanged, this, [this] {
        if (m_chartWindows.isEmpty()) {
            return;
        }
        rebuildChartWindows();
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

    m_actions.editFind->setEnabled(true);
    m_actions.editFindNext->setEnabled(true);
    m_actions.editFindPrevious->setEnabled(true);
    connect(m_actions.editFind, &QAction::triggered, m_findBar, &CalcFindBar::openFind);
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
    wb->charts.append(spec);
    m_document->setDirty(true);
    rebuildChartWindows();
#endif
}

void CalcWindow::rebuildChartWindows()
{
#ifdef QQO_WITH_CHARTS
    qDeleteAll(m_chartWindows);
    m_chartWindows.clear();
    const Workbook *wb = m_document->workbook();
    for (const CalcEng::ChartSpec &spec : wb->charts) {
        auto *floating = new QWidget(this, Qt::Tool | Qt::WindowStaysOnTopHint);
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

void CalcWindow::insertFileMenuActions(QMenu *fileMenu)
{
    const QList<Shell::TemplateEntry> templates = Shell::templatesFor(QStringLiteral("QindaCalc"));
    if (templates.isEmpty()) {
        return;
    }
    QMenu *submenu = fileMenu->addMenu(tr("New from &Template"));
    for (const Shell::TemplateEntry &entry : templates) {
        submenu->addAction(entry.name, this, [this, path = entry.path] {
            QString error;
            if (m_document->load(path, &error)) {
                m_document->adoptPath(QString());
                m_document->setDirty(true);
                rebuildSheetTabs();
                m_view->refreshAll();
                rebuildChartWindows();
            }
        });
    }
}

void CalcWindow::insertApplicationMenus(QMenu *before)
{
#ifdef QQO_WITH_CHARTS
    auto *insertMenu = new QMenu(tr("&Insert"), this);
    menuBar()->insertMenu(before->menuAction(), insertMenu);
    insertMenu->addAction(m_insertChartAction);
#endif
    auto *sheetMenu = new QMenu(tr("&Sheet"), this);
    menuBar()->insertMenu(before->menuAction(), sheetMenu);
    sheetMenu->addAction(m_insertSheetAction);
    auto *formatMenu = new QMenu(tr("F&ormat"), this);
    menuBar()->insertMenu(before->menuAction(), formatMenu);
    m_format->populateFormatMenu(formatMenu);
    formatMenu->addSeparator();
    formatMenu->addAction(m_thousandsAction);
}

QString CalcWindow::filterString() const
{
    return tr("Spreadsheets (*.xlsx);;CSV (*.csv);;All files (*)");
}

} // namespace QindaOffice::Apps::QindaCalc
