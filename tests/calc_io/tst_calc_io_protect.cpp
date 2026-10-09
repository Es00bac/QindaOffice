// SPDX-License-Identifier: GPL-3.0-or-later
// M66 (D-075): sheet and workbook protection as Excel writes it. The hash
// attributes Excel computed (SHA-512, salt, spin count) are kept verbatim,
// so the owner's password still opens the sheet in Excel after our save;
// the permission flags (formatCells, insertRows, …) and the workbook's
// lockStructure survive; and a cell format's protection (locked="0",
// hidden="1") is kept. Before M66 all of it was dropped on save — a
// protected workbook came back unprotected.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_inspect.h"
#include "excel_inspect.h"
#include "excel_package.h"

#include <QFile>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using QindaOffice::Ooxml::Package;
using QindaOffice::Ooxml::XmlElement;
using QindaOffice::Test::ExcelSheet;
using QindaOffice::Test::ExcelWorkbook;
using QindaOffice::Test::sml;

class CalcIoProtectTest : public QObject {
    Q_OBJECT
private slots:
    void excelProtectionSurvivesSave();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
};

namespace {

// The hash, salt and spin count as an Excel 365 save writes them for a
// password; the test never needs the password itself — the attributes
// travel verbatim.
const char kHash[] = "nYXKsyBUJ0hVK0Wfzhl2dVLnqxcydw0bMx6E1bfOrIrSz7Oc0AxF8b9V6S6xxYZxfLgGEx5CkPXGl2QDkKXo/w==";
const char kSalt[] = "6fB3zAWylW7UgMJbXV0Atw==";
const char kBookHash[] = "Kq4D8u8Bt1AWBLMlxSQSf3HfZrqP2l9CJp/fT/0Qz8Gj7EM1k1Dyy0MjvM8ZD1Xx2JmMRxGDEXWbTmNiIcrMKQ==";
const char kBookSalt[] = "s1lN2WkaQ5IuzJ3sNaj6kQ==";

// Two cell formats beyond the defaults: xf 2 is an unlocked input cell,
// xf 3 a locked cell whose formula is hidden.
QByteArray protectionStyles()
{
    QByteArray styles = ExcelWorkbook::defaultStyles();
    styles.replace("<cellXfs count=\"2\">", "<cellXfs count=\"4\">");
    styles.replace("</cellXfs>",
                   "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyProtection=\"1\">"
                   "<protection locked=\"0\"/></xf>"
                   "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyProtection=\"1\">"
                   "<protection hidden=\"1\"/></xf></cellXfs>");
    return styles;
}

ExcelWorkbook protectedBook()
{
    ExcelWorkbook book;
    book.stylesXml = protectionStyles();
    book.beforeBookViews = QByteArrayLiteral("<workbookProtection workbookAlgorithmName=\"SHA-512\" workbookHashValue=\"")
        + kBookHash + QByteArrayLiteral("\" workbookSaltValue=\"") + kBookSalt
        + QByteArrayLiteral("\" workbookSpinCount=\"100000\" lockStructure=\"1\"/>");
    ExcelSheet sheet;
    sheet.name = QStringLiteral("Rates");
    sheet.sheetData = QByteArrayLiteral(
        "<row r=\"1\"><c r=\"A1\" s=\"2\"><v>5</v></c><c r=\"B1\" s=\"3\"><f>A1*2</f><v>10</v></c>"
        "<c r=\"C1\"><v>7</v></c></row>");
    sheet.afterData = QByteArrayLiteral("<sheetProtection algorithmName=\"SHA-512\" hashValue=\"") + kHash
        + QByteArrayLiteral("\" saltValue=\"") + kSalt
        + QByteArrayLiteral("\" spinCount=\"100000\" sheet=\"1\" objects=\"1\" scenarios=\"1\""
                            " formatCells=\"0\" insertRows=\"0\" sort=\"0\"/>"
                            "<pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>");
    ExcelSheet open;
    open.name = QStringLiteral("Notes");
    open.sheetData = QByteArrayLiteral("<row r=\"1\"><c r=\"A1\"><v>1</v></c></row>");
    book.sheets = {sheet, open};
    return book;
}

// The <protection> of the xf a saved cell uses (empty map: none).
QHash<QString, QString> cellProtection(const Package &package, const QString &sheetPart, const QString &ref)
{
    QHash<QString, QString> out;
    const QindaOffice::Ooxml::XmlElementPtr sheet = package.xml(sheetPart);
    const XmlElement *data = sheet ? sheet->child(sml(), u"sheetData") : nullptr;
    QString style;
    for (const XmlElement *row : data != nullptr ? data->elements(sml(), u"row") : QList<const XmlElement *>()) {
        for (const XmlElement *c : row->elements(sml(), u"c")) {
            if (c->attr(u"r") == ref) {
                style = c->attr(u"s", QStringLiteral("0"));
            }
        }
    }
    const QindaOffice::Ooxml::XmlElementPtr styles = package.xml(QStringLiteral("xl/styles.xml"));
    const XmlElement *xfs = styles ? styles->child(sml(), u"cellXfs") : nullptr;
    if (xfs == nullptr || style.isEmpty()) {
        return out;
    }
    const QList<const XmlElement *> list = xfs->elements(sml(), u"xf");
    const int index = style.toInt();
    if (index < 0 || index >= list.size()) {
        return out;
    }
    if (const XmlElement *protection = list.at(index)->child(sml(), u"protection")) {
        for (const auto &attribute : protection->attributes) {
            out.insert(attribute.name, attribute.value);
        }
    }
    return out;
}

} // namespace

void CalcIoProtectTest::excelProtectionSurvivesSave()
{
    const QString source = path(QStringLiteral("protected.xlsx"));
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(protectedBook().build());
    }
    QQO_VERIFY_XLSX_SCHEMA(source);
    Workbook wb;
    QString error;
    QVERIFY2(IO::readXlsxFile(source, &wb, &error), qPrintable(error));
    const QString saved = path(QStringLiteral("protected-saved.xlsx"));
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    QindaOffice::Test::keepForInspection(saved);
    const auto package = Package::load(saved, &error);
    QVERIFY2(package, qPrintable(error));

    const QindaOffice::Ooxml::XmlElementPtr sheet = package->xml(QStringLiteral("xl/worksheets/sheet1.xml"));
    QVERIFY(sheet);
    const XmlElement *protection = sheet->child(sml(), u"sheetProtection");
    QVERIFY2(protection != nullptr, "the sheet lost its protection");
    QCOMPARE(protection->attr(u"algorithmName"), QStringLiteral("SHA-512"));
    QCOMPARE(protection->attr(u"hashValue"), QString::fromLatin1(kHash));
    QCOMPARE(protection->attr(u"saltValue"), QString::fromLatin1(kSalt));
    QCOMPARE(protection->attr(u"spinCount"), QStringLiteral("100000"));
    QCOMPARE(protection->attr(u"sheet"), QStringLiteral("1"));
    QCOMPARE(protection->attr(u"formatCells"), QStringLiteral("0"));
    QCOMPARE(protection->attr(u"insertRows"), QStringLiteral("0"));
    QCOMPARE(protection->attr(u"sort"), QStringLiteral("0"));
    // The unprotected sheet stays unprotected.
    const QindaOffice::Ooxml::XmlElementPtr notes = package->xml(QStringLiteral("xl/worksheets/sheet2.xml"));
    QVERIFY(notes && notes->child(sml(), u"sheetProtection") == nullptr);

    const QindaOffice::Ooxml::XmlElementPtr workbook = package->xml(QStringLiteral("xl/workbook.xml"));
    QVERIFY(workbook);
    const XmlElement *bookProtection = workbook->child(sml(), u"workbookProtection");
    QVERIFY2(bookProtection != nullptr, "the workbook lost its structure lock");
    QCOMPARE(bookProtection->attr(u"workbookAlgorithmName"), QStringLiteral("SHA-512"));
    QCOMPARE(bookProtection->attr(u"workbookHashValue"), QString::fromLatin1(kBookHash));
    QCOMPARE(bookProtection->attr(u"workbookSaltValue"), QString::fromLatin1(kBookSalt));
    QCOMPARE(bookProtection->attr(u"workbookSpinCount"), QStringLiteral("100000"));
    QCOMPARE(bookProtection->attr(u"lockStructure"), QStringLiteral("1"));

    // Cell formats keep their protection.
    const QString part = QStringLiteral("xl/worksheets/sheet1.xml");
    QCOMPARE(cellProtection(*package, part, QStringLiteral("A1")).value(QStringLiteral("locked")), QStringLiteral("0"));
    QCOMPARE(cellProtection(*package, part, QStringLiteral("B1")).value(QStringLiteral("hidden")), QStringLiteral("1"));
    QVERIFY(cellProtection(*package, part, QStringLiteral("C1")).value(QStringLiteral("locked"), QStringLiteral("1"))
            != QLatin1String("0"));
}

QTEST_GUILESS_MAIN(CalcIoProtectTest)
#include "tst_calc_io_protect.moc"
