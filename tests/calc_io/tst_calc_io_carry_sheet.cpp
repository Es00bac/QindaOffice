// SPDX-License-Identifier: GPL-3.0-or-later
// M66 (D-075, D-068 rule 2): the sheet-level parts QindaCalc does not
// model — a threaded comment and its reply with the workbook's persons, a
// form check box (control properties, <controls>, its VML shape and its
// drawing anchor) and printer settings behind <pageSetup r:id> — and a
// macro-enabled workbook's VBA project, kept (never run) when it is saved
// as .xlsm and left out, with the main part's content type to match, when
// it is saved as .xlsx.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_fixtures.h"
#include "carry_inspect.h"

#include <QFile>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using namespace QindaOffice::Test;
using QindaOffice::Ooxml::Package;
using QindaOffice::Ooxml::XmlElement;

class CalcIoCarrySheetTest : public QObject {
    Q_OBJECT
private slots:
    void threadedCommentsTravel();
    void controlsAndPrinterSettingsTravel();
    void macroWorkbookKeepsItsProject();
    void macroProjectStaysOutOfAnXlsx();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    bool open(const ExcelWorkbook &book, const QString &name, Workbook *wb)
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
        return IO::readXlsxFile(path(name), wb, &error);
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
        if (package) {
            const QStringList broken = brokenWiring(*package);
            if (!broken.isEmpty()) {
                qWarning("broken wiring: %s", qPrintable(broken.join(QLatin1String("; "))));
                return nullptr;
            }
        }
        return package;
    }
};

namespace {

const QString kWorkbook = QStringLiteral("xl/workbook.xml");
const QString kThreaded = msRel("2017/10/relationships/threadedComment");

} // namespace

void CalcIoCarrySheetTest::threadedCommentsTravel()
{
    Workbook wb;
    QVERIFY(open(sheetPartsBook(), QStringLiteral("threads.xlsx"), &wb));
    const auto package = save(wb, QStringLiteral("threads-saved.xlsx"));
    QVERIFY(package);
    const QString sheetPart = savedSheetPart(*package, QStringLiteral("Review"));
    const QStringList threads = partsByType(*package, sheetPart, kThreaded);
    QCOMPARE(threads.size(), 1);
    const QByteArray thread = package->data(threads.first());
    QVERIFY(thread.contains("Check this total"));
    QVERIFY(thread.contains("<text>Fixed</text>")); // the reply
    QVERIFY(thread.contains("ref=\"A1\""));
    // The people the thread names, from the workbook.
    const QStringList persons = partsByType(*package, kWorkbook, msRel("2017/10/relationships/person"));
    QCOMPARE(persons.size(), 1);
    QVERIFY(package->data(persons.first()).contains("displayName=\"Owner\""));
    // The legacy note that stands in for it is still written.
    QCOMPARE(partsByType(*package, sheetPart, officeRel("comments")).size(), 1);
}

void CalcIoCarrySheetTest::controlsAndPrinterSettingsTravel()
{
    Workbook wb;
    QVERIFY(open(sheetPartsBook(), QStringLiteral("controls.xlsx"), &wb));
    const auto package = save(wb, QStringLiteral("controls-saved.xlsx"));
    QVERIFY(package);
    const QString sheetPart = savedSheetPart(*package, QStringLiteral("Review"));
    const auto sheetXml = package->xml(sheetPart);
    QVERIFY(sheetXml);
    // <pageSetup r:id> names the printer settings, bytes untouched, and the
    // paper size the file asked for is still there.
    const XmlElement *setup = sheetXml->child(sml(), u"pageSetup");
    QVERIFY(setup != nullptr);
    QCOMPARE(setup->attr(u"paperSize"), QStringLiteral("9"));
    QCOMPARE(setup->attr(u"orientation"), QStringLiteral("landscape"));
    const QString printer = partForId(*package, sheetPart,
                                      setup->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id"));
    QCOMPARE(package->data(printer), QByteArrayLiteral("DEVMODE-A4-LANDSCAPE-OPAQUE"));
    // The check box: <control> names its properties part...
    const XmlElement *control = findLocal(*sheetXml, u"control");
    QVERIFY2(control != nullptr, "the <controls> element was lost");
    const QString shapeId = control->attr(u"shapeId");
    QVERIFY(shapeId.toInt() > 0);
    const QString props = partForId(*package, sheetPart,
                                    control->attribute(QString::fromUtf16(QindaOffice::Ooxml::Ns::R), u"id"));
    QVERIFY(package->data(props).contains("objectType=\"CheckBox\""));
    // ...its VML shape sits in the legacy drawing next to the note, under the
    // id the control names (moved into a block of its own, clear of the
    // block the written notes use)...
    const QStringList vml = partsByType(*package, sheetPart, officeRel("vmlDrawing"));
    QCOMPARE(vml.size(), 1);
    const QByteArray legacy = package->data(vml.first());
    const QByteArray spid = "_x0000_s" + shapeId.toUtf8();
    QVERIFY2(legacy.contains("id=\"" + spid + '"') && legacy.contains("ObjectType=\"Checkbox\""), legacy.constData());
    QVERIFY(legacy.contains("ObjectType=\"Note\""));
    QVERIFY(legacy.contains("data=\"1," + QByteArray::number(shapeId.toInt() / 1024) + '"'));
    // ...and its DrawingML anchor points at the same shape.
    const QStringList drawings = partsByType(*package, sheetPart, officeRel("drawing"));
    QCOMPARE(drawings.size(), 1);
    QVERIFY(package->data(drawings.first()).contains("spid=\"" + spid + '"'));
}

void CalcIoCarrySheetTest::macroWorkbookKeepsItsProject()
{
    Workbook wb;
    QVERIFY(open(macroBook(), QStringLiteral("macros.xlsm"), &wb));
    const auto package = save(wb, QStringLiteral("macros-saved.xlsm"));
    QVERIFY(package);
    QCOMPARE(package->contentType(kWorkbook), QStringLiteral("application/vnd.ms-excel.sheet.macroEnabled.main+xml"));
    const QStringList projects = partsByType(*package, kWorkbook, msRel("2006/relationships/vbaProject"));
    QCOMPARE(projects.size(), 1);
    QCOMPARE(package->data(projects.first()), QByteArrayLiteral("VBA-PROJECT-OPAQUE-BYTES\x01\x02"));
    QCOMPARE(package->contentType(projects.first()), QStringLiteral("application/vnd.ms-office.vbaProject"));
    const QStringList signatures =
        partsByType(*package, projects.first(), msRel("2006/relationships/vbaProjectSignature"));
    QCOMPARE(signatures.size(), 1);
    QCOMPARE(package->data(signatures.first()), QByteArrayLiteral("SIGNATURE-OPAQUE"));
    // The code names the project binds its modules to.
    QCOMPARE(package->xml(kWorkbook)->child(sml(), u"workbookPr")->attr(u"codeName"), QStringLiteral("ThisWorkbook"));
    const auto sheetXml = package->xml(savedSheetPart(*package, QStringLiteral("Macros")));
    QVERIFY(sheetXml && sheetXml->child(sml(), u"sheetPr") != nullptr);
    QCOMPARE(sheetXml->child(sml(), u"sheetPr")->attr(u"codeName"), QStringLiteral("Sheet1"));
}

void CalcIoCarrySheetTest::macroProjectStaysOutOfAnXlsx()
{
    Workbook wb;
    QVERIFY(open(macroBook(), QStringLiteral("macros2.xlsm"), &wb));
    const auto package = save(wb, QStringLiteral("macros-as.xlsx"));
    QVERIFY(package);
    // Excel refuses an .xlsx whose main part is macro-enabled, or that
    // carries a VBA project: neither is written.
    QCOMPARE(package->contentType(kWorkbook), smlType("sheet.main+xml"));
    for (const QString &part : package->partNames()) {
        QVERIFY2(!part.contains(QLatin1String("vbaProject")), qPrintable(part));
    }
}

QTEST_GUILESS_MAIN(CalcIoCarrySheetTest)
#include "tst_calc_io_carry_sheet.moc"
