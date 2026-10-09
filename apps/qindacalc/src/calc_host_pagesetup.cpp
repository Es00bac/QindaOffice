// SPDX-License-Identifier: GPL-3.0-or-later
// The Page Setup dialog's state and answer (M78, D-087): Excel's four tabs
// — Page (orientation, scaling, paper, first page number), Margins (and
// centring), Header/Footer (odd, even and first, in the dialog's &[Page]
// spelling) and Sheet (print area, rows and columns to repeat, gridlines,
// headings, black and white, draft, comments, errors, page order) — as one
// QVariantMap, and the ribbon's Scale to Fit slots. One undo step per
// answer. Also the cell-reference parsing the what-if dialogs share.

#include "calc_host.h"

#include "spreadsheet_item.h"

#include "header_footer.h"
#include "page_layout.h"
#include "parser.h"
#include "sheet.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

QVariantMap partsMap(const QString &code)
{
    const HeaderFooterParts parts = headerFooterParts(code);
    return {{QStringLiteral("left"), parts.left}, {QStringLiteral("center"), parts.center}, {QStringLiteral("right"), parts.right}};
}

QString codeFrom(const QVariant &value)
{
    const QVariantMap map = value.toMap();
    return headerFooterCode({map.value(QStringLiteral("left")).toString(), map.value(QStringLiteral("center")).toString(),
                             map.value(QStringLiteral("right")).toString()});
}

// A reference typed in a dialog: the parsed RangeRef, or nullopt.
std::optional<RangeRef> refFromText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }
    const ParseResult parsed = parseFormula(trimmed.startsWith(QLatin1Char('=')) ? trimmed.mid(1) : trimmed);
    if (!parsed.error.isEmpty() || !parsed.root || parsed.root->kind != Node::Kind::Ref) {
        return std::nullopt;
    }
    return parsed.root->ref;
}

} // namespace

CellAddr CalcHost::cellFromText(const QString &text) const
{
    const std::optional<RangeRef> ref = refFromText(text);
    if (!ref || !ref->isSingleCell()) {
        return {};
    }
    if (!ref->sheet.isEmpty() && m_document->workbook()->sheetIndex(unquotedSheetName(ref->sheet)) != m_document->activeSheet()) {
        return {}; // what-if cells live on the active sheet, as in Excel
    }
    return ref->range.topLeft;
}

QList<CellAddr> CalcHost::cellsFromText(const QString &text) const
{
    QList<CellAddr> cells;
    for (const QString &part : text.split(QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts)) {
        const std::optional<RangeRef> ref = refFromText(part);
        if (!ref || ref->wholeRows || ref->wholeCols || ref->range.cellCount() > kMaxScenarioCells
            || (!ref->sheet.isEmpty()
                && m_document->workbook()->sheetIndex(unquotedSheetName(ref->sheet)) != m_document->activeSheet())) {
            return {};
        }
        for (qint32 r = ref->range.topLeft.row; r <= ref->range.bottomRight.row; ++r) {
            for (qint32 c = ref->range.topLeft.col; c <= ref->range.bottomRight.col; ++c) {
                if (!cells.contains(CellAddr{r, c})) {
                    cells.append({r, c});
                }
            }
        }
    }
    return cells;
}

QVariantMap CalcHost::pageSetupState() const
{
    const Sheet &current = sheet();
    const Sheet::PageSetup &s = current.pageSetup();
    QVariantList papers;
    for (const PaperChoice &paper : commonPaperSizes()) {
        papers.append(QVariantMap{{QStringLiteral("code"), paper.code}, {QStringLiteral("name"), QString::fromUtf8(paper.name)}});
    }
    HeaderFooterContext fields;
    fields.date = QDate::currentDate();
    fields.time = QTime::currentTime();
    fields.sheetName = current.name();
    fields.fileName = m_document->canonicalPath().isEmpty() ? tr("Book1") : QFileInfo(m_document->canonicalPath()).fileName();
    QVariantList presets;
    for (const HeaderFooterPreset &preset : headerFooterPresets(fields)) {
        QVariantMap entry = partsMap(preset.code);
        entry.insert(QStringLiteral("code"), preset.code);
        entry.insert(QStringLiteral("label"), preset.label);
        presets.append(entry);
    }
    const auto areaText = [](const std::optional<CellRange> &range) {
        return range ? addrText(range->topLeft, true, true) + QLatin1Char(':') + addrText(range->bottomRight, true, true) : QString();
    };
    return {
        {QStringLiteral("sheet"), current.name()},
        {QStringLiteral("orientation"), s.orientation},
        {QStringLiteral("paperSize"), s.paperSize == 0 ? 1 : s.paperSize},
        {QStringLiteral("papers"), papers},
        {QStringLiteral("fitToPage"), s.fitToPage},
        {QStringLiteral("scale"), s.scale},
        {QStringLiteral("fitToWidth"), s.fitToWidth},
        {QStringLiteral("fitToHeight"), s.fitToHeight},
        {QStringLiteral("firstPageNumber"), s.useFirstPageNumber ? QString::number(s.firstPageNumber) : QString()},
        {QStringLiteral("margins"), QVariantMap{{QStringLiteral("left"), s.marginLeft}, {QStringLiteral("right"), s.marginRight},
                                                {QStringLiteral("top"), s.marginTop}, {QStringLiteral("bottom"), s.marginBottom},
                                                {QStringLiteral("header"), s.marginHeader}, {QStringLiteral("footer"), s.marginFooter}}},
        {QStringLiteral("centerHorizontally"), s.centerHorizontally},
        {QStringLiteral("centerVertically"), s.centerVertically},
        {QStringLiteral("oddHeader"), partsMap(s.oddHeader)},
        {QStringLiteral("oddFooter"), partsMap(s.oddFooter)},
        {QStringLiteral("evenHeader"), partsMap(s.evenHeader)},
        {QStringLiteral("evenFooter"), partsMap(s.evenFooter)},
        {QStringLiteral("firstHeader"), partsMap(s.firstHeader)},
        {QStringLiteral("firstFooter"), partsMap(s.firstFooter)},
        {QStringLiteral("differentOddEven"), s.differentOddEven},
        {QStringLiteral("differentFirst"), s.differentFirst},
        {QStringLiteral("scaleWithDoc"), s.scaleWithDoc},
        {QStringLiteral("alignWithMargins"), s.alignWithMargins},
        {QStringLiteral("presets"), presets},
        {QStringLiteral("printArea"), [&]() {
             // Every area of a multi-area print area, as Excel lists them.
             QStringList areas{areaText(current.printArea())};
             for (const CellRange &more : s.morePrintAreas) {
                 areas.append(areaText(more));
             }
             return areas.join(QLatin1Char(',')).remove(QRegularExpression(QStringLiteral("^,+")));
         }()},
        {QStringLiteral("titleRows"), current.printTitleRows()
             ? QStringLiteral("$%1:$%2").arg(current.printTitleRows()->topLeft.row + 1).arg(current.printTitleRows()->bottomRight.row + 1)
             : QString()},
        {QStringLiteral("titleCols"), current.printTitleCols()
             ? QStringLiteral("$%1:$%2").arg(colName(current.printTitleCols()->topLeft.col), colName(current.printTitleCols()->bottomRight.col))
             : QString()},
        {QStringLiteral("gridLines"), s.gridLines},
        {QStringLiteral("headings"), s.headings},
        {QStringLiteral("blackAndWhite"), s.blackAndWhite},
        {QStringLiteral("draft"), s.draft},
        {QStringLiteral("overThenDown"), s.overThenDown},
        {QStringLiteral("cellComments"), s.cellComments},
        {QStringLiteral("errors"), s.errors},
    };
}

QString CalcHost::applyPageSetup(const QVariantMap &state)
{
    const Sheet &current = sheet();
    Sheet::PageSetup s = current.pageSetup();
    const auto value = [&state](const char *key) { return state.value(QString::fromLatin1(key)); };
    s.orientation = value("orientation").toInt() == 1 ? 1 : 0;
    s.paperSize = value("paperSize").toInt();
    s.fitToPage = value("fitToPage").toBool();
    s.scale = qBound(10, value("scale").toInt(), 400);
    s.fitToWidth = qMax(0, value("fitToWidth").toInt());
    s.fitToHeight = qMax(0, value("fitToHeight").toInt());
    const QString first = value("firstPageNumber").toString().trimmed();
    bool numbered = false;
    const int firstNumber = first.toInt(&numbered);
    if (!first.isEmpty() && !numbered) {
        return tr("The first page number must be a number or Auto.");
    }
    s.useFirstPageNumber = numbered;
    s.firstPageNumber = numbered ? firstNumber : 1;
    const QVariantMap margins = value("margins").toMap();
    s.marginLeft = qMax(0.0, margins.value(QStringLiteral("left")).toDouble());
    s.marginRight = qMax(0.0, margins.value(QStringLiteral("right")).toDouble());
    s.marginTop = qMax(0.0, margins.value(QStringLiteral("top")).toDouble());
    s.marginBottom = qMax(0.0, margins.value(QStringLiteral("bottom")).toDouble());
    s.marginHeader = qMax(0.0, margins.value(QStringLiteral("header")).toDouble());
    s.marginFooter = qMax(0.0, margins.value(QStringLiteral("footer")).toDouble());
    const QSizeF paper = paperSizeMm(s.paperSize);
    const double widthIn = (s.orientation == 1 ? paper.height() : paper.width()) / 25.4;
    const double heightIn = (s.orientation == 1 ? paper.width() : paper.height()) / 25.4;
    if (s.marginLeft + s.marginRight >= widthIn || s.marginTop + s.marginBottom >= heightIn) {
        return tr("Margins do not fit page size.");
    }
    s.centerHorizontally = value("centerHorizontally").toBool();
    s.centerVertically = value("centerVertically").toBool();
    s.oddHeader = codeFrom(value("oddHeader"));
    s.oddFooter = codeFrom(value("oddFooter"));
    s.evenHeader = codeFrom(value("evenHeader"));
    s.evenFooter = codeFrom(value("evenFooter"));
    s.firstHeader = codeFrom(value("firstHeader"));
    s.firstFooter = codeFrom(value("firstFooter"));
    s.differentOddEven = value("differentOddEven").toBool();
    s.differentFirst = value("differentFirst").toBool();
    s.scaleWithDoc = value("scaleWithDoc").toBool();
    s.alignWithMargins = value("alignWithMargins").toBool();
    s.gridLines = value("gridLines").toBool();
    s.headings = value("headings").toBool();
    s.blackAndWhite = value("blackAndWhite").toBool();
    s.draft = value("draft").toBool();
    s.overThenDown = value("overThenDown").toBool();
    s.cellComments = qBound(0, value("cellComments").toInt(), 2);
    s.errors = qBound(0, value("errors").toInt(), 3);
    // Sheet tab: the print area (one, or several separated by commas) and
    // the rows / columns to repeat.
    std::optional<CellRange> area;
    s.morePrintAreas.clear();
    for (const QString &part : value("printArea").toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const std::optional<RangeRef> ref = refFromText(part);
        if (!ref || ref->wholeRows || ref->wholeCols) {
            return tr("Reference isn't valid: %1").arg(part.trimmed());
        }
        if (!area) {
            area = ref->range;
        } else {
            s.morePrintAreas.append(ref->range);
        }
    }
    std::optional<CellRange> rows;
    if (const QString text = value("titleRows").toString().trimmed(); !text.isEmpty()) {
        const std::optional<RangeRef> ref = refFromText(text.contains(QLatin1Char(':')) ? text : text + QLatin1Char(':') + text);
        if (!ref || ref->wholeCols) {
            return tr("Rows to repeat at top: the reference isn't valid (%1).").arg(text);
        }
        rows = CellRange{{ref->range.topLeft.row, 0}, {ref->range.bottomRight.row, Sheet::kMaxCols - 1}};
    }
    std::optional<CellRange> cols;
    if (const QString text = value("titleCols").toString().trimmed(); !text.isEmpty()) {
        const std::optional<RangeRef> ref = refFromText(text.contains(QLatin1Char(':')) ? text : text + QLatin1Char(':') + text);
        if (!ref || ref->wholeRows) {
            return tr("Columns to repeat at left: the reference isn't valid (%1).").arg(text);
        }
        cols = CellRange{{0, ref->range.topLeft.col}, {Sheet::kMaxRows - 1, ref->range.bottomRight.col}};
    }
    m_document->setPrintSettings(m_document->activeSheet(), s, area, rows, cols, tr("Page Setup"));
    syncFromCursor();
    refreshPageBreakView();
    emit pageLayoutChanged();
    return {};
}

void CalcHost::setFitTo(int width, int height)
{
    changePageSetup(
        [width, height](Sheet::PageSetup &s) {
            s.fitToWidth = qMax(0, width);
            s.fitToHeight = qMax(0, height);
            s.fitToPage = s.fitToWidth > 0 || s.fitToHeight > 0;
            if (!s.fitToPage) {
                // Automatic × Automatic is no Fit to: Excel's 100 % and 1 × 1.
                s.scale = 100;
                s.fitToWidth = 1;
                s.fitToHeight = 1;
            }
        },
        tr("Scale to Fit"));
}

void CalcHost::setScalePercent(int percent)
{
    changePageSetup(
        [percent](Sheet::PageSetup &s) {
            s.fitToPage = false;
            s.scale = qBound(10, percent, 400);
        },
        tr("Scale to Fit"));
}

QString CalcHost::headerFooterPreview(const QString &left, const QString &center, const QString &right) const
{
    const HeaderFooterText text = parseHeaderFooter(headerFooterCode({left, center, right}));
    HeaderFooterContext fields;
    fields.page = 1;
    fields.pages = qMax(1, int(layoutPrintPages(*m_document->workbook(), m_document->activeSheet()).pages.size()));
    fields.date = QDate::currentDate();
    fields.time = QTime::currentTime();
    fields.sheetName = sheet().name();
    fields.fileName = m_document->canonicalPath().isEmpty() ? tr("Book1") : QFileInfo(m_document->canonicalPath()).fileName();
    QStringList shown;
    for (const HeaderFooterSection *section : {&text.left, &text.center, &text.right}) {
        shown.append(headerFooterSectionText(*section, fields));
    }
    return shown.join(QStringLiteral("    |    "));
}

} // namespace QindaOffice::Apps::QindaCalc
