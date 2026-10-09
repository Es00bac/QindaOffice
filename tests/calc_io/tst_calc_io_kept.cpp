// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087) conditional formats and validations through the file: every
// kind the model holds round-trips equal and is written in Excel's markup
// (the formulas Excel stores for each kind, x14 twins for data bars and
// Excel 2010's icon sets); what it cannot hold — a formula the engine does
// not parse, custom icons, an input mode — survives byte for byte with its
// dxf at the same dxfId, follows inserted rows (only its sqref changes)
// and keeps its priority until the precedence itself changes. Every source
// and saved package passes the schema gate.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "kept_fixtures.h"

#include "structure.h"
#include "workbook.h"

#include <KArchiveFile>
#include <KZip>

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <QtTest>

using namespace QindaOffice::Calc;
using QindaOffice::Test::ExcelWorkbook;
namespace Kept = QindaOffice::Test::Kept;
using RuleOp = CondFormatRule::Op;

namespace {

QString readPart(const QString &path, const QString &part)
{
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly)) {
        return {};
    }
    const KArchiveFile *file = zip.directory()->file(part);
    return file != nullptr ? QString::fromUtf8(file->data()) : QString();
}

QString sheet1(const QString &path)
{
    return readPart(path, QStringLiteral("xl/worksheets/sheet1.xml"));
}

QString dxfsOf(const QString &path)
{
    const QString styles = readPart(path, QStringLiteral("xl/styles.xml"));
    const qsizetype open = styles.indexOf(QLatin1String("<dxfs"));
    const qsizetype close = styles.indexOf(QLatin1String("</dxfs>"), open);
    return open < 0 || close < 0 ? QString() : styles.mid(open, close + 7 - open);
}

// A rule without what the file adds to it (priority, dxf, twin id) and
// without its parse trees, for comparing what it means.
CondFormatRule meaning(CondFormatRule rule)
{
    rule.priority = 0;
    rule.dxfId = -1;
    rule.x14Id.clear();
    rule.ast.reset();
    rule.ast2.reset();
    return rule;
}

ValidationRule meaning(ValidationRule rule)
{
    rule.ast1.reset();
    rule.ast2.reset();
    if (!rule.listSource.isEmpty()) {
        rule.list.clear(); // read from the source's cells
    }
    return rule;
}

int countOf(const QString &text, const QString &needle)
{
    return int(text.count(needle));
}

} // namespace

class CalcIoKeptTest : public QObject {
    Q_OBJECT
private slots:
    void unmodelledRulesSurviveByteIdentical();
    void keptRulesFollowAnInsertedRow();
    void prioritiesKeptOrRenumbered();
    void changedFormatsGetNewDxfs();
    void excelDataBarTwinRoundTrips();
    void everyModelledKindRoundTrips();
    void validationsOfEveryKindRoundTrip();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    QString writeSource(const QString &name, const ExcelWorkbook &book) const
    {
        QFile file(path(name));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(book.build());
        }
        return path(name);
    }
    bool load(const QString &name, const ExcelWorkbook &book, Workbook *wb, QStringList *notes = nullptr) const
    {
        QString error;
        const QString source = writeSource(name, book);
        if (!QindaOffice::Test::xlsxSchemaErrors(source).isEmpty()) {
            qWarning("source fixture %s is not schema-valid", qPrintable(name));
            return false;
        }
        return IO::readXlsxFile(source, wb, &error, notes);
    }
};

void CalcIoKeptTest::unmodelledRulesSurviveByteIdentical()
{
    Workbook wb;
    QStringList notes;
    QVERIFY(load(QStringLiteral("mixed.xlsx"), Kept::mixedBook(), &wb, &notes));
    const Sheet &data = wb.sheetAt(0);
    QCOMPARE(data.condFormats().size(), 3); // A: > 5, C: < 0, D: 3Arrows
    QCOMPARE(data.condFormats().at(0).op, RuleOp::GreaterThan);
    QCOMPARE(data.condFormats().at(1).op, RuleOp::LessThan);
    QCOMPARE(data.condFormats().at(2).op, RuleOp::IconSet);
    QCOMPARE(data.condFormats().at(2).iconSet, QStringLiteral("3Arrows"));
    QCOMPARE(data.validations().size(), 2); // F decimal, H x14 list
    QCOMPARE(data.keptRules().size(), 5);   // mixed rule, J block, x14 icons, two validations
    QVERIFY(std::any_of(notes.begin(), notes.end(), [](const QString &note) { return note.contains(QLatin1String("keeps")); }));
    // The x14 list resolves its items from the other sheet.
    const ValidationRule *list = data.validationAt({0, 7});
    QVERIFY(list != nullptr && list->x14);
    QCOMPARE(list->list, (QStringList{QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")}));

    const QString saved = path(QStringLiteral("mixed-saved.xlsx"));
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    const QString xml = sheet1(saved);
    QVERIFY2(xml.contains(QString::fromUtf8(Kept::kKeptExpression)), qPrintable(xml));
    QVERIFY(xml.contains(QString::fromUtf8(Kept::kKeptBlock)));
    QVERIFY(xml.contains(QString::fromUtf8(Kept::keptX14Block())));
    QVERIFY(xml.contains(QString::fromUtf8(Kept::kKeptValidation)));
    QVERIFY(xml.contains(QString::fromUtf8(Kept::kKeptX14Validation)));
    QCOMPARE(countOf(xml, QStringLiteral("SUM(J1:J3 J2:K2)")), 1); // never twice
    // The dxf table: the source's three, unchanged, at their own indexes.
    QCOMPARE(dxfsOf(saved), QString::fromUtf8(Kept::dxfs()));
    // The modelled rules keep their priorities and dxfIds.
    QVERIFY(xml.contains(QStringLiteral("<cfRule type=\"cellIs\" dxfId=\"0\" priority=\"1\" operator=\"greaterThan\">")));
    QVERIFY(xml.contains(QStringLiteral("<cfRule type=\"cellIs\" dxfId=\"2\" priority=\"3\" operator=\"lessThan\">")));

    // A second round trip gives the same kept text again.
    Workbook back;
    QVERIFY2(IO::readXlsxFile(saved, &back, &error), qPrintable(error));
    QCOMPARE(back.sheetAt(0).keptRules().size(), 5);
    for (qsizetype i = 0; i < data.keptRules().size(); ++i) {
        const KeptRule &first = data.keptRules().at(i);
        const auto same = std::find_if(back.sheetAt(0).keptRules().begin(), back.sheetAt(0).keptRules().end(),
                                       [&first](const KeptRule &rule) { return rule.xml == first.xml; });
        QVERIFY2(same != back.sheetAt(0).keptRules().end(), qPrintable(first.xml));
    }
    QCOMPARE(back.sheetAt(0).condFormats().size(), 3);
}

void CalcIoKeptTest::keptRulesFollowAnInsertedRow()
{
    Workbook wb;
    QVERIFY(load(QStringLiteral("moved.xlsx"), Kept::mixedBook(), &wb));
    applyStructureEdit(wb, {StructureOp::InsertRows, 0, 0, 2});
    const QString saved = path(QStringLiteral("moved-saved.xlsx"));
    QString error;
    QStringList warnings;
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error, &warnings), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    const QString xml = sheet1(saved);
    // Only the sqref moved; the rules themselves are as Excel wrote them.
    QVERIFY2(xml.contains(QStringLiteral("<conditionalFormatting sqref=\"A3:A12\">") + QString::fromUtf8(Kept::kKeptExpression)),
             qPrintable(xml));
    QString block = QString::fromUtf8(Kept::kKeptBlock);
    block.replace(QStringLiteral("J1:J10"), QStringLiteral("J3:J12"));
    QVERIFY(xml.contains(block));
    QString icons = QString::fromUtf8(Kept::keptX14Block());
    icons.replace(QStringLiteral("G1:G10"), QStringLiteral("G3:G12"));
    QVERIFY(xml.contains(icons));
    QString validation = QString::fromUtf8(Kept::kKeptValidation);
    validation.replace(QStringLiteral("E1:E5"), QStringLiteral("E3:E7"));
    QVERIFY(xml.contains(validation));
    // Their formulas did not move, and the save says so.
    QVERIFY(std::any_of(warnings.begin(), warnings.end(),
                        [](const QString &note) { return note.contains(QLatin1String("still name the cells")); }));
    // The modelled rules moved with their cells.
    QVERIFY(xml.contains(QStringLiteral("<conditionalFormatting sqref=\"C3:C12\">")));
}

void CalcIoKeptTest::prioritiesKeptOrRenumbered()
{
    Workbook wb;
    QVERIFY(load(QStringLiteral("priorities.xlsx"), Kept::mixedBook(), &wb));
    // A new rule at the top of the precedence: every priority renumbers.
    CondFormatRule top;
    top.op = RuleOp::Top10;
    top.rank = 2;
    top.range = {{0, 0}, {9, 0}};
    top.format.bold = true;
    QList<CondFormatRule> rules = wb.sheetAt(0).condFormats();
    rules.prepend(top);
    wb.sheetAt(0).setCondFormats(rules);
    const QString saved = path(QStringLiteral("priorities-saved.xlsx"));
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    const QString xml = sheet1(saved);
    // New 1, > 5 2, kept expression 3, < 0 4, x14 icons 5, 3Arrows 6, J block 7.
    QString expression = QString::fromUtf8(Kept::kKeptExpression);
    expression.replace(QStringLiteral("priority=\"2\""), QStringLiteral("priority=\"3\""));
    QVERIFY2(xml.contains(expression), qPrintable(xml));
    QString block = QString::fromUtf8(Kept::kKeptBlock);
    block.replace(QStringLiteral("priority=\"6\""), QStringLiteral("priority=\"7\""));
    QVERIFY(xml.contains(block));
    QString icons = QString::fromUtf8(Kept::keptX14Block());
    icons.replace(QStringLiteral("priority=\"4\""), QStringLiteral("priority=\"5\""));
    QVERIFY(xml.contains(icons));
    Workbook back;
    QVERIFY2(IO::readXlsxFile(saved, &back, &error), qPrintable(error));
    QList<int> priorities;
    for (const CondFormatRule &rule : back.sheetAt(0).condFormats()) {
        priorities.append(rule.priority);
    }
    QCOMPARE(priorities, (QList<int>{1, 2, 4, 6}));
    QCOMPARE(back.sheetAt(0).condFormats().first().op, RuleOp::Top10);
}

void CalcIoKeptTest::changedFormatsGetNewDxfs()
{
    Workbook wb;
    QVERIFY(load(QStringLiteral("formats.xlsx"), Kept::mixedBook(), &wb));
    QList<CondFormatRule> rules = wb.sheetAt(0).condFormats();
    rules[1].format.foreground = Rgb{0x12, 0x34, 0x56}; // the < 0 rule, dxf 2
    wb.sheetAt(0).setCondFormats(rules);
    const QString saved = path(QStringLiteral("formats-saved.xlsx"));
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    const QString dxfs = dxfsOf(saved);
    QVERIFY2(dxfs.startsWith(QStringLiteral("<dxfs count=\"4\">")), qPrintable(dxfs));
    QString source = QString::fromUtf8(Kept::dxfs());
    source.remove(QStringLiteral("<dxfs count=\"3\">")).remove(QStringLiteral("</dxfs>"));
    QVERIFY(dxfs.contains(source)); // the three as they were, first
    const QString added = dxfs.mid(dxfs.lastIndexOf(QStringLiteral("<dxf>")));
    QVERIFY2(added.contains(QStringLiteral("FF123456")), qPrintable(added));
    QVERIFY(added.contains(QStringLiteral("numFmtId=\"164\"")));  // what the model does not hold stays
    QVERIFY(added.contains(QStringLiteral("<border>")));
    QVERIFY(sheet1(saved).contains(QStringLiteral("<cfRule type=\"cellIs\" dxfId=\"3\" priority=\"3\" operator=\"lessThan\">")));
    Workbook back;
    QVERIFY2(IO::readXlsxFile(saved, &back, &error), qPrintable(error));
    QVERIFY(back.sheetAt(0).condFormats().at(1).format.foreground == (Rgb{0x12, 0x34, 0x56}));
    QVERIFY(back.sheetAt(0).condFormats().at(1).format.background == wb.sheetAt(0).condFormats().at(1).format.background);
}

void CalcIoKeptTest::excelDataBarTwinRoundTrips()
{
    Workbook wb;
    QVERIFY(load(QStringLiteral("bars.xlsx"), Kept::dataBarBook(), &wb));
    QCOMPARE(wb.sheetAt(0).keptRules().size(), 0);
    QCOMPARE(wb.sheetAt(0).condFormats().size(), 1);
    const CondFormatRule bar = wb.sheetAt(0).condFormats().first();
    QCOMPARE(bar.op, RuleOp::DataBar);
    QVERIFY(bar.bar.x14);
    QCOMPARE(bar.bar.minLength, 0);
    QCOMPARE(bar.bar.maxLength, 100);
    QVERIFY(!bar.bar.gradient);
    QCOMPARE(bar.bar.axis, DataBarStyle::Axis::Middle);
    QVERIFY(bar.bar.border == (Rgb{0x63, 0x8E, 0xC6}));
    QVERIFY(bar.bar.negativeBorder == (Rgb{0xFF, 0, 0}));
    QCOMPARE(bar.thresholds.first().type, CfThreshold::Type::AutoMin);
    QCOMPARE(bar.x14Id, QStringLiteral("{11111111-2222-4333-8444-555555555555}"));
    // Drawn as Excel 2010 draws it: the middle axis, the negative bar left of it.
    QCOMPARE(wb.condFormatFor(0, {3, 1}).dataBar->origin, 0.5);
    QVERIFY(wb.condFormatFor(0, {0, 1}).dataBar->negative);

    const QString saved = path(QStringLiteral("bars-saved.xlsx"));
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    const QString xml = sheet1(saved);
    QCOMPARE(countOf(xml, QStringLiteral("{11111111-2222-4333-8444-555555555555}")), 2); // the 2007 rule names its twin
    QVERIFY(xml.contains(QStringLiteral("<x14:dataBar minLength=\"0\" maxLength=\"100\" border=\"1\" gradient=\"0\"")));
    QVERIFY(xml.contains(QStringLiteral("axisPosition=\"middle\"")));
    QVERIFY(xml.contains(QStringLiteral("<x14:cfvo type=\"autoMin\"/>")));
    QVERIFY(xml.contains(QStringLiteral("<cfvo type=\"min\"/>"))); // 2007 has no automatic minimum
    Workbook back;
    QVERIFY2(IO::readXlsxFile(saved, &back, &error), qPrintable(error));
    QCOMPARE(back.sheetAt(0).condFormats().size(), 1);
    QVERIFY(meaning(back.sheetAt(0).condFormats().first()) == meaning(bar));
}

void CalcIoKeptTest::everyModelledKindRoundTrips()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("All"));
    for (int r = 0; r < 6; ++r) {
        wb.setCellInput(0, {r, 0}, QString::number(r * 10));
    }
    QList<CondFormatRule> rules = Kept::everyKind();
    for (CondFormatRule &rule : rules) {
        QVERIFY(prepareCondRule(rule));
    }
    wb.sheetAt(0).setCondFormats(rules);

    const QString saved = path(QStringLiteral("kinds.xlsx"));
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    const QString xml = sheet1(saved);
    for (const QString &markup : {QStringLiteral("operator=\"notBetween\"><formula>5</formula><formula>25</formula>"),
                                  QStringLiteral("operator=\"equal\"><formula>\"Yes\"</formula>"),
                                  QStringLiteral("<formula>$C$1</formula>"),
                                  QStringLiteral("<formula>MOD($A1,20)=0</formula>"),
                                  QStringLiteral("text=\"a&quot;b\"><formula>NOT(ISERROR(SEARCH(\"a\"\"b\",A1)))</formula>"),
                                  QStringLiteral("operator=\"notContains\" text=\"x\"><formula>ISERROR(SEARCH(\"x\",A1))</formula>"),
                                  QStringLiteral("<formula>LEFT(A1,LEN(\"Q\"))=\"Q\"</formula>"),
                                  QStringLiteral("<formula>RIGHT(A1,LEN(\"z\"))=\"z\"</formula>"),
                                  QStringLiteral("<formula>LEN(TRIM(A1))=0</formula>"),
                                  QStringLiteral("<formula>LEN(TRIM(A1))&gt;0</formula>"),
                                  QStringLiteral("<formula>ISERROR(A1)</formula>"),
                                  QStringLiteral("<formula>NOT(ISERROR(A1))</formula>"),
                                  QStringLiteral("percent=\"1\" bottom=\"1\" rank=\"3\"/>"),
                                  QStringLiteral("aboveAverage=\"0\" equalAverage=\"1\" stdDev=\"1\"/>"),
                                  QStringLiteral("<conditionalFormatting sqref=\"A1:A6 C1:C6\">"),
                                  QStringLiteral("type=\"uniqueValues\""),
                                  QStringLiteral("timePeriod=\"lastWeek\"><formula>AND(TODAY()-ROUNDDOWN(A1,0)&gt;=(WEEKDAY(TODAY()))"),
                                  QStringLiteral("<cfvo type=\"percentile\" val=\"50\"/><cfvo type=\"num\" val=\"100\"/>"),
                                  QStringLiteral("<dataBar showValue=\"0\"><cfvo type=\"min\"/><cfvo type=\"max\"/><color rgb=\"FF5A8AC6\"/>"),
                                  QStringLiteral("direction=\"rightToLeft\" negativeBarColorSameAsPositive=\"1\""),
                                  QStringLiteral("<x14:cfvo type=\"formula\"><xm:f>$C$2</xm:f></x14:cfvo>"),
                                  QStringLiteral("<iconSet iconSet=\"4Rating\" reverse=\"1\"><cfvo type=\"percent\" val=\"0\"/>"
                                                 "<cfvo type=\"percent\" val=\"25\" gte=\"0\"/>"),
                                  QStringLiteral("<x14:iconSet iconSet=\"3Stars\" showValue=\"0\">")}) {
        QVERIFY2(xml.contains(markup), qPrintable(markup));
    }
    QVERIFY(!xml.contains(QStringLiteral("<iconSet iconSet=\"3Stars\""))); // Excel 2007 has no stars
    Workbook back;
    QVERIFY2(IO::readXlsxFile(saved, &back, &error), qPrintable(error));
    QCOMPARE(back.sheetAt(0).keptRules().size(), 0);
    const QList<CondFormatRule> &read = back.sheetAt(0).condFormats();
    QCOMPARE(read.size(), rules.size());
    for (qsizetype i = 0; i < rules.size(); ++i) {
        QVERIFY2(meaning(read.at(i)) == meaning(rules.at(i)), qPrintable(QStringLiteral("rule %1").arg(i)));
        QCOMPARE(read.at(i).priority, int(i + 1));
    }
}

void CalcIoKeptTest::validationsOfEveryKindRoundTrip()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("Form"));
    wb.addSheet(QStringLiteral("Lists"));
    wb.setCellInput(1, {0, 0}, QStringLiteral("North"));
    wb.setCellInput(1, {1, 0}, QStringLiteral("South"));
    QList<ValidationRule> rules = Kept::everyValidation();
    for (ValidationRule &rule : rules) {
        QVERIFY(prepareValidationRule(rule));
    }
    wb.sheetAt(0).setValidations(rules);

    const QString saved = path(QStringLiteral("validations.xlsx"));
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, saved, &error), qPrintable(error));
    QQO_VERIFY_XLSX_SCHEMA(saved);
    const QString xml = sheet1(saved);
    for (const QString &markup : {QStringLiteral("<dataValidations count=\"9\">"),
                                  QStringLiteral("type=\"decimal\" errorStyle=\"warning\" operator=\"notBetween\""),
                                  QStringLiteral("errorTitle=\"Careful\" error=\"Outside 0.5–1.5 &quot;please&quot;\""),
                                  QStringLiteral("type=\"date\" operator=\"greaterThan\""),
                                  QStringLiteral("<formula1>TODAY()</formula1>"),
                                  QStringLiteral("type=\"time\" errorStyle=\"information\""),
                                  QStringLiteral("<formula1>0.375</formula1><formula2>0.75</formula2>"),
                                  QStringLiteral("operator=\"lessThanOrEqual\""),
                                  QStringLiteral("showDropDown=\"1\""),
                                  QStringLiteral("<formula1>\"Low,High\"</formula1>"),
                                  QStringLiteral("<formula1>ISNUMBER(G1)</formula1>"),
                                  QStringLiteral("<dataValidation allowBlank=\"1\" showInputMessage=\"1\" promptTitle=\"Note\" "
                                                 "prompt=\"Anything goes\" sqref=\"H1:H10\">"),
                                  QStringLiteral("<formula1>LEN(TRIM(I1))&gt;0</formula1>"),
                                  QStringLiteral("<x14:formula1><xm:f>Lists!$A$1:$A$2</xm:f></x14:formula1>"
                                                 "<xm:sqref>J1:J10 L1:L10</xm:sqref>")}) {
        QVERIFY2(xml.contains(markup), qPrintable(markup));
    }
    Workbook back;
    QVERIFY2(IO::readXlsxFile(saved, &back, &error), qPrintable(error));
    QCOMPARE(back.sheetAt(0).keptRules().size(), 0);
    const QList<ValidationRule> &read = back.sheetAt(0).validations();
    QCOMPARE(read.size(), rules.size());
    for (const ValidationRule &rule : rules) {
        const auto found = std::find_if(read.begin(), read.end(),
                                        [&rule](const ValidationRule &candidate) { return candidate.range == rule.range; });
        QVERIFY(found != read.end());
        QVERIFY2(meaning(*found) == meaning(rule), qPrintable(rule.range.topLeft.col == 0 ? QStringLiteral("A")
                                                                                       : QString::number(rule.range.topLeft.col)));
    }
    const ValidationRule *regions = back.sheetAt(0).validationAt({4, 11});
    QVERIFY(regions != nullptr);
    QCOMPARE(regions->list, (QStringList{QStringLiteral("North"), QStringLiteral("South")}));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcIoKeptTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_io_kept.moc"
