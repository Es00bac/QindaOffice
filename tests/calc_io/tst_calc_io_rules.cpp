// SPDX-License-Identifier: GPL-3.0-or-later
// calc_io rule gates (M34): conditional formats (cellIs + dxf, expression,
// data bar), data validations, the autofilter and cell comments round-trip
// through xlsx — via the QXlsx model where it has one and the post-pass
// where it does not.

#include "xlsx_bridge.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <KArchiveFile>
#include <KZip>

#include <QtTest>

using namespace QindaOffice::Calc;

class CalcIoRulesTest : public QObject {
    Q_OBJECT
private slots:
    void condFormatsRoundTrip();
    void expressionAndDataBarSurvive();
    void validationsRoundTrip();
    void autoFilterRoundTrip();
    void commentsRoundTrip();
};

static QString readPart(const QString &path, const QString &part)
{
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly)) {
        return {};
    }
    const KArchiveFile *file = zip.directory()->file(part);
    return file != nullptr ? QString::fromUtf8(file->data()) : QString();
}

void CalcIoRulesTest::condFormatsRoundTrip()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    wb.setCellInput(0, {0, 0}, QStringLiteral("5"));
    wb.setCellInput(0, {1, 0}, QStringLiteral("15"));
    CondFormatRule rule;
    rule.op = CondFormatRule::Op::GreaterThan;
    rule.first = 10;
    rule.range = {{0, 0}, {1, 0}};
    rule.format.background = Rgb{0xFF, 0xC7, 0xCE};
    rule.format.bold = true;
    rule.stopIfTrue = true;
    wb.sheetAt(0).setCondFormats({rule});

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/cf.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));
    const QString sheetXml = readPart(path, QStringLiteral("xl/worksheets/sheet1.xml"));
    QVERIFY(sheetXml.contains(QStringLiteral("conditionalFormatting")));
    QVERIFY(sheetXml.contains(QStringLiteral("cellIs")));
    QVERIFY(sheetXml.contains(QStringLiteral("greaterThan")));
    const QString stylesXml = readPart(path, QStringLiteral("xl/styles.xml"));
    QVERIFY(stylesXml.contains(QStringLiteral("dxfs")));
    // The dxf fill is the file's bgColor (Excel's dxf convention).
    QVERIFY2(stylesXml.contains(QStringLiteral("bgColor rgb=\"FFFFC7CE\"")), qPrintable(stylesXml));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    const QList<CondFormatRule> rules = back.sheetAt(0).condFormats();
    QCOMPARE(rules.size(), 1);
    QCOMPARE(rules.at(0).op, CondFormatRule::Op::GreaterThan);
    QCOMPARE(rules.at(0).first, 10.0);
    QVERIFY(rules.at(0).stopIfTrue);
    QVERIFY(rules.at(0).format.background.has_value());
    QCOMPARE(int(rules.at(0).format.background->r), 0xFF);
    QCOMPARE(int(rules.at(0).format.background->g), 0xC7);
    // And the rule evaluates against live values after the round trip.
    QVERIFY(!back.condFormatFor(0, {0, 0}).background.has_value());
    QVERIFY(back.condFormatFor(0, {1, 0}).background.has_value());
}

void CalcIoRulesTest::expressionAndDataBarSurvive()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    for (int i = 0; i < 4; ++i) {
        wb.setCellInput(0, {i, 0}, QString::number((i + 1) * 10));
    }
    CondFormatRule expression;
    expression.op = CondFormatRule::Op::Expression;
    expression.range = {{0, 0}, {3, 0}};
    expression.formula = QStringLiteral("$A1>=20");
    QVERIFY(prepareCondRule(expression));
    expression.format.bold = true;
    CondFormatRule bar;
    bar.op = CondFormatRule::Op::DataBar;
    bar.range = {{0, 0}, {3, 0}};
    bar.barColor = Rgb{0x08, 0x7F, 0x8C};
    wb.sheetAt(0).setCondFormats({expression, bar});

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/expr.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    const QList<CondFormatRule> rules = back.sheetAt(0).condFormats();
    QCOMPARE(rules.size(), 2);
    const auto exprIt = std::find_if(rules.begin(), rules.end(), [](const CondFormatRule &rule) {
        return rule.op == CondFormatRule::Op::Expression;
    });
    QVERIFY(exprIt != rules.end());
    QCOMPARE(exprIt->formula, QStringLiteral("$A1>=20"));
    QVERIFY(exprIt->ast != nullptr);
    QVERIFY(!back.condFormatFor(0, {0, 0}).bold.has_value());
    QVERIFY(back.condFormatFor(0, {2, 0}).bold.value_or(false));
    const auto barIt = std::find_if(rules.begin(), rules.end(), [](const CondFormatRule &rule) {
        return rule.op == CondFormatRule::Op::DataBar;
    });
    QVERIFY(barIt != rules.end());
    QCOMPARE(int(barIt->barColor.r), 0x08);
    QCOMPARE(int(barIt->barColor.g), 0x7F);
    const auto fraction = back.condFormatFor(0, {3, 0}).dataBar;
    QVERIFY(fraction.has_value());
    QCOMPARE(fraction->fraction, 1.0);
}

void CalcIoRulesTest::validationsRoundTrip()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    ValidationRule list;
    list.kind = ValidationRule::Kind::List;
    list.range = {{0, 0}, {9, 0}};
    list.list = {QStringLiteral("Downside"), QStringLiteral("Base"), QStringLiteral("Upside")};
    list.message = QStringLiteral("Pick a scenario");
    ValidationRule whole;
    whole.kind = ValidationRule::Kind::WholeNumber;
    whole.range = {{0, 1}, {9, 1}};
    whole.min = 1;
    whole.max = 10;
    wb.sheetAt(0).setValidations({list, whole});

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/dv.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));
    const QString sheetXml = readPart(path, QStringLiteral("xl/worksheets/sheet1.xml"));
    QVERIFY(sheetXml.contains(QStringLiteral("dataValidation")));
    QVERIFY(sheetXml.contains(QStringLiteral("Downside,Base,Upside")));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    const QList<ValidationRule> rules = back.sheetAt(0).validations();
    QCOMPARE(rules.size(), 2);
    const ValidationRule *readList = back.sheetAt(0).validationAt({5, 0});
    QVERIFY(readList != nullptr);
    QCOMPARE(readList->kind, ValidationRule::Kind::List);
    QCOMPARE(readList->list, QStringList({QStringLiteral("Downside"), QStringLiteral("Base"),
                                          QStringLiteral("Upside")}));
    QCOMPARE(readList->message, QStringLiteral("Pick a scenario"));
    QVERIFY(validationAccepts(*readList, QStringLiteral("Base")));
    QVERIFY(!validationAccepts(*readList, QStringLiteral("Bogus")));
    const ValidationRule *readWhole = back.sheetAt(0).validationAt({5, 1});
    QVERIFY(readWhole != nullptr);
    QCOMPARE(readWhole->kind, ValidationRule::Kind::WholeNumber);
    QCOMPARE(readWhole->min, 1.0);
    QCOMPARE(readWhole->max, 10.0);
}

void CalcIoRulesTest::autoFilterRoundTrip()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    for (int i = 0; i < 4; ++i) {
        wb.setCellInput(0, {i, 0}, QStringLiteral("h%1").arg(i));
        wb.setCellInput(0, {i, 1}, QString::number(i));
    }
    AutoFilter filter;
    filter.range = {{0, 0}, {3, 1}};
    filter.allowed.insert(1, QStringList{QStringLiteral("1"), QStringLiteral("2")});
    filter.active = true;
    wb.sheetAt(0).setAutoFilter(filter);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/af.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));
    const QString sheetXml = readPart(path, QStringLiteral("xl/worksheets/sheet1.xml"));
    QVERIFY(sheetXml.contains(QStringLiteral("<autoFilter ref=\"A1:B4\">")));
    QVERIFY(sheetXml.contains(QStringLiteral("filterColumn colId=\"1\"")));
    // Schema order: the element lands before mergeCells/conditionalFormatting.
    QVERIFY(sheetXml.indexOf(QStringLiteral("</sheetData>")) < sheetXml.indexOf(QStringLiteral("<autoFilter")));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    const AutoFilter &read = back.sheetAt(0).autoFilter();
    QVERIFY(read.active);
    QCOMPARE(read.range.topLeft.row, 0);
    QCOMPARE(read.range.bottomRight.row, 3);
    QCOMPARE(read.allowed.value(1), QStringList({QStringLiteral("1"), QStringLiteral("2")}));
    // And the filter still hides rows: body row 3 (value 3) is out, row 1 stays.
    QVERIFY(back.sheetAt(0).filteredOutRows().contains(3));
    QVERIFY(!back.sheetAt(0).filteredOutRows().contains(1));
}

void CalcIoRulesTest::commentsRoundTrip()
{
    Workbook wb;
    wb.addSheet(QStringLiteral("S"));
    wb.setCellInput(0, {0, 0}, QStringLiteral("1"));
    CellComment first{QStringLiteral("Ada"), QStringLiteral("Check this number")};
    CellComment second{QStringLiteral("Bob"), QStringLiteral("Looks fine")};
    wb.sheetAt(0).setCommentAt({0, 0}, first);
    wb.sheetAt(0).setCommentAt({1, 1}, second);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/cm.xlsx");
    QString error;
    QVERIFY2(IO::writeXlsxFile(wb, path, &error), qPrintable(error));
    const QString part = readPart(path, QStringLiteral("xl/comments1.xml"));
    QVERIFY(part.contains(QStringLiteral("Ada")));
    QVERIFY(part.contains(QStringLiteral("Check this number")));
    QVERIFY(part.contains(QStringLiteral("ref=\"A1\"")));
    const QString contentTypes = readPart(path, QStringLiteral("[Content_Types].xml"));
    QVERIFY(contentTypes.contains(QStringLiteral("comments1.xml")));
    const QString rels = readPart(path, QStringLiteral("xl/worksheets/_rels/sheet1.xml.rels"));
    QVERIFY(rels.contains(QStringLiteral("comments")));

    Workbook back;
    QVERIFY2(IO::readXlsxFile(path, &back, &error), qPrintable(error));
    const CellComment *readFirst = back.sheetAt(0).commentAt({0, 0});
    QVERIFY(readFirst != nullptr);
    QCOMPARE(readFirst->author, QStringLiteral("Ada"));
    QCOMPARE(readFirst->text, QStringLiteral("Check this number"));
    const CellComment *readSecond = back.sheetAt(0).commentAt({1, 1});
    QVERIFY(readSecond != nullptr);
    QCOMPARE(readSecond->text, QStringLiteral("Looks fine"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    CalcIoRulesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_io_rules.moc"
