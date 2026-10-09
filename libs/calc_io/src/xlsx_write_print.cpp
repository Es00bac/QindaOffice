// SPDX-License-Identifier: GPL-3.0-or-later
// Writing a sheet's print settings (xlsx_print.h, M78, D-087): the
// elements in the worksheet schema's order, each attribute only where it is
// not the schema's default, and nothing at all for a sheet whose settings
// are all Excel's defaults. The carry stage adds the source's printer
// settings link (r:id) and the printer attributes the model does not hold
// to the <pageSetup> written here (xlsx_carry_sheet.cpp).

#include "xlsx_print.h"

#include "xlsx_xml.h"

#include "sheet.h"

#include <QLocale>

namespace QindaOffice::Calc::IO {

namespace {

// The shortest text that reads back as the same number: Excel's own
// 0.70866141732283472 (18 mm) must not come back a hair different.
QByteArray measureText(double inches)
{
    return QString::number(inches, 'g', QLocale::FloatingPointShortest).toLatin1();
}

QByteArray attribute(const char *name, const QByteArray &value)
{
    return QByteArrayLiteral(" ") + name + QByteArrayLiteral("=\"") + value + '"';
}

QByteArray flagAttribute(const char *name, bool on)
{
    return on ? attribute(name, QByteArrayLiteral("1")) : QByteArray();
}

bool marginsAreDefault(const Sheet::PageSetup &s)
{
    const Sheet::PageSetup d;
    return s.marginLeft == d.marginLeft && s.marginRight == d.marginRight && s.marginTop == d.marginTop
        && s.marginBottom == d.marginBottom && s.marginHeader == d.marginHeader && s.marginFooter == d.marginFooter;
}

bool printOptionsAreDefault(const Sheet::PageSetup &s)
{
    return !s.centerHorizontally && !s.centerVertically && !s.headings && !s.gridLines;
}

// A fit-to count other than the schema's 1 is written, under Fit to or not
// (0 is "automatic"; Excel remembers the counts for the next switch).
bool writesFitCount(int count)
{
    return count != 1;
}

bool pageSetupIsDefault(const Sheet::PageSetup &s)
{
    return s.orientation == 0 && s.paperSize == 0 && s.scale == 100 && s.firstPageNumber == 1
        && !s.useFirstPageNumber && !writesFitCount(s.fitToWidth) && !writesFitCount(s.fitToHeight)
        && !s.overThenDown && !s.blackAndWhite && !s.draft && s.cellComments == 0 && s.errors == 0;
}

bool headerFooterIsDefault(const Sheet::PageSetup &s)
{
    return s.oddHeader.isEmpty() && s.oddFooter.isEmpty() && s.evenHeader.isEmpty() && s.evenFooter.isEmpty()
        && s.firstHeader.isEmpty() && s.firstFooter.isEmpty() && !s.differentOddEven && !s.differentFirst
        && s.scaleWithDoc && s.alignWithMargins;
}

QByteArray pageSetupXml(const Sheet::PageSetup &s)
{
    static const char *const kComments[] = {"none", "atEnd", "asDisplayed"};
    static const char *const kErrors[] = {"displayed", "blank", "dash", "NA"};
    // Excel's attribute order.
    QByteArray xml = QByteArrayLiteral("<pageSetup");
    if (s.paperSize != 0) {
        xml += attribute("paperSize", QByteArray::number(s.paperSize));
    }
    if (s.scale != 100) {
        xml += attribute("scale", QByteArray::number(qBound(10, s.scale, 400)));
    }
    if (s.firstPageNumber != 1) {
        xml += attribute("firstPageNumber", QByteArray::number(s.firstPageNumber));
    }
    if (writesFitCount(s.fitToWidth)) {
        xml += attribute("fitToWidth", QByteArray::number(qMax(0, s.fitToWidth)));
    }
    if (writesFitCount(s.fitToHeight)) {
        xml += attribute("fitToHeight", QByteArray::number(qMax(0, s.fitToHeight)));
    }
    if (s.overThenDown) {
        xml += attribute("pageOrder", QByteArrayLiteral("overThenDown"));
    }
    // Excel always says which way the paper turns.
    xml += attribute("orientation", s.orientation == 1 ? QByteArrayLiteral("landscape") : QByteArrayLiteral("portrait"));
    xml += flagAttribute("blackAndWhite", s.blackAndWhite);
    xml += flagAttribute("draft", s.draft);
    if (s.cellComments > 0 && s.cellComments < 3) {
        xml += attribute("cellComments", kComments[s.cellComments]);
    }
    xml += flagAttribute("useFirstPageNumber", s.useFirstPageNumber);
    if (s.errors > 0 && s.errors < 4) {
        xml += attribute("errors", kErrors[s.errors]);
    }
    return xml + QByteArrayLiteral("/>");
}

QByteArray headerFooterXml(const Sheet::PageSetup &s)
{
    QByteArray xml = QByteArrayLiteral("<headerFooter") + flagAttribute("differentOddEven", s.differentOddEven)
        + flagAttribute("differentFirst", s.differentFirst)
        + (s.scaleWithDoc ? QByteArray() : attribute("scaleWithDoc", QByteArrayLiteral("0")))
        + (s.alignWithMargins ? QByteArray() : attribute("alignWithMargins", QByteArrayLiteral("0")));
    QByteArray children;
    for (const auto &[name, text] : {std::pair{"oddHeader", &s.oddHeader}, std::pair{"oddFooter", &s.oddFooter},
                                     std::pair{"evenHeader", &s.evenHeader}, std::pair{"evenFooter", &s.evenFooter},
                                     std::pair{"firstHeader", &s.firstHeader},
                                     std::pair{"firstFooter", &s.firstFooter}}) {
        if (!text->isEmpty()) {
            children += QByteArrayLiteral("<") + name + '>' + xmlAttribute(*text) + QByteArrayLiteral("</") + name + '>';
        }
    }
    return children.isEmpty() ? xml + QByteArrayLiteral("/>") : xml + '>' + children + QByteArrayLiteral("</headerFooter>");
}

// <rowBreaks>/<colBreaks> as Excel writes them: every break manual, each
// spanning the whole other axis (max = its last index).
QByteArray breaksXml(const char *name, const QList<qint32> &breaks, qint32 lastIndex)
{
    if (breaks.isEmpty()) {
        return {};
    }
    QByteArray xml = QByteArrayLiteral("<") + name + attribute("count", QByteArray::number(breaks.size()))
        + attribute("manualBreakCount", QByteArray::number(breaks.size())) + '>';
    for (const qint32 line : breaks) {
        xml += QByteArrayLiteral("<brk") + attribute("id", QByteArray::number(line))
            + attribute("max", QByteArray::number(lastIndex)) + attribute("man", QByteArrayLiteral("1"))
            + QByteArrayLiteral("/>");
    }
    return xml + QByteArrayLiteral("</") + name + '>';
}

} // namespace

void fillPrintPatch(const Sheet &sheet, PatchManifest::SheetPatch *patch)
{
    const Sheet::PageSetup &s = sheet.pageSetup();
    patch->fitToPage = s.fitToPage;
    const bool any = !marginsAreDefault(s) || !printOptionsAreDefault(s) || !pageSetupIsDefault(s)
        || !headerFooterIsDefault(s) || !s.rowBreaks.isEmpty() || !s.colBreaks.isEmpty();
    if (!any) {
        return; // Excel's defaults: nothing to say
    }
    QByteArray xml;
    if (!printOptionsAreDefault(s)) {
        xml += QByteArrayLiteral("<printOptions") + flagAttribute("horizontalCentered", s.centerHorizontally)
            + flagAttribute("verticalCentered", s.centerVertically) + flagAttribute("headings", s.headings)
            + flagAttribute("gridLines", s.gridLines) + QByteArrayLiteral("/>");
    }
    // AGENT-NOTE: Excel always writes <pageMargins> with a <pageSetup>; so
    // does this, whenever anything about printing is written.
    xml += QByteArrayLiteral("<pageMargins") + attribute("left", measureText(s.marginLeft))
        + attribute("right", measureText(s.marginRight)) + attribute("top", measureText(s.marginTop))
        + attribute("bottom", measureText(s.marginBottom)) + attribute("header", measureText(s.marginHeader))
        + attribute("footer", measureText(s.marginFooter)) + QByteArrayLiteral("/>");
    xml += pageSetupXml(s);
    if (!headerFooterIsDefault(s)) {
        xml += headerFooterXml(s);
    }
    xml += breaksXml("rowBreaks", s.rowBreaks, Sheet::kMaxCols - 1);
    xml += breaksXml("colBreaks", s.colBreaks, Sheet::kMaxRows - 1);
    patch->printXml = xml;
}

} // namespace QindaOffice::Calc::IO
