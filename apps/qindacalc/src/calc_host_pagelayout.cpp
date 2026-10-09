// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's Page Layout tab (M78, D-087): Margins, Orientation, Size,
// Print Area, Breaks, Print Titles, Scale to Fit, Sheet Options, Header &
// Footer and the Page Setup dialog — one ActionEntry each for the ribbon,
// the Page Layout menu and the panel export, every change one undo step on
// the active sheet's Page Setup — and View ▸ Page Break Preview with the
// Freeze Panes variants. The dialog's state and answer are
// calc_host_pagesetup.cpp's.

#include "calc_host.h"

#include "spreadsheet_item.h"
#include "standard_actions.h"

#include "page_layout.h"
#include "sheet.h"

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

// Excel's margin presets, inches: left, right, top, bottom, header, footer.
struct MarginPreset {
    const char *id;
    double left, right, top, bottom, header, footer;
};
constexpr MarginPreset kMarginPresets[] = {
    {ActionId::kCalcMarginsNormal, 0.7, 0.7, 0.75, 0.75, 0.3, 0.3},
    {ActionId::kCalcMarginsWide, 1.0, 1.0, 1.0, 1.0, 0.5, 0.5},
    {ActionId::kCalcMarginsNarrow, 0.25, 0.25, 0.75, 0.75, 0.3, 0.3},
};

bool hasMargins(const Sheet::PageSetup &s, const MarginPreset &m)
{
    const auto same = [](double a, double b) { return qAbs(a - b) < 0.005; };
    return same(s.marginLeft, m.left) && same(s.marginRight, m.right) && same(s.marginTop, m.top)
        && same(s.marginBottom, m.bottom) && same(s.marginHeader, m.header) && same(s.marginFooter, m.footer);
}

QString paperId(int code)
{
    return QStringLiteral("calcPaperSize_") + QString::number(code);
}

// Print's scaling choices (Excel's backstage): fit to W × H pages, or none.
struct ScaleChoice {
    const char *id;
    bool fit;
    int width;
    int height;
};
constexpr ScaleChoice kScaleChoices[] = {
    {ActionId::kCalcScaleNone, false, 1, 1}, // Excel's defaults: 1 × 1 while Fit to is off
    {ActionId::kCalcScaleFitSheet, true, 1, 1},
    {ActionId::kCalcScaleFitColumns, true, 1, 0},
    {ActionId::kCalcScaleFitRows, true, 0, 1},
};

} // namespace

void CalcHost::changePageSetup(const std::function<void(Sheet::PageSetup &)> &change, const QString &undoText)
{
    const Sheet &current = sheet();
    Sheet::PageSetup setup = current.pageSetup();
    change(setup);
    m_document->setPrintSettings(m_document->activeSheet(), setup, current.printArea(), current.printTitleRows(),
                                 current.printTitleCols(), undoText);
    syncFromCursor();
    refreshPageBreakView();
    emit pageLayoutChanged();
}

void CalcHost::createPageLayoutActions()
{
    Quick::ActionModel *model = actions();
    const auto make = [this, model](const char *id, const QString &text, const char *icon, const std::function<void()> &run) {
        ActionEntry *entry = model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
        connect(entry, &ActionEntry::triggered, this, [this, run] {
            if (!m_syncing) {
                run();
            }
        });
        return entry;
    };
    const auto radio = [&make](const char *id, const QString &text, const char *icon, const char *group,
                               const std::function<void()> &run) {
        ActionEntry *entry = make(id, text, icon, run);
        entry->setCheckable(true);
        entry->setRadioGroup(QString::fromLatin1(group));
        return entry;
    };
    make(ActionId::kCalcPageSetup, tr("Page Set&up…"), "layout-template", [this] { emit pageSetupRequested(QStringLiteral("page")); });
    const QStringList marginNames{tr("&Normal"), tr("&Wide"), tr("N&arrow")};
    for (int i = 0; i < 3; ++i) {
        const MarginPreset preset = kMarginPresets[i];
        radio(preset.id, marginNames.at(i), "square-dashed", "calcMargins", [this, preset] {
            changePageSetup(
                [preset](Sheet::PageSetup &s) {
                    s.marginLeft = preset.left;
                    s.marginRight = preset.right;
                    s.marginTop = preset.top;
                    s.marginBottom = preset.bottom;
                    s.marginHeader = preset.header;
                    s.marginFooter = preset.footer;
                },
                tr("Margins"));
        });
    }
    make(ActionId::kCalcMarginsCustom, tr("Custom &Margins…"), "ruler", [this] { emit pageSetupRequested(QStringLiteral("margins")); });
    radio(ActionId::kCalcOrientationPortrait, tr("&Portrait"), "file", "calcOrientation",
          [this] { changePageSetup([](Sheet::PageSetup &s) { s.orientation = 0; }, tr("Orientation")); });
    radio(ActionId::kCalcOrientationLandscape, tr("&Landscape"), "file-text", "calcOrientation",
          [this] { changePageSetup([](Sheet::PageSetup &s) { s.orientation = 1; }, tr("Orientation")); });
    for (const PaperChoice &paper : commonPaperSizes()) {
        const int code = paper.code;
        ActionEntry *entry = model->add(paperId(code), QString::fromUtf8(paper.name), {}, QStringLiteral("file"));
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("calcPaperSize"));
        connect(entry, &ActionEntry::triggered, this, [this, code] {
            if (!m_syncing) {
                changePageSetup([code](Sheet::PageSetup &s) { s.paperSize = code; }, tr("Paper Size"));
            }
        });
    }
    make(ActionId::kCalcPaperSizeMore, tr("More Paper Si&zes…"), "ruler", [this] { emit pageSetupRequested(QStringLiteral("page")); });
    make(ActionId::kCalcSetPrintArea, tr("&Set Print Area"), "frame", [this] {
        const CellRange area = selection();
        m_document->setPrintSettings(m_document->activeSheet(), sheet().pageSetup(), area, sheet().printTitleRows(),
                                     sheet().printTitleCols(), tr("Set Print Area"));
        syncFromCursor();
        refreshPageBreakView();
    });
    make(ActionId::kCalcClearPrintArea, tr("&Clear Print Area"), "eraser", [this] {
        m_document->setPrintSettings(m_document->activeSheet(), sheet().pageSetup(), std::nullopt, sheet().printTitleRows(),
                                     sheet().printTitleCols(), tr("Clear Print Area"));
        syncFromCursor();
        refreshPageBreakView();
    });
    make(ActionId::kCalcInsertPageBreak, tr("&Insert Page Break"), "separator-horizontal", [this] {
        const CellAddr at = cursor();
        changePageSetup([at](Sheet::PageSetup &s) { static_cast<void>(insertPageBreak(s, at)); }, tr("Insert Page Break"));
    });
    make(ActionId::kCalcRemovePageBreak, tr("&Remove Page Break"), "separator-vertical", [this] {
        const CellAddr at = cursor();
        changePageSetup([at](Sheet::PageSetup &s) { static_cast<void>(removePageBreak(s, at)); }, tr("Remove Page Break"));
    });
    make(ActionId::kCalcResetPageBreaks, tr("Reset &All Page Breaks"), "list-restart",
         [this] { changePageSetup([](Sheet::PageSetup &s) { resetPageBreaks(s); }, tr("Reset All Page Breaks")); });
    make(ActionId::kCalcPrintTitles, tr("Print &Titles…"), "rows-2", [this] { emit pageSetupRequested(QStringLiteral("sheet")); });
    make(ActionId::kCalcHeaderFooter, tr("&Header && Footer…"), "panel-top", [this] { emit pageSetupRequested(QStringLiteral("header")); });
    const QStringList scaleNames{tr("&No Scaling"), tr("&Fit Sheet on One Page"), tr("Fit All &Columns on One Page"),
                                 tr("Fit All &Rows on One Page")};
    for (int i = 0; i < 4; ++i) {
        const ScaleChoice choice = kScaleChoices[i];
        radio(choice.id, scaleNames.at(i), "scaling", "calcScale", [this, choice] {
            changePageSetup(
                [choice](Sheet::PageSetup &s) {
                    s.fitToPage = choice.fit;
                    s.fitToWidth = choice.width;
                    s.fitToHeight = choice.height;
                    if (!choice.fit) {
                        s.scale = 100;
                    }
                },
                tr("Scale to Fit"));
        });
    }
    const auto option = [this, model](const char *id, const QString &text, const char *icon, bool Sheet::PageSetup::*flag) {
        ActionEntry *entry = model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
        entry->setCheckable(true);
        connect(entry, &ActionEntry::toggled, this, [this, flag](bool on) {
            if (!m_syncing) {
                changePageSetup([flag, on](Sheet::PageSetup &s) { s.*flag = on; }, tr("Sheet Options"));
            }
        });
    };
    option(ActionId::kCalcPrintGridlines, tr("Print &Gridlines"), "grid-3x3", &Sheet::PageSetup::gridLines);
    option(ActionId::kCalcPrintHeadings, tr("Print &Headings"), "heading", &Sheet::PageSetup::headings);
    radio(ActionId::kCalcViewNormal, tr("&Normal"), "sheet", "calcView", [this] {
        if (m_grid != nullptr) {
            m_grid->setPageBreakView(std::nullopt);
        }
        syncFromCursor();
    });
    radio(ActionId::kCalcViewPageBreakPreview, tr("Page &Break Preview"), "layout-dashboard", "calcView", [this] {
        if (m_grid != nullptr) {
            m_grid->setPageBreakView(SpreadsheetItem::PageBreakView{});
        }
        refreshPageBreakView();
        syncFromCursor();
    });
    make(ActionId::kCalcFreezeTopRow, tr("Freeze &Top Row"), "panel-top", [this] {
        m_document->setFreeze(1, 0);
        refreshGrid();
        syncFromCursor();
    });
    make(ActionId::kCalcFreezeFirstColumn, tr("Freeze First &Column"), "panel-left", [this] {
        m_document->setFreeze(0, 1);
        refreshGrid();
        syncFromCursor();
    });
    // The view follows edits that move pages, and the sheet shown.
    connect(m_document, &SpreadsheetDocument::contentChanged, this, [this] { refreshPageBreakView(); });
    connect(this, &CalcHost::sheetsChanged, this, [this] { refreshPageBreakView(); });
}

void CalcHost::insertPageLayoutMenus()
{
    const auto a = [](const char *id) { return MenuNode::action(QString::fromLatin1(id)); };
    QList<MenuNode> papers;
    for (const PaperChoice &paper : commonPaperSizes()) {
        papers.append(MenuNode::action(paperId(paper.code)));
    }
    papers.append(MenuNode::separator());
    papers.append(a(ActionId::kCalcPaperSizeMore));
    Quick::MenuTree *tree = menuTree();
    tree->insertMenuBefore(
        QStringLiteral("calcDataMenu"),
        MenuNode::submenu(
            QStringLiteral("calcPageLayoutMenu"), tr("Page &Layout"),
            {a(ActionId::kCalcPageSetup), MenuNode::separator(),
             MenuNode::submenu(QStringLiteral("calcMarginsMenu"), tr("&Margins"),
                               {a(ActionId::kCalcMarginsNormal), a(ActionId::kCalcMarginsWide), a(ActionId::kCalcMarginsNarrow),
                                MenuNode::separator(), a(ActionId::kCalcMarginsCustom)}),
             MenuNode::submenu(QStringLiteral("calcOrientationMenu"), tr("&Orientation"),
                               {a(ActionId::kCalcOrientationPortrait), a(ActionId::kCalcOrientationLandscape)}),
             MenuNode::submenu(QStringLiteral("calcPaperSizeMenu"), tr("Si&ze"), papers),
             MenuNode::submenu(QStringLiteral("calcPrintAreaMenu"), tr("Print &Area"),
                               {a(ActionId::kCalcSetPrintArea), a(ActionId::kCalcClearPrintArea)}),
             MenuNode::submenu(QStringLiteral("calcBreaksMenu"), tr("&Breaks"),
                               {a(ActionId::kCalcInsertPageBreak), a(ActionId::kCalcRemovePageBreak),
                                a(ActionId::kCalcResetPageBreaks)}),
             a(ActionId::kCalcPrintTitles), MenuNode::separator(),
             MenuNode::submenu(QStringLiteral("calcScaleMenu"), tr("&Scale to Fit"),
                               {a(ActionId::kCalcScaleNone), a(ActionId::kCalcScaleFitSheet),
                                a(ActionId::kCalcScaleFitColumns), a(ActionId::kCalcScaleFitRows)}),
             MenuNode::separator(), a(ActionId::kCalcPrintGridlines), a(ActionId::kCalcPrintHeadings), MenuNode::separator(),
             a(ActionId::kCalcHeaderFooter)}));
    // AGENT-GUARD: an id appears once in the menu tree (the panel export
    // refuses a duplicate): Header & Footer lives in Page Layout; the
    // Insert ribbon tab shows it too.
    tree->insertInto(QStringLiteral("viewMenu"), QString::fromLatin1(ActionId::kViewZoomIn), a(ActionId::kCalcViewNormal));
    tree->insertInto(QStringLiteral("viewMenu"), QString::fromLatin1(ActionId::kViewZoomIn), a(ActionId::kCalcViewPageBreakPreview));
    tree->insertInto(QStringLiteral("viewMenu"), QString::fromLatin1(ActionId::kViewZoomIn), MenuNode::separator());
    tree->insertInto(QStringLiteral("calcDataMenu"), QString::fromLatin1(ActionId::kCalcAutoFilter), a(ActionId::kCalcFreezeTopRow));
    tree->insertInto(QStringLiteral("calcDataMenu"), QString::fromLatin1(ActionId::kCalcAutoFilter), a(ActionId::kCalcFreezeFirstColumn));
    tree->insertInto(QStringLiteral("calcDataMenu"), QString::fromLatin1(ActionId::kCalcAutoFilter), MenuNode::separator());
}

void CalcHost::syncPageLayoutFromCursor()
{
    m_syncing = true;
    Quick::ActionModel *model = actions();
    const Sheet &current = sheet();
    const Sheet::PageSetup &setup = current.pageSetup();
    const bool cells = !current.isChartSheet();
    QStringList ids;
    for (const MarginPreset &preset : kMarginPresets) {
        ids.append(QString::fromLatin1(preset.id));
        model->setChecked(QString::fromLatin1(preset.id), hasMargins(setup, preset));
    }
    model->setChecked(QString::fromLatin1(setup.orientation == 1 ? ActionId::kCalcOrientationLandscape
                                                                  : ActionId::kCalcOrientationPortrait),
                      true);
    for (const PaperChoice &paper : commonPaperSizes()) {
        model->setChecked(paperId(paper.code), paper.code == (setup.paperSize == 0 ? 1 : setup.paperSize));
    }
    bool scaled = false;
    for (const ScaleChoice &choice : kScaleChoices) {
        const bool on = choice.fit ? setup.fitToPage && setup.fitToWidth == choice.width && setup.fitToHeight == choice.height
                                   : !setup.fitToPage && setup.scale == 100;
        scaled = scaled || on;
        model->setChecked(QString::fromLatin1(choice.id), on);
    }
    if (!scaled) { // a custom scale: none of the four is the sheet's
        for (const ScaleChoice &choice : kScaleChoices) {
            model->setChecked(QString::fromLatin1(choice.id), false);
        }
    }
    model->setChecked(QString::fromLatin1(ActionId::kCalcPrintGridlines), setup.gridLines);
    model->setChecked(QString::fromLatin1(ActionId::kCalcPrintHeadings), setup.headings);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcClearPrintArea), cells && current.printArea().has_value());
    model->setEnabled(QString::fromLatin1(ActionId::kCalcSetPrintArea), cells);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcInsertPageBreak), cells && cursor() != CellAddr{0, 0});
    model->setEnabled(QString::fromLatin1(ActionId::kCalcRemovePageBreak), cells && hasPageBreakAt(setup, cursor()));
    model->setEnabled(QString::fromLatin1(ActionId::kCalcResetPageBreaks),
                      cells && (!setup.rowBreaks.isEmpty() || !setup.colBreaks.isEmpty()));
    const bool preview = m_grid != nullptr && m_grid->pageBreakPreview();
    model->setChecked(QString::fromLatin1(preview ? ActionId::kCalcViewPageBreakPreview : ActionId::kCalcViewNormal), true);
    m_syncing = false;
    emit pageLayoutChanged(); // the Scale to Fit slots follow the sheet
}

QStringList CalcHost::paperSizeIds() const
{
    QStringList ids;
    for (const PaperChoice &paper : commonPaperSizes()) {
        ids.append(paperId(paper.code));
    }
    return ids;
}

QVariantMap CalcHost::scaling() const
{
    const Sheet::PageSetup &setup = sheet().pageSetup();
    return {{QStringLiteral("fitToPage"), setup.fitToPage},
            {QStringLiteral("fitToWidth"), setup.fitToPage ? setup.fitToWidth : 0},
            {QStringLiteral("fitToHeight"), setup.fitToPage ? setup.fitToHeight : 0},
            {QStringLiteral("scale"), setup.scale}};
}

void CalcHost::refreshPageBreakView()
{
    if (m_grid == nullptr || !m_grid->pageBreakPreview()) {
        return;
    }
    const PrintLayout layout = layoutPrintPages(*m_document->workbook(), m_document->activeSheet());
    SpreadsheetItem::PageBreakView view;
    view.area = layout.area;
    view.rowStarts = layout.rowStarts;
    view.colStarts = layout.colStarts;
    view.rowManual = layout.rowStartManual;
    view.colManual = layout.colStartManual;
    for (const PrintPage &page : layout.pages) {
        view.pages.append({page.body, page.number});
    }
    m_grid->setPageBreakView(view);
}

} // namespace QindaOffice::Apps::QindaCalc
