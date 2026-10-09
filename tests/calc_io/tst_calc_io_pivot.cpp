// SPDX-License-Identifier: GPL-3.0-or-later
// M74 (D-083): Excel PivotTables in and out (pivot_fixtures.h). An
// Excel-shaped pivot becomes the model — compact with a column field,
// tabular with a filter and two data fields, dates grouped by years and
// months, a table source — and keeps its cells; an untouched one is saved
// byte for byte (refreshOnLoad only after its data changed); an edited or
// new one is written from the model and read back equal (its own Excel
// formats left out with a save warning); a PivotChart stays a PivotChart;
// a pivot with a calculated field stays read-only and carried. Every saved
// package is schema-valid with every relationship resolving.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_inspect.h"
#include "pivot_fixtures.h"

#include "pivot_cache.h"
#include "pivot_edit.h"
#include "sheet.h"
#include "structure.h"
#include "workbook.h"

#include <QFile>
#include <QTemporaryDir>

#include <QtTest>

#include <limits>

using namespace QindaOffice::Calc;
using namespace QindaOffice::Test;
using QindaOffice::Ooxml::Package;

class CalcIoPivotTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }

    bool open(const ExcelWorkbook &book, const QString &name, Workbook *wb, QStringList *warnings = nullptr)
    {
        QFile file(path(name));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(book.build());
        file.close();
        const QString schema = xlsxSchemaErrors(path(name));
        if (!schema.isEmpty()) {
            qWarning("fixture %s is not schema-valid: %s", qPrintable(name), qPrintable(schema));
            return false;
        }
        QString error;
        return IO::readXlsxFile(path(name), wb, &error, warnings);
    }

    std::shared_ptr<const Package> save(const Workbook &wb, const QString &name)
    {
        QString error;
        if (!IO::writeXlsxFile(wb, path(name), &error)) {
            qWarning("write failed: %s", qPrintable(error));
            return nullptr;
        }
        const QString schema = xlsxSchemaErrors(path(name));
        if (!schema.isEmpty()) {
            qWarning("saved package is not schema-valid: %s", qPrintable(schema));
            return nullptr;
        }
        keepForInspection(path(name));
        auto package = Package::load(path(name), &error);
        if (package && !brokenWiring(*package).isEmpty()) {
            qWarning("broken wiring: %s", qPrintable(brokenWiring(*package).join(QLatin1String("; "))));
            return nullptr;
        }
        return package;
    }

    // The pivot parts a saved sheet reaches: table, cache definition, records.
    static QStringList pivotParts(const Package &package, const QString &sheet)
    {
        const QStringList tables = partsByType(package, savedSheetPart(package, sheet), officeRel("pivotTable"));
        if (tables.size() != 1) {
            return {};
        }
        const QStringList caches = partsByType(package, tables.first(), officeRel("pivotCacheDefinition"));
        const QStringList records = caches.isEmpty() ? QStringList()
                                                     : partsByType(package, caches.first(), officeRel("pivotCacheRecords"));
        return {tables.first(), caches.value(0), records.value(0)};
    }

    static const PivotTable &only(const Workbook &wb, qint32 sheet = 1) { return wb.sheetAt(sheet).pivots().first(); }

    // A readable account of a pivot's settings, for a failing comparison.
    static QString describe(const PivotTable &p)
    {
        QStringList out;
        out << QStringLiteral("rows %1 cols %2 pages %3").arg(toList(p.rowFields), toList(p.colFields), toList(p.pageFields));
        for (qsizetype f = 0; f < p.fields.size(); ++f) {
            const PivotFieldSettings &s = p.fields.at(f);
            QList<qint32> hidden = s.hidden.values();
            std::sort(hidden.begin(), hidden.end());
            out << QStringLiteral("field %1 '%2' order %3 hidden %4 sort %5/%6 sub %7/%8 top %9 blank %10 repeat %11 all %12 "
                                 "multi %13 page %14 filters %15")
                       .arg(f).arg(s.caption, toList(s.order), toList(hidden)).arg(int(s.sort)).arg(s.sortData)
                       .arg(s.subtotals).arg(s.customSubtotals.size()).arg(s.subtotalTop).arg(s.insertBlankRow)
                       .arg(s.repeatLabels).arg(s.showAllItems).arg(s.multipleSelection).arg(s.pageItem).arg(s.filters.size());
        }
        for (const PivotDataField &d : p.dataFields) {
            out << QStringLiteral("data '%1' fld %2 fn %3 as %4 base %5/%6 fmt %7")
                       .arg(d.name).arg(d.field).arg(int(d.function)).arg(int(d.showAs)).arg(d.baseField).arg(d.baseItem)
                       .arg(d.numberFormat);
        }
        const PivotOptions &o = p.options;
        out << QStringLiteral("form %1 grand %2/%3 missing %4 '%5' headers %6 style %7").arg(int(o.form))
                   .arg(o.rowGrandTotals).arg(o.colGrandTotals).arg(o.showMissing).arg(o.missingCaption).arg(o.showHeaders)
                   .arg(o.style.name);
        return out.join(QLatin1Char('\n'));
    }
    static QString toList(const QList<qint32> &list)
    {
        QStringList out;
        for (const qint32 v : list) {
            out << QString::number(v);
        }
        return out.join(QLatin1Char(','));
    }

    // Changes the pivot and lays it out again (what a QindaCalc command does).
    template <typename Change>
    static bool edit(Workbook &wb, Change change, qint32 sheet = 1)
    {
        PivotTable pivot = only(wb, sheet);
        const PivotCache &cache = *pivotCacheOf(wb, pivot.cacheId);
        change(pivot, cache);
        pivotNormalize(pivot, cache);
        wb.sheetAt(sheet).mutablePivots()[0] = pivot;
        return renderPivotTable(wb, {sheet, 0});
    }

private slots:
    void excelPivotsBecomeTheModel()
    {
        Workbook wb;
        QStringList warnings;
        QVERIFY(open(regionsByProductBook(), QStringLiteral("a.xlsx"), &wb, &warnings));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
        QCOMPARE(wb.sheetAt(1).pivots().size(), 1);
        const PivotTable &pivot = only(wb);
        QVERIFY2(pivot.modelled, qPrintable(pivot.note));
        QCOMPARE(pivot.name, QStringLiteral("PivotTable1"));
        QCOMPARE(pivot.rowFields, (QList<qint32>{0}));
        QCOMPARE(pivot.colFields, (QList<qint32>{1}));
        QCOMPARE(pivot.dataFields.size(), 1);
        QCOMPARE(pivot.fields.at(0).order, (QList<qint32>{0, 2, 1}));
        QCOMPARE(pivot.extent, (CellRange{{2, 0}, {7, 3}}));
        const PivotCache &cache = *pivotCacheOf(wb, pivot.cacheId);
        QCOMPARE(cache.id, 1);
        QCOMPARE(cache.recordCount, 6);
        QCOMPARE(cache.fields.at(2).items.size(), 6); // dates inline in the records
        QCOMPARE(cache.source.sheet, QStringLiteral("Data"));
        QVERIFY(!cache.extensions.isEmpty());
        // GETPIVOTDATA answers from it.
        QVERIFY(wb.setCellInput(1, {0, 6}, QStringLiteral("=GETPIVOTDATA(\"Sales\",$A$3,\"Region\",\"West\",\"Product\",\"Pear\")")));
        QCOMPARE(wb.cellValue(1, 0, 6), Value(20.0));
        for (const auto &[book, name] : {std::pair{tabularBook(), QStringLiteral("b.xlsx")},
                                          std::pair{groupedDatesBook(), QStringLiteral("c.xlsx")},
                                          std::pair{tableSourceBook(), QStringLiteral("d.xlsx")}}) {
            Workbook other;
            QVERIFY(open(book, name, &other));
            QVERIFY2(only(other).modelled, qPrintable(name + QLatin1Char(' ') + only(other).note));
        }
        Workbook tabular;
        QVERIFY(open(tabularBook(), QStringLiteral("b2.xlsx"), &tabular));
        QCOMPARE(only(tabular).options.form, PivotForm::Tabular);
        QCOMPARE(only(tabular).pageFields, (QList<qint32>{2}));
        QCOMPARE(only(tabular).colFields, (QList<qint32>{kPivotValuesField}));
        QCOMPARE(only(tabular).extent, (CellRange{{0, 0}, {11, 3}}));
        Workbook grouped;
        QVERIFY(open(groupedDatesBook(), QStringLiteral("c2.xlsx"), &grouped));
        const PivotCache &dates = *pivotCacheOf(grouped, only(grouped).cacheId);
        QCOMPARE(dates.fields.size(), 6);
        QCOMPARE(dates.fields.at(2).group.by, PivotGroupBy::Months);
        QCOMPARE(dates.fields.at(5).group.by, PivotGroupBy::Years);
        QCOMPARE(dates.fields.at(5).group.names, (QStringList{"<1/5/2024", "2024", "2025", ">3/4/2025"}));
        Workbook table;
        QVERIFY(open(tableSourceBook(), QStringLiteral("d2.xlsx"), &table));
        QCOMPARE(pivotCacheOf(table, only(table).cacheId)->source.name, QStringLiteral("Sales"));
    }

    void untouchedPivotsAreWrittenBackByteForByte()
    {
        Workbook wb;
        QVERIFY(open(regionsByProductBook(), QStringLiteral("untouched.xlsx"), &wb));
        const auto source = Package::load(path(QStringLiteral("untouched.xlsx")));
        const QStringList sourceParts = pivotParts(*source, QStringLiteral("Pivot"));
        wb.setCellValue(1, {20, 0}, QStringLiteral("an edit elsewhere"));
        auto saved = save(wb, QStringLiteral("untouched-saved.xlsx"));
        QVERIFY(saved);
        const QStringList parts = pivotParts(*saved, QStringLiteral("Pivot"));
        QCOMPARE(parts.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QVERIFY2(saved->data(parts.at(i)) == source->data(sourceParts.at(i)), qPrintable(parts.at(i)));
        }
        // The source data changes: the same bytes, the cache told to refresh.
        wb.setCellValue(0, {1, 3}, 100.0);
        saved = save(wb, QStringLiteral("stale-saved.xlsx"));
        QVERIFY(saved);
        const QStringList stale = pivotParts(*saved, QStringLiteral("Pivot"));
        QCOMPARE(saved->data(stale.at(0)), source->data(sourceParts.at(0)));
        QCOMPARE(saved->xml(stale.at(1))->attr(u"refreshOnLoad"), QStringLiteral("1"));
        QCOMPARE(saved->data(stale.at(2)), source->data(sourceParts.at(2)));
    }

    void editedPivotsAreWrittenFromTheModel()
    {
        Workbook wb;
        QVERIFY(open(regionsByProductBook(), QStringLiteral("edit.xlsx"), &wb));
        QVERIFY(edit(wb, [](PivotTable &p, const PivotCache &) {
            p.options.form = PivotForm::Tabular;
            p.options.style.name = QStringLiteral("PivotStyleMedium9");
        }));
        const QByteArray table = pivotTableFingerprint(only(wb));
        auto saved = save(wb, QStringLiteral("edit-saved.xlsx"));
        QVERIFY(saved);
        const QStringList parts = pivotParts(*saved, QStringLiteral("Pivot"));
        QCOMPARE(parts.size(), 3);
        const auto definition = saved->xml(parts.at(0));
        QCOMPARE(definition->attr(u"compact"), QStringLiteral("0"));
        QCOMPARE(definition->child(QString::fromUtf16(QindaOffice::Ooxml::Ns::S), u"location")->attr(u"ref"),
                 QStringLiteral("A3:D8"));
        QVERIFY(saved->data(parts.at(1)).contains("pivotCacheId=\"1\"")); // the cache's x14 id kept
        QCOMPARE(saved->xml(QStringLiteral("xl/workbook.xml"))->child(QString::fromUtf16(QindaOffice::Ooxml::Ns::S), u"pivotCaches")
                     ->elements().size(),
                 1);
        Workbook back;
        QString error;
        QVERIFY2(IO::readXlsxFile(path(QStringLiteral("edit-saved.xlsx")), &back, &error), qPrintable(error));
        QVERIFY2(only(back).modelled, qPrintable(only(back).note));
        QCOMPARE(pivotTableFingerprint(only(back)), table);
        QCOMPARE(only(back).options.style.name, QStringLiteral("PivotStyleMedium9"));
    }

    // A pivot's own formats (Excel's <formats>) stay while it is untouched
    // and are left out, with a save warning, once it is written from the model.
    void ownFormattingStaysUntilThePivotChanges()
    {
        ExcelWorkbook book = regionsByProductBook();
        for (ExcelPart &part : book.parts) {
            if (part.name.endsWith(QLatin1String("pivotTable1.xml"))) {
                part.data.replace("</dataFields>",
                                  "</dataFields><formats count=\"1\"><format dxfId=\"0\"><pivotArea type=\"all\""
                                  " dataOnly=\"0\" outline=\"0\" fieldPosition=\"0\"/></format></formats>");
            }
        }
        Workbook wb;
        QVERIFY(open(book, QStringLiteral("formats.xlsx"), &wb));
        QVERIFY2(only(wb).modelled, qPrintable(only(wb).note));
        QVERIFY(only(wb).customFormats);
        QString error;
        QStringList warnings;
        QVERIFY2(IO::writeXlsxFile(wb, path(QStringLiteral("formats-untouched.xlsx")), &error, &warnings), qPrintable(error));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char(' '))));
        const auto untouched = Package::load(path(QStringLiteral("formats-untouched.xlsx")), &error);
        QVERIFY(untouched);
        QVERIFY(untouched->data(pivotParts(*untouched, QStringLiteral("Pivot")).value(0)).contains("<formats count=\"1\">"));
        QVERIFY(edit(wb, [](PivotTable &p, const PivotCache &) { p.options.form = PivotForm::Outline; }));
        const auto edited = save(wb, QStringLiteral("formats-edited.xlsx"));
        QVERIFY(edited);
        QVERIFY(!edited->data(pivotParts(*edited, QStringLiteral("Pivot")).value(0)).contains("<formats"));
        QVERIFY2(IO::writeXlsxFile(wb, path(QStringLiteral("formats-edited-again.xlsx")), &error, &warnings),
                 qPrintable(error));
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.first().contains(QStringLiteral("cell formatting")));
    }

    // A PivotChart is shown (its series read) but saved as Excel wrote it,
    // c:pivotSource and all — untouched and after its pivot changed — and
    // never written a second time as a plain chart.
    void pivotChartsStayPivotCharts()
    {
        Workbook wb;
        QVERIFY(open(pivotChartBook(), QStringLiteral("pivot-chart.xlsx"), &wb));
        QCOMPARE(wb.charts.size(), 1);
        QCOMPARE(wb.charts.first().pivotSource, QStringLiteral("[regions.xlsx]Pivot!PivotTable1"));
        QCOMPARE(wb.charts.first().series.size(), 2);
        QByteArray source;
        for (const ExcelPart &part : pivotChartBook().parts) {
            source = part.name == QLatin1String("xl/charts/chart1.xml") ? part.data : source;
        }
        const auto chartsOf = [](const Package &package) {
            const QStringList drawings = partsByType(package, savedSheetPart(package, QStringLiteral("Pivot")), officeRel("drawing"));
            return drawings.size() == 1 ? partsByType(package, drawings.first(), officeRel("chart")) : QStringList();
        };
        for (const bool editFirst : {false, true}) {
            if (editFirst) {
                QVERIFY(edit(wb, [](PivotTable &p, const PivotCache &) { p.options.form = PivotForm::Tabular; }));
            }
            const QString name = editFirst ? QStringLiteral("pivot-chart-edited.xlsx") : QStringLiteral("pivot-chart-saved.xlsx");
            const auto saved = save(wb, name);
            QVERIFY(saved);
            const QStringList charts = chartsOf(*saved);
            QCOMPARE(charts.size(), 1); // carried, not written again from the model
            const QByteArray chart = saved->data(charts.first());
            QVERIFY(chart.contains("<c:pivotSource><c:name>[regions.xlsx]Pivot!PivotTable1</c:name>"));
            QCOMPARE(chart, source); // byte for byte
            Workbook back;
            QString error;
            QVERIFY2(IO::readXlsxFile(path(name), &back, &error), qPrintable(error));
            QCOMPARE(back.charts.size(), 1);
            QCOMPARE(back.charts.first().pivotSource, wb.charts.first().pivotSource);
        }
    }

    void refreshedPivotsRoundTrip()
    {
        for (const auto &[book, name] : {std::pair{tabularBook(), QStringLiteral("tabular")},
                                          std::pair{groupedDatesBook(), QStringLiteral("grouped")},
                                          std::pair{tableSourceBook(), QStringLiteral("table")}}) {
            Workbook wb;
            QVERIFY(open(book, name + QStringLiteral(".xlsx"), &wb));
            wb.setCellValue(0, {1, 3}, 100.0); // East Apple 10 → 100
            QVERIFY2(refreshPivotCache(wb, only(wb).cacheId), qPrintable(name));
            const QByteArray table = pivotTableFingerprint(only(wb));
            const QByteArray cache = pivotCacheFingerprint(*pivotCacheOf(wb, only(wb).cacheId));
            QVERIFY(save(wb, name + QStringLiteral("-saved.xlsx")));
            Workbook back;
            QVERIFY(IO::readXlsxFile(path(name + QStringLiteral("-saved.xlsx")), &back));
            QVERIFY2(only(back).modelled, qPrintable(name + QLatin1Char(' ') + only(back).note));
            QVERIFY2(pivotTableFingerprint(only(back)) == table, qPrintable(describe(only(wb)) + QStringLiteral("\n---\n")
                                                                            + describe(only(back))));
            QCOMPARE(pivotCacheFingerprint(*pivotCacheOf(back, only(back).cacheId)), cache);
            QVERIFY(!pivotCacheOf(back, only(back).cacheId)->refreshOnLoad);
        }
    }

    void newPivotsRoundTripEveryOption()
    {
        Workbook wb;
        QVERIFY(open(regionsByProductBook(), QStringLiteral("new.xlsx"), &wb));
        wb.addSheet(QStringLiteral("Report"));
        QString error;
        const PivotRef ref = createPivotTable(wb, PivotSource{QStringLiteral("Data"), {{0, 0}, {6, 4}}, QString()}, 2,
                                              {2, 0}, &error);
        QVERIFY2(ref.isValid(), qPrintable(error));
        const double automatic = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(edit(wb, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 0, PivotArea::Rows);
            pivotPlaceField(p, c, 1, PivotArea::Rows);
            pivotPlaceField(p, c, 2, PivotArea::Filters);
            pivotPlaceField(p, c, 3, PivotArea::Values);
            pivotPlaceField(p, c, 4, PivotArea::Values);
            p.dataFields[0].numberFormat = QStringLiteral("#,##0.00");
            p.dataFields[1].function = PivotFunction::Average;
            p.dataFields[1].name = QStringLiteral("Average Qty");
            p.dataFields[1].showAs = PivotShowAs::PercentOfParentRow;
            p.fields[0].sort = PivotSort::Descending;
            p.fields[0].sortData = 0;
            p.fields[1].customSubtotals = {PivotFunction::Sum, PivotFunction::Max};
            p.fields[0].repeatLabels = true;
            p.fields[0].insertBlankRow = true;
            p.fields[2].multipleSelection = true;
            p.fields[2].hidden = {5}; // the 2025 date unchecked
            PivotFilter top;
            top.kind = PivotFilter::Kind::Top;
            top.value1 = 2;
            p.fields[0].filters = {top};
            p.options.form = PivotForm::Outline;
            p.options.missingCaption = QStringLiteral("-");
            p.options.colGrandTotals = false;
        }, 2));
        QVERIFY(pivotGroupSelection(wb, {2, 0}, 1, {1}, &error)); // Pear in a group of its own
        QVERIFY(only(wb, 2).modelled);
        const QByteArray table = pivotTableFingerprint(only(wb, 2));
        QVERIFY(save(wb, QStringLiteral("new-saved.xlsx")));
        Workbook back;
        QVERIFY(IO::readXlsxFile(path(QStringLiteral("new-saved.xlsx")), &back, &error));
        QVERIFY2(only(back, 2).modelled, qPrintable(only(back, 2).note));
        QVERIFY2(pivotTableFingerprint(only(back, 2)) == table,
                 qPrintable(describe(only(wb, 2)) + QStringLiteral("\n---\n") + describe(only(back, 2))));
        QCOMPARE(only(back, 2).render->cells.size(), only(wb, 2).render->cells.size());
        // Two caches in one <pivotCaches>: the carried untouched one and the new one.
        const auto saved = Package::load(path(QStringLiteral("new-saved.xlsx")));
        QCOMPARE(saved->xml(QStringLiteral("xl/workbook.xml"))->child(QString::fromUtf16(QindaOffice::Ooxml::Ns::S), u"pivotCaches")
                     ->elements().size(),
                 2);
        Q_UNUSED(automatic);
    }

    void calculatedFieldsStayReadOnlyAndCarried()
    {
        Workbook wb;
        QStringList warnings;
        QVERIFY(open(calculatedFieldBook(), QStringLiteral("calculated.xlsx"), &wb, &warnings));
        QVERIFY(!only(wb).modelled);
        QVERIFY2(only(wb).note.contains(QStringLiteral("calculated")), qPrintable(only(wb).note));
        QCOMPARE(warnings.size(), 1);
        QCOMPARE(only(wb).extent, (CellRange{{2, 0}, {7, 3}}));
        QVERIFY(!renderPivotTable(wb, {1, 0}));
        QVERIFY(!refreshPivotCache(wb, only(wb).cacheId));
        const auto saved = save(wb, QStringLiteral("calculated-saved.xlsx"));
        QVERIFY(saved);
        const QStringList parts = pivotParts(*saved, QStringLiteral("Pivot"));
        QCOMPARE(parts.size(), 3);
        QVERIFY(saved->data(parts.at(1)).contains("formula=\"Sales*0.1\""));
        QCOMPARE(saved->xml(parts.at(1))->attr(u"refreshOnLoad"), QStringLiteral("1")); // as before M74
    }

    void slicersStayWhenTheCacheKeepsItsItems()
    {
        Workbook wb;
        QVERIFY(open(pivotBook(true), QStringLiteral("slicer.xlsx"), &wb));
        QVERIFY2(only(wb).modelled, qPrintable(only(wb).note));
        QVERIFY(edit(wb, [](PivotTable &p, const PivotCache &) { p.options.style.name = QStringLiteral("PivotStyleLight18"); }));
        const auto saved = save(wb, QStringLiteral("slicer-saved.xlsx"));
        QVERIFY(saved);
        const QString sheet = savedSheetPart(*saved, QStringLiteral("Pivot"));
        QVERIFY(saved->data(sheet).contains("uri=\"{A8765BA9-456A-4dab-B4F3-ACF838C121DE}\""));
        QCOMPARE(partsByType(*saved, QStringLiteral("xl/workbook.xml"),
                             QStringLiteral("http://schemas.microsoft.com/office/2007/relationships/slicerCache"))
                     .size(),
                 1);
        QVERIFY(saved->data(pivotParts(*saved, QStringLiteral("Pivot")).at(0)).contains("PivotStyleLight18"));
    }

    void deletedPivotsLeaveNothingBehind()
    {
        Workbook wb;
        QVERIFY(open(regionsByProductBook(), QStringLiteral("deleted.xlsx"), &wb));
        QVERIFY(removePivotTable(wb, {1, 0}));
        const auto saved = save(wb, QStringLiteral("deleted-saved.xlsx"));
        QVERIFY(saved);
        for (const QString &part : saved->partNames()) {
            QVERIFY2(!part.contains(QLatin1String("pivot")), qPrintable(part));
        }
        QVERIFY(saved->xml(QStringLiteral("xl/workbook.xml"))->child(QString::fromUtf16(QindaOffice::Ooxml::Ns::S), u"pivotCaches")
                == nullptr);
    }
};

QTEST_GUILESS_MAIN(CalcIoPivotTest)

#include "tst_calc_io_pivot.moc"
