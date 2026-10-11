// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_bridge.h"
#include "calc_io_schema.h"
#include "calc_engine/pivot_fixture.h"
#include "pivot_cache.h"
#include "pivot_fixtures.h"
#include <QFile>
#include <QTemporaryDir>
using namespace PivotTest;
class CalcIoPivotM90Test : public QObject {
    Q_OBJECT
private slots:
    void supportedExcelCalculatedFieldOpensEditable()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("excel-calculated.xlsx"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QindaOffice::Test::calculatedFieldBook().build());
        file.close();
        Workbook book;
        QString error;
        QVERIFY2(IO::readXlsxFile(path, &book, &error), qPrintable(error));
        QVERIFY2(book.sheetAt(1).pivots().first().modelled, qPrintable(book.sheetAt(1).pivots().first().note));
        QCOMPARE(book.pivotCaches.first().fields.last().formula, QStringLiteral("Sales*0.1"));
    }
    void calculatedFieldAndDateFilterRoundTrip()
    {
        Workbook book;
        fillSales(book);
        const auto ref = makePivot(book);
        PivotCacheField calculated;
        calculated.name = QStringLiteral("Margin");
        calculated.formula = QStringLiteral("Sales/Qty");
        calculated.database = false;
        book.pivotCaches[0].fields.append(calculated);
        QVERIFY(change(book, ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 2, PivotArea::Rows);
            pivotPlaceField(p, c, 5, PivotArea::Values);
            PivotFilter date;
            date.kind = PivotFilter::Kind::Date;
            date.dateType = QStringLiteral("dateBetween");
            date.text1 = QStringLiteral("2024-01-01");
            date.text2 = QStringLiteral("2024-12-31");
            p.fields[2].filters = {date};
        }));
        const auto before = body(book, ref);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("calculated.xlsx"));
        QString error;
        QVERIFY2(IO::writeXlsxFile(book, path, &error), qPrintable(error));
        const QString schema = QindaOffice::Test::xlsxSchemaErrors(path);
        QVERIFY2(schema.isEmpty(), qPrintable(schema));
        Workbook restored;
        QVERIFY2(IO::readXlsxFile(path, &restored, &error), qPrintable(error));
        QVERIFY2(restored.sheetAt(1).pivots().first().modelled, qPrintable(restored.sheetAt(1).pivots().first().note));
        QCOMPARE(restored.pivotCaches.first().fields.last().formula, QStringLiteral("Sales/Qty"));
        QCOMPARE(restored.sheetAt(1).pivots().first().fields[2].filters.first().dateType, QStringLiteral("dateBetween"));
        QCOMPARE(body(restored, ref), before);
        QVERIFY(refreshPivotCache(restored, restored.pivotCaches.first().id, &error));
        QCOMPARE(body(restored, ref), before);
    }
};
QTEST_GUILESS_MAIN(CalcIoPivotM90Test)
#include "tst_calc_io_pivot_m90.moc"
