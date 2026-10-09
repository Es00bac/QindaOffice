// SPDX-License-Identifier: GPL-3.0-or-later
// M66 (D-075): LibreOffice re-opens what QindaCalc writes — a second,
// independent reader beside the schema gate. Each saved package is
// converted headless to flat ODS and the result is read back: links stay
// links (external and to a place in the workbook), a collapsed group stays
// a collapsed row group, a protected sheet stays protected, unlocked and
// formula-hidden cells keep their flags, a
// spill's values are all there, and a CARRIED pivot table is imported as a
// pivot table over the source range the carry stage moved — proof that the
// copied cache and its wiring are coherent. The query-table, sheet-parts
// and macro workbooks re-open too.
//
// Skips (whole) when soffice is missing or cannot convert a plain workbook
// in this environment; once a plain workbook converts, a package that does
// not is a failure. soffice runs with the svp backend and a private
// profile (support/soffice_env.h): nothing reaches a desktop session.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_fixtures.h"
#include "carry_inspect.h"
#include "m69_fixtures.h"
#include "pivot_fixtures.h"
#include "support/soffice_env.h"

#include "hyperlink.h"
#include "outline.h"
#include "pivot_edit.h"
#include "protection.h"
#include "structure.h"
#include "workbook.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <QtTest>

using namespace QindaOffice::Calc;
using namespace QindaOffice::Test;

namespace {

constexpr QStringView kOfficeNs = u"urn:oasis:names:tc:opendocument:xmlns:office:1.0";
constexpr QStringView kTableNs = u"urn:oasis:names:tc:opendocument:xmlns:table:1.0";
constexpr QStringView kTextNs = u"urn:oasis:names:tc:opendocument:xmlns:text:1.0";
constexpr QStringView kXlinkNs = u"http://www.w3.org/1999/xlink";

QString a1(int row, int col)
{
    QString letters;
    for (int c = col + 1; c > 0; c = (c - 1) / 26) {
        letters.prepend(QChar(u'A' + (c - 1) % 26));
    }
    return letters + QString::number(row + 1);
}

// One sheet of a flat ODS document, as far as these gates look.
struct OdsSheet {
    bool isProtected = false;
    int rowGroups = 0;
    QSet<int> collapsedRows;      // 0-based
    QHash<QString, QString> text; // "D1" → the cell's text
    QStringList links;            // every xlink:href, in document order
};

struct OdsBook {
    QMap<QString, OdsSheet> sheets;
    QStringList pivotSources; // each data pilot table's source range
    QByteArray xml;
};

OdsBook readFlatOds(const QString &path)
{
    OdsBook book;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return book;
    }
    book.xml = file.readAll();
    QXmlStreamReader xml(book.xml);
    OdsSheet *sheet = nullptr;
    int row = 0;
    int rowRepeat = 1;
    int col = 0;
    int colRepeat = 1;
    bool inCell = false;
    int paragraphs = 0; // open text:p elements of the cell (pretty-printed
                        // flat ODS has white space between elements)
    int notes = 0;      // open office:annotation elements: not cell text
    QString cellText;
    const auto number = [&xml](QStringView name) {
        const QStringView value = xml.attributes().value(kTableNs, name);
        return value.isEmpty() ? 1 : value.toInt();
    };
    while (!xml.atEnd()) {
        xml.readNext();
        const QStringView ns = xml.namespaceUri();
        const QStringView name = xml.name();
        if (xml.isStartElement()) {
            if (ns == kTableNs && name == u"table") {
                sheet = &book.sheets[xml.attributes().value(kTableNs, u"name").toString()];
                sheet->isProtected = xml.attributes().value(kTableNs, u"protected") == u"true";
                row = 0;
            } else if (ns == kTableNs && name == u"table-row-group" && sheet != nullptr) {
                ++sheet->rowGroups;
            } else if (ns == kTableNs && name == u"table-row" && sheet != nullptr) {
                rowRepeat = number(u"number-rows-repeated");
                col = 0;
                if (xml.attributes().value(kTableNs, u"visibility") == u"collapse") {
                    for (int r = row; r < row + qMin(rowRepeat, 10000); ++r) {
                        sheet->collapsedRows.insert(r);
                    }
                }
            } else if (ns == kTableNs && (name == u"table-cell" || name == u"covered-table-cell")) {
                colRepeat = number(u"number-columns-repeated");
                inCell = true;
                cellText.clear();
            } else if (ns == kOfficeNs && name == u"annotation") {
                ++notes;
            } else if (ns == kTextNs && name == u"p" && inCell) {
                if (paragraphs == 0 && notes == 0 && !cellText.isEmpty()) {
                    cellText += QLatin1Char('\n');
                }
                ++paragraphs;
            } else if (ns == kTextNs && name == u"a" && sheet != nullptr) {
                sheet->links.append(xml.attributes().value(kXlinkNs, u"href").toString());
            } else if (ns == kTableNs && name == u"source-cell-range") {
                book.pivotSources.append(xml.attributes().value(kTableNs, u"cell-range-address").toString());
            }
        } else if (xml.isCharacters() && inCell && paragraphs > 0 && notes == 0) {
            cellText += xml.text();
        } else if (xml.isEndElement()) {
            if (ns == kOfficeNs && name == u"annotation") {
                --notes;
            } else if (ns == kTextNs && name == u"p" && inCell) {
                --paragraphs;
            } else if (ns == kTableNs && (name == u"table-cell" || name == u"covered-table-cell")) {
                if (!cellText.isEmpty() && sheet != nullptr) {
                    for (int r = row; r < row + qMin(rowRepeat, 100); ++r) {
                        for (int c = col; c < col + qMin(colRepeat, 100); ++c) {
                            sheet->text.insert(a1(r, c), cellText);
                        }
                    }
                }
                col += colRepeat;
                inCell = false;
            } else if (ns == kTableNs && name == u"table-row") {
                row += rowRepeat;
            } else if (ns == kTableNs && name == u"table") {
                sheet = nullptr;
            }
        }
    }
    return book;
}

} // namespace

class CalcIoLibreTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void linksOutlinesProtectionAndSpills();
    void carriedPivotTableIsImported();
    void writtenPivotTableIsImported();
    void carriedWorkbookAndSheetPartsReopen();
    void tablesArraysAndChartSheetsReopen();

private:
    QTemporaryDir m_dir;
    QString m_skip;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    // soffice --convert-to fods; the flat ODS path, empty when none came out.
    QString convert(const QString &package, QString *log = nullptr);
    bool openFixture(const ExcelWorkbook &book, const QString &name, Workbook *wb);
    QString saveChecked(const Workbook &wb, const QString &name);
};

QString CalcIoLibreTest::convert(const QString &package, QString *log)
{
    QProcess soffice;
    startSoffice(soffice, {QStringLiteral("--headless"), QStringLiteral("--norestore"), QStringLiteral("--nologo"),
                           QStringLiteral("-env:UserInstallation=file://%1/profile").arg(m_dir.path()),
                           QStringLiteral("--convert-to"), QStringLiteral("fods"), QStringLiteral("--outdir"),
                           path(QStringLiteral("ods")), package});
    const bool finished = soffice.waitForFinished(180000);
    const QString out = path(QStringLiteral("ods/%1.fods").arg(QFileInfo(package).completeBaseName()));
    if (log != nullptr) {
        *log = QStringLiteral("finished=%1 exit=%2 %3 %4")
                   .arg(finished)
                   .arg(soffice.exitCode())
                   .arg(QString::fromUtf8(soffice.readAllStandardOutput()),
                        QString::fromUtf8(soffice.readAllStandardError()));
    }
    if (!QFileInfo::exists(out)) {
        return {};
    }
    keepForInspection(out);
    return out;
}

void CalcIoLibreTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QProcess probe;
    startSoffice(probe, {QStringLiteral("--headless"), QStringLiteral("--version")});
    if (!probe.waitForFinished(15000) || probe.exitCode() != 0) {
        QSKIP("LibreOffice unavailable in this environment");
    }
    // The control: a plain workbook must convert, or the environment cannot.
    Workbook plain;
    plain.addSheet(QStringLiteral("Plain"));
    plain.setCellValue(0, {0, 0}, Value(QStringLiteral("control")));
    QVERIFY(IO::writeXlsxFile(plain, path(QStringLiteral("control.xlsx"))));
    QString log;
    if (convert(path(QStringLiteral("control.xlsx")), &log).isEmpty()) {
        qWarning("%s", qPrintable(log));
        QSKIP("LibreOffice cannot convert in this environment");
    }
}

bool CalcIoLibreTest::openFixture(const ExcelWorkbook &book, const QString &name, Workbook *wb)
{
    QFile file(path(name));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(book.build());
    file.close();
    QString error;
    if (!IO::readXlsxFile(path(name), wb, &error)) {
        qWarning("read failed: %s", qPrintable(error));
        return false;
    }
    return true;
}

QString CalcIoLibreTest::saveChecked(const Workbook &wb, const QString &name)
{
    QString error;
    if (!IO::writeXlsxFile(wb, path(name), &error)) {
        qWarning("write failed: %s", qPrintable(error));
        return {};
    }
    const QString schema = xlsxSchemaErrors(path(name));
    if (!schema.isEmpty()) {
        qWarning("saved package is not schema-valid: %s", qPrintable(schema));
        return {};
    }
    keepForInspection(path(name));
    return path(name);
}

void CalcIoLibreTest::linksOutlinesProtectionAndSpills()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Data"));
    wb.addSheet(QStringLiteral("Locked"));
    const QStringList regions{QStringLiteral("East"), QStringLiteral("West"), QStringLiteral("East"),
                              QStringLiteral("North")};
    wb.setCellValue(0, {0, 0}, Value(QStringLiteral("Region")));
    wb.setCellValue(0, {0, 1}, Value(QStringLiteral("Sales")));
    for (int r = 0; r < regions.size(); ++r) {
        wb.setCellValue(0, {r + 1, 0}, Value(regions.at(r)));
        wb.setCellValue(0, {r + 1, 1}, Value(double((r + 1) * 10)));
    }
    QVERIFY(wb.setCellInput(0, {0, 3}, QStringLiteral("=SORT(B2:B5,1,-1)")));
    QVERIFY(wb.setCellInput(0, {0, 4}, QStringLiteral("=UNIQUE(A2:A5)")));
    // Links: a web page, and a place on the other sheet.
    wb.setCellValue(0, {6, 0}, Value(QStringLiteral("Report")));
    wb.setCellValue(0, {7, 0}, Value(QStringLiteral("To the locked sheet")));
    Hyperlink web;
    web.range = {{6, 0}, {6, 0}};
    web.address = QStringLiteral("https://example.com/report");
    web.tooltip = QStringLiteral("The quarterly report");
    Hyperlink place;
    place.range = {{7, 0}, {7, 0}};
    place.location = QStringLiteral("Locked!B2");
    wb.sheetAt(0).setHyperlinks({web, place});
    // Rows 10-12 grouped and collapsed under the summary row 13.
    for (int r = 9; r <= 12; ++r) {
        wb.setCellValue(0, {r, 0}, Value(r < 12 ? QStringLiteral("detail %1").arg(r - 8) : QStringLiteral("summary")));
    }
    SheetOutline outline;
    QVERIFY(groupLines(outline, OutlineAxis::Rows, 9, 11));
    const auto group = groupAt(outline, OutlineAxis::Rows, 10);
    QVERIFY(group.has_value());
    setGroupCollapsed(outline, OutlineAxis::Rows, *group, true);
    wb.sheetAt(0).setOutline(outline);
    // The locked sheet: protected with a password, B2 open, C2's formula hidden.
    wb.setCellValue(1, {1, 0}, Value(QStringLiteral("Rate")));
    wb.setCellValue(1, {1, 1}, Value(0.25));
    QVERIFY(wb.setCellInput(1, {1, 2}, QStringLiteral("=B2*4")));
    CellStyle open;
    open.locked = false;
    wb.sheetAt(1).setStyleIdAt({1, 1}, wb.addStyle(open));
    CellStyle secret;
    secret.hiddenFormula = true;
    wb.sheetAt(1).setStyleIdAt({1, 2}, wb.addStyle(secret));
    SheetProtection protection;
    protection.enabled = true;
    setPassword(&protection, QStringLiteral("s3cret"));
    wb.sheetAt(1).setProtection(protection);
    wb.protection.lockStructure = true;
    setPassword(&wb.protection, QStringLiteral("book"));
    wb.recalcAll();

    const QString xlsx = saveChecked(wb, QStringLiteral("features.xlsx"));
    QVERIFY(!xlsx.isEmpty());
    QString log;
    const QString fods = convert(xlsx, &log);
    QVERIFY2(!fods.isEmpty(), qPrintable(log));
    const OdsBook book = readFlatOds(fods);
    QVERIFY2(book.sheets.contains(QStringLiteral("Data")) && book.sheets.contains(QStringLiteral("Locked")),
             qPrintable(QStringList(book.sheets.keys()).join(u',')));
    const OdsSheet &data = book.sheets.value(QStringLiteral("Data"));
    // The spills, every cell of them.
    const QStringList sorted{QStringLiteral("40"), QStringLiteral("30"), QStringLiteral("20"), QStringLiteral("10")};
    for (int r = 0; r < sorted.size(); ++r) {
        QCOMPARE(data.text.value(a1(r, 3)), sorted.at(r));
    }
    QCOMPARE(data.text.value(QStringLiteral("E3")), QStringLiteral("North"));
    // The links.
    QVERIFY2(data.links.contains(QStringLiteral("https://example.com/report")), qPrintable(data.links.join(u' ')));
    QVERIFY2(std::any_of(data.links.cbegin(), data.links.cend(),
                         [](const QString &href) {
                             return href.startsWith(u'#') && href.contains(QLatin1String("Locked"))
                                 && href.contains(QLatin1String("B2"));
                         }),
             qPrintable(data.links.join(u' ')));
    // The collapsed group.
    QVERIFY(data.rowGroups >= 1);
    for (int r = 9; r <= 11; ++r) {
        QVERIFY2(data.collapsedRows.contains(r), qPrintable(QStringLiteral("row %1 not collapsed").arg(r + 1)));
    }
    QVERIFY(!data.collapsedRows.contains(12));
    QCOMPARE(data.text.value(QStringLiteral("A13")), QStringLiteral("summary"));
    // Protection: the sheet and the cell flags.
    QVERIFY(!data.isProtected);
    QVERIFY(book.sheets.value(QStringLiteral("Locked")).isProtected);
    // AGENT-NOTE: no assertion on the workbook's structure lock here.
    // LibreOffice 25.2 does not import <workbookProtection> from OOXML in
    // any form (tried: SHA-512, legacy workbookPassword, none — its own
    // xlsx export then writes an empty element), so a fods cannot show it.
    // tst_calc_io_protect reads it back; Excel is the desktop check.
    QVERIFY(book.xml.contains("cell-protect=\"none\""));
    QVERIFY(book.xml.contains("formula-hidden"));
    QCOMPARE(book.sheets.value(QStringLiteral("Locked")).text.value(QStringLiteral("C2")), QStringLiteral("1"));
}

void CalcIoLibreTest::carriedPivotTableIsImported()
{
    Workbook wb;
    QVERIFY(openFixture(pivotBook(true), QStringLiteral("pivot-source.xlsx"), &wb));
    // The data sheet renamed and moved down two rows: the carried cache
    // must name the new place, or LibreOffice reads the wrong range.
    QVERIFY(renameSheet(wb, 0, QStringLiteral("Sales Data")));
    applyStructureEdit(wb, StructureEdit{StructureOp::InsertRows, 0, 0, 2});
    const QString xlsx = saveChecked(wb, QStringLiteral("pivot.xlsx"));
    QVERIFY(!xlsx.isEmpty());
    QString log;
    const QString fods = convert(xlsx, &log);
    QVERIFY2(!fods.isEmpty(), qPrintable(log));
    const OdsBook book = readFlatOds(fods);
    QCOMPARE(book.sheets.value(QStringLiteral("Sales Data")).text.value(QStringLiteral("A3")), QStringLiteral("Region"));
    QCOMPARE(book.pivotSources.size(), 1);
    const QString source = book.pivotSources.first();
    QVERIFY2(source.contains(QLatin1String("Sales Data")) && source.contains(QLatin1String("A3"))
                 && source.contains(QLatin1String("B6")),
             qPrintable(source));
    // Its output still sums the data.
    const OdsSheet &pivot = book.sheets.value(QStringLiteral("Pivot"));
    QVERIFY2(QStringList(pivot.text.values()).contains(QStringLiteral("60")),
             qPrintable(QStringList(pivot.text.values()).join(u',')));
}

// M74 (D-083): a PivotTable written from the model — Region by Product,
// Sum of Sales, North unchecked — is a data pilot to LibreOffice, which
// recomputes it from the definition and the source: its numbers must be
// the ones QindaCalc shows.
void CalcIoLibreTest::writtenPivotTableIsImported()
{
    Workbook wb;
    QVERIFY(openFixture(regionsByProductBook(), QStringLiteral("pivot-model-source.xlsx"), &wb));
    wb.addSheet(QStringLiteral("Report"));
    QString error;
    const PivotRef ref = createPivotTable(wb, PivotSource{QStringLiteral("Data"), {{0, 0}, {6, 4}}, QString()}, 2, {2, 0},
                                          &error);
    QVERIFY2(ref.isValid(), qPrintable(error));
    PivotTable pivot = wb.sheetAt(2).pivots().first();
    const PivotCache &cache = *pivotCacheOf(wb, pivot.cacheId);
    QVERIFY(pivotPlaceField(pivot, cache, 0, PivotArea::Rows));
    QVERIFY(pivotPlaceField(pivot, cache, 1, PivotArea::Columns));
    QVERIFY(pivotPlaceField(pivot, cache, 3, PivotArea::Values));
    pivot.fields[0].hidden = {2}; // North
    wb.sheetAt(2).mutablePivots()[0] = pivot;
    QVERIFY(renderPivotTable(wb, ref, &error));
    QStringList ours;
    for (const PivotCell &cell : wb.sheetAt(2).pivots().first().render->cells) {
        if (cell.kind == PivotCellKind::Data && isNumber(cell.value)) {
            ours.append(QString::number(std::get<double>(cell.value)));
        }
    }
    std::sort(ours.begin(), ours.end());
    QCOMPARE(ours, (QStringList{"18", "20", "25", "27", "30", "48", "50", "7", "75"}));
    const QString xlsx = saveChecked(wb, QStringLiteral("pivot-model.xlsx"));
    QVERIFY(!xlsx.isEmpty());
    QString log;
    const QString fods = convert(xlsx, &log);
    QVERIFY2(!fods.isEmpty(), qPrintable(log));
    const OdsBook book = readFlatOds(fods);
    QCOMPARE(book.pivotSources.size(), 2); // the untouched pivot and the new one
    QStringList theirs;
    static const QRegularExpression numberText(QStringLiteral("^-?\\d+(\\.\\d+)?$"));
    for (const QString &text : book.sheets.value(QStringLiteral("Report")).text) {
        if (numberText.match(text).hasMatch()) {
            theirs.append(text);
        }
    }
    std::sort(theirs.begin(), theirs.end());
    QCOMPARE(theirs, ours);
}

void CalcIoLibreTest::carriedWorkbookAndSheetPartsReopen()
{
    struct Case {
        ExcelWorkbook book;
        QString name;
        QString saved;
    };
    const QList<Case> cases{{connectionBook(), QStringLiteral("connections-source.xlsx"), QStringLiteral("connections.xlsx")},
                            {sheetPartsBook(), QStringLiteral("parts-source.xlsx"), QStringLiteral("parts.xlsx")},
                            {macroBook(), QStringLiteral("macro-source.xlsm"), QStringLiteral("macro.xlsm")}};
    for (const Case &item : cases) {
        Workbook wb;
        QVERIFY2(openFixture(item.book, item.name, &wb), qPrintable(item.name));
        // An edit, so the save is not a copy of the source.
        wb.setCellValue(0, {20, 0}, Value(QStringLiteral("edited")));
        const QString saved = saveChecked(wb, item.saved);
        QVERIFY2(!saved.isEmpty(), qPrintable(item.saved));
        QString log;
        const QString fods = convert(saved, &log);
        QVERIFY2(!fods.isEmpty(), qPrintable(item.saved + QLatin1Char(' ') + log));
        const OdsBook book = readFlatOds(fods);
        QVERIFY2(!book.sheets.isEmpty(), qPrintable(item.saved));
        const OdsSheet &first = book.sheets.first();
        QVERIFY2(std::any_of(book.sheets.cbegin(), book.sheets.cend(),
                             [](const OdsSheet &sheet) {
                                 return sheet.text.value(QStringLiteral("A21")) == QLatin1String("edited");
                             }),
                 qPrintable(item.saved + QLatin1Char(' ') + QStringList(first.text.keys()).join(u',')));
    }
}

// M69 (D-078): a table with structured references, a calculated column, a
// totals row and legacy array formulas, edited and saved, re-opens with
// every value; a chart sheet re-opens as a sheet holding its chart, in its
// place between the worksheets.
void CalcIoLibreTest::tablesArraysAndChartSheetsReopen()
{
    Workbook wb;
    QVERIFY(openFixture(ordersBook(), QStringLiteral("orders-source.xlsx"), &wb));
    wb.setCellValue(0, {2, 1}, Value(6.0)); // South's Qty: 5 → 6, its Total 24
    const QString saved = saveChecked(wb, QStringLiteral("orders.xlsx"));
    QVERIFY(!saved.isEmpty());
    QString log;
    const QString fods = convert(saved, &log);
    QVERIFY2(!fods.isEmpty(), qPrintable(log));
    const OdsBook book = readFlatOds(fods);
    QCOMPARE(book.sheets.value(QStringLiteral("Orders")).text.value(QStringLiteral("D3")), QStringLiteral("24"));
    QCOMPARE(book.sheets.value(QStringLiteral("Orders")).text.value(QStringLiteral("D5")), QStringLiteral("65"));
    const OdsSheet &report = book.sheets.value(QStringLiteral("Report"));
    const QList<std::pair<const char *, const char *>> expected{{"A1", "65"}, {"A2", "11"}, {"B3", "4"}, {"C3", "6"},
                                                                {"E1", "4"},  {"E2", "12"}, {"E3", "6"}, {"F1", "9"},
                                                                {"A7", "10"}};
    for (const auto &[cell, text] : expected) {
        QCOMPARE(report.text.value(QString::fromLatin1(cell)), QString::fromLatin1(text));
    }
    Workbook charts;
    QVERIFY(openFixture(chartBook(), QStringLiteral("chart-source.xlsx"), &charts));
    charts.setCellValue(0, {1, 1}, Value(15.0));
    const QString chartSaved = saveChecked(charts, QStringLiteral("chartsheet.xlsx"));
    QVERIFY(!chartSaved.isEmpty());
    const QString chartFods = convert(chartSaved, &log);
    QVERIFY2(!chartFods.isEmpty(), qPrintable(log));
    const OdsBook chartBookOds = readFlatOds(chartFods);
    QStringList order;
    static const QRegularExpression kTable(QStringLiteral("<table:table table:name=\"([^\"]+)\""));
    auto tables = kTable.globalMatch(QString::fromUtf8(chartBookOds.xml));
    while (tables.hasNext()) {
        order.append(tables.next().captured(1));
    }
    order.removeAll(QStringLiteral("local-table")); // the chart's own data table, inside its object
    QCOMPARE(order, (QStringList{QStringLiteral("Q1 Data"), QStringLiteral("Sales Chart"), QStringLiteral("Summary")}));
    QVERIFY(chartBookOds.xml.contains("chart:chart"));
    QCOMPARE(chartBookOds.sheets.value(QStringLiteral("Summary")).text.value(QStringLiteral("A1")), QStringLiteral("65"));
}

QTEST_GUILESS_MAIN(CalcIoLibreTest)

#include "tst_calc_io_libre.moc"
