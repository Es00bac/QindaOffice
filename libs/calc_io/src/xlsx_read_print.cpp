// SPDX-License-Identifier: GPL-3.0-or-later
// Reading a sheet's print settings (xlsx_print.h, M78, D-087): the
// worksheet's own <printOptions>, <pageMargins>, <pageSetup>,
// <headerFooter>, <rowBreaks> and <colBreaks>, and sheetPr's
// <pageSetUpPr fitToPage>, into Sheet::PageSetup with the schema's
// defaults for what a file leaves out.
//
// AGENT-GUARD: only the worksheet's direct children count. A custom sheet
// view (<customSheetViews><customSheetView>) carries elements of the same
// names for its own print settings; reading those would overwrite the
// sheet's.

#include "xlsx_print.h"

#include "sheet.h"

#include <QXmlStreamReader>

#include <algorithm>

namespace QindaOffice::Calc::IO {

namespace {

bool flag(const QXmlStreamAttributes &attributes, QLatin1StringView name, bool fallback)
{
    const QStringView value = attributes.value(name);
    if (value.isEmpty()) {
        return fallback;
    }
    return value == QLatin1String("1") || value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

int number(const QXmlStreamAttributes &attributes, QLatin1StringView name, int fallback)
{
    bool ok = false;
    const int value = attributes.value(name).toInt(&ok);
    return ok ? value : fallback;
}

double measure(const QXmlStreamAttributes &attributes, QLatin1StringView name, double fallback)
{
    bool ok = false;
    const double value = attributes.value(name).toDouble(&ok);
    return ok ? value : fallback;
}

void readPageSetup(const QXmlStreamAttributes &a, Sheet::PageSetup *setup)
{
    setup->paperSize = number(a, QLatin1StringView("paperSize"), 0);
    setup->scale = number(a, QLatin1StringView("scale"), 100);
    setup->firstPageNumber = number(a, QLatin1StringView("firstPageNumber"), 1);
    // The schema's defaults: one page wide by one tall.
    setup->fitToWidth = number(a, QLatin1StringView("fitToWidth"), 1);
    setup->fitToHeight = number(a, QLatin1StringView("fitToHeight"), 1);
    setup->overThenDown = a.value(QLatin1StringView("pageOrder")) == QLatin1String("overThenDown");
    setup->orientation = a.value(QLatin1StringView("orientation")) == QLatin1String("landscape") ? 1 : 0;
    setup->blackAndWhite = flag(a, QLatin1StringView("blackAndWhite"), false);
    setup->draft = flag(a, QLatin1StringView("draft"), false);
    const QStringView comments = a.value(QLatin1StringView("cellComments"));
    setup->cellComments = comments == QLatin1String("atEnd") ? 1 : comments == QLatin1String("asDisplayed") ? 2 : 0;
    setup->useFirstPageNumber = flag(a, QLatin1StringView("useFirstPageNumber"), false);
    const QStringView errors = a.value(QLatin1StringView("errors"));
    setup->errors = errors == QLatin1String("blank") ? 1
        : errors == QLatin1String("dash")            ? 2
        : errors == QLatin1String("NA")              ? 3
                                                     : 0;
}

// The manual breaks of a <rowBreaks>/<colBreaks> (the reader on its start
// tag): each brk's id is the line a new page starts at.
QList<qint32> readBreaks(QXmlStreamReader &reader)
{
    QList<qint32> breaks;
    while (reader.readNextStartElement()) {
        if (reader.name() == QLatin1String("brk")) {
            const auto a = reader.attributes();
            const int id = number(a, QLatin1StringView("id"), 0);
            // Automatic breaks (man="0") are Excel's to work out again.
            if (id > 0 && flag(a, QLatin1StringView("man"), false) && !breaks.contains(id)) {
                breaks.append(id);
            }
        }
        reader.skipCurrentElement();
    }
    std::sort(breaks.begin(), breaks.end());
    return breaks;
}

void readHeaderFooter(QXmlStreamReader &reader, Sheet::PageSetup *setup)
{
    const auto a = reader.attributes();
    setup->differentOddEven = flag(a, QLatin1StringView("differentOddEven"), false);
    setup->differentFirst = flag(a, QLatin1StringView("differentFirst"), false);
    setup->scaleWithDoc = flag(a, QLatin1StringView("scaleWithDoc"), true);
    setup->alignWithMargins = flag(a, QLatin1StringView("alignWithMargins"), true);
    while (reader.readNextStartElement()) {
        const QStringView name = reader.name();
        QString *target = name == QLatin1String("oddHeader")     ? &setup->oddHeader
            : name == QLatin1String("oddFooter")                ? &setup->oddFooter
            : name == QLatin1String("evenHeader")               ? &setup->evenHeader
            : name == QLatin1String("evenFooter")               ? &setup->evenFooter
            : name == QLatin1String("firstHeader")              ? &setup->firstHeader
            : name == QLatin1String("firstFooter")              ? &setup->firstFooter
                                                                 : nullptr;
        if (target != nullptr) {
            *target = reader.readElementText();
        } else {
            reader.skipCurrentElement();
        }
    }
}

} // namespace

void readSheetPrint(const QByteArray &sheetXml, Sheet *sheet)
{
    Sheet::PageSetup setup = sheet->pageSetup();
    QXmlStreamReader reader(sheetXml);
    if (!reader.readNextStartElement()) {
        return; // not a worksheet
    }
    while (reader.readNextStartElement()) {
        const QStringView name = reader.name();
        const auto a = reader.attributes();
        if (name == QLatin1String("sheetPr")) {
            while (reader.readNextStartElement()) {
                if (reader.name() == QLatin1String("pageSetUpPr")) {
                    setup.fitToPage = flag(reader.attributes(), QLatin1StringView("fitToPage"), false);
                }
                reader.skipCurrentElement();
            }
            continue;
        }
        if (name == QLatin1String("printOptions")) {
            setup.centerHorizontally = flag(a, QLatin1StringView("horizontalCentered"), false);
            setup.centerVertically = flag(a, QLatin1StringView("verticalCentered"), false);
            setup.headings = flag(a, QLatin1StringView("headings"), false);
            setup.gridLines = flag(a, QLatin1StringView("gridLines"), false);
        } else if (name == QLatin1String("pageMargins")) {
            setup.marginLeft = measure(a, QLatin1StringView("left"), 0.7);
            setup.marginRight = measure(a, QLatin1StringView("right"), 0.7);
            setup.marginTop = measure(a, QLatin1StringView("top"), 0.75);
            setup.marginBottom = measure(a, QLatin1StringView("bottom"), 0.75);
            setup.marginHeader = measure(a, QLatin1StringView("header"), 0.3);
            setup.marginFooter = measure(a, QLatin1StringView("footer"), 0.3);
        } else if (name == QLatin1String("pageSetup")) {
            readPageSetup(a, &setup);
        } else if (name == QLatin1String("headerFooter")) {
            readHeaderFooter(reader, &setup);
            continue; // the reader is on </headerFooter>
        } else if (name == QLatin1String("rowBreaks") || name == QLatin1String("colBreaks")) {
            (name == QLatin1String("rowBreaks") ? setup.rowBreaks : setup.colBreaks) = readBreaks(reader);
            continue; // the reader is on the end tag
        }
        reader.skipCurrentElement();
    }
    sheet->setPageSetup(setup);
}

} // namespace QindaOffice::Calc::IO
