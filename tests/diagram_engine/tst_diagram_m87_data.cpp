// SPDX-License-Identifier: GPL-3.0-or-later
// M87 (D-095) external data: tables read from CSV and Excel workbooks (the
// first row naming the columns, numbers as Excel shows them, a chosen
// sheet, .xls refused), Visio's Link Data to Shapes — rows linked by
// matching shape text, one row linked by hand, refresh rewriting the linked
// fields and reporting rows that are gone, unlink keeping the values,
// shapes made from rows — and a recordset removed unlinking its shapes.

#include "diagram_fixture.h"

#include "data_link.h"

#include "xlsxdocument.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTimeZone>

using namespace DiagramTest;

namespace {

const QDateTime kWhen(QDate(2026, 10, 9), QTime(12, 0), QTimeZone::UTC);

QString writeFile(const QTemporaryDir &dir, const char *name, const QByteArray &bytes)
{
    const QString path = dir.filePath(QString::fromLatin1(name));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        return {};
    }
    return path;
}

DataTable staff()
{
    return dataTableFromCsv(QStringLiteral("Name,Role,Phone\nAnn Lee,Director,555-0101\n\"Bo, Jr\",Coordinator,555-0102\n"
                                           ",,\nCy,Driver,555-0103\n"));
}

} // namespace

class DiagramM87DataTest final : public QObject {
    Q_OBJECT
private slots:
    void readsCsvAndWorkbooks()
    {
        const DataTable t = staff();
        QCOMPARE(t.columns, (QStringList{QStringLiteral("Name"), QStringLiteral("Role"), QStringLiteral("Phone")}));
        QCOMPARE(t.rows.size(), 3); // the blank row is skipped
        QCOMPARE(t.rows.at(1).at(0), QStringLiteral("Bo, Jr"));
        const DataTable dup = dataTableFromCsv(QStringLiteral("Name,,Name\n1,2,3\n"));
        QCOMPARE(dup.columns, (QStringList{QStringLiteral("Name"), QStringLiteral("Column 2"), QStringLiteral("Name (2)")}));

        QTemporaryDir dir;
        QXlsx::Document book;
        book.addSheet(QStringLiteral("Notes")); // left empty
        book.addSheet(QStringLiteral("Cover"));
        book.selectSheet(QStringLiteral("Cover"));
        book.write(QStringLiteral("A1"), QStringLiteral("Inventory, see next sheet"));
        book.addSheet(QStringLiteral("Servers"));
        book.selectSheet(QStringLiteral("Servers"));
        book.write(QStringLiteral("A1"), QStringLiteral("Host"));
        book.write(QStringLiteral("B1"), QStringLiteral("CPU (MHz)"));
        book.write(QStringLiteral("A2"), QStringLiteral("files"));
        book.write(QStringLiteral("B2"), 2400);
        book.write(QStringLiteral("A3"), QStringLiteral("print"));
        book.write(QStringLiteral("B3"), 1200.5);
        const QString path = dir.filePath(QStringLiteral("servers.xlsx"));
        QVERIFY(book.saveAs(path));
        DataTable read;
        QString error;
        QVERIFY2(readDataTable(path, QStringLiteral("Servers"), &read, &error), qPrintable(error));
        QCOMPARE(read.sheets, (QStringList{QStringLiteral("Notes"), QStringLiteral("Cover"), QStringLiteral("Servers")}));
        QCOMPARE(read.sheet, QStringLiteral("Servers"));
        QCOMPARE(read.columns, (QStringList{QStringLiteral("Host"), QStringLiteral("CPU (MHz)")}));
        QCOMPARE(read.rows.at(0), (QStringList{QStringLiteral("files"), QStringLiteral("2400")}));
        QCOMPARE(read.rows.at(1).at(1), QStringLiteral("1200.5"));
        // The first sheet holding data when none is named (Notes is empty).
        QVERIFY(readDataTable(path, {}, &read, &error));
        QCOMPARE(read.sheet, QStringLiteral("Cover"));
        // Refusals say why.
        QVERIFY(!readDataTable(writeFile(dir, "old.xls", "x"), {}, &read, &error));
        QVERIFY(error.contains(QStringLiteral(".xlsx")));
        QVERIFY(!readDataTable(dir.filePath(QStringLiteral("gone.csv")), {}, &read, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(readDataTable(writeFile(dir, "staff.csv", "Name,Role\nAnn,Lead\n"), {}, &read, &error));
        QCOMPARE(read.rows.size(), 1);
    }

    void linkRefreshAndUnlink()
    {
        Fixture f;
        const QString ann = f.shape("orgPerson", 200, 200, "Ann Lee");
        const QString bo = f.shape("orgPerson", 450, 200, "Bo, Jr");
        const QString other = f.shape("process", 700, 200, "Somebody else");
        Ops::setData(f.page(), ann, QStringLiteral("Desk"), QStringLiteral("2F")); // a field of its own
        const QString id = Ops::addRecordset(f.drawing, staff(), {}, QStringLiteral("/data/staff.csv"), 0, kWhen);
        QCOMPARE(f.drawing.recordset(id)->name, QStringLiteral("staff"));
        // Automatically link: shape text = Name.
        QCOMPARE(Ops::autoLink(f.drawing, f.page(), id, 0, {}), 2);
        const Shape *a = f.page().shape(ann);
        QCOMPARE(a->dataLink, (DataLink{id, QStringLiteral("Ann Lee")}));
        QCOMPARE(dataValue(a->data, QStringLiteral("Role")), QStringLiteral("Director"));
        QCOMPARE(dataValue(a->data, QStringLiteral("Desk")), QStringLiteral("2F"));
        QVERIFY(!f.page().shape(other)->dataLink.linked());
        // A row linked by hand (Cy, row 2) — by position when keyless.
        const QString keyless = Ops::addRecordset(f.drawing, staff(), QStringLiteral("Staff"), {}, -1, kWhen);
        QVERIFY(Ops::linkShapes(f.drawing, f.page(), {other}, keyless, 2));
        QCOMPARE(f.page().shape(other)->dataLink.row, QStringLiteral("#3"));
        QCOMPARE(dataValue(f.page().shape(other)->data, QStringLiteral("Name")), QStringLiteral("Cy"));
        // Refresh: new values flow in; a row that is gone is counted and its
        // shape keeps its values.
        const DataTable next = dataTableFromCsv(QStringLiteral("Phone,Name,Role\n555-0199,Ann Lee,Chair\n"));
        int missing = -1;
        QVERIFY(Ops::refreshRecordset(f.drawing, id, next, kWhen.addDays(1), &missing));
        QCOMPARE(missing, 1); // Bo
        QCOMPARE(f.drawing.recordset(id)->keyColumn, 1); // still "Name", now the second column
        a = f.page().shape(ann);
        QCOMPARE(dataValue(a->data, QStringLiteral("Role")), QStringLiteral("Chair"));
        QCOMPARE(dataValue(a->data, QStringLiteral("Phone")), QStringLiteral("555-0199"));
        QCOMPARE(dataValue(f.page().shape(bo)->data, QStringLiteral("Role")), QStringLiteral("Coordinator"));
        // Unlink keeps the values; removing data unlinks everything.
        QVERIFY(Ops::unlinkShapes(f.page(), {ann}));
        QVERIFY(!f.page().shape(ann)->dataLink.linked());
        QCOMPARE(dataValue(f.page().shape(ann)->data, QStringLiteral("Role")), QStringLiteral("Chair"));
        QVERIFY(Ops::removeRecordset(f.drawing, keyless));
        QVERIFY(!f.page().shape(other)->dataLink.linked());
        QCOMPARE(f.drawing.recordsets.size(), 1);
    }

    void shapesFromRows()
    {
        Fixture f;
        const QString id = Ops::addRecordset(f.drawing, staff(), QStringLiteral("Staff"), {}, 0, kWhen);
        const QStringList made = Ops::shapesFromRows(f.drawing, f.page(), id, {0, 1, 2}, QStringLiteral("orgPerson"),
                                                     QPointF(100, 100), 0);
        QCOMPARE(made.size(), 3);
        QCOMPARE(f.page().shape(made.at(1))->text, QStringLiteral("Bo, Jr"));
        QCOMPARE(f.page().shape(made.at(2))->dataLink.row, QStringLiteral("Cy"));
        QCOMPARE(f.page().shape(made.at(0))->rect.center(), QPointF(100, 100));
        QVERIFY(f.page().shape(made.at(1))->rect.center().x() > f.page().shape(made.at(0))->rect.right());
        QVERIFY(Ops::shapesFromRows(f.drawing, f.page(), id, {9}, QStringLiteral("orgPerson"), {}, 0).isEmpty());
        QVERIFY(Ops::shapesFromRows(f.drawing, f.page(), QStringLiteral("r404"), {0}, QStringLiteral("orgPerson"), {}, 0).isEmpty());
    }
};

QTEST_MAIN(DiagramM87DataTest)
#include "tst_diagram_m87_data.moc"
