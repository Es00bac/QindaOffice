// SPDX-License-Identifier: GPL-3.0-or-later
// M82 (D-091): charts as PowerPoint makes them past M67's limits, over the
// fixture deck of pptx_fixture_slides.h whose chart is replaced by a
// PowerPoint combo chart — clustered columns (North with a red second
// point and data labels, one of them its own; South) and a line (Total) on
// the same axes, over two-level categories (East ▸ Q1, Q2; West ▸ Q1, Q2)
// — with the workbook PowerPoint embeds. Both plots read; an edit of the
// data reaches both (an added series joins the line, an added category
// the West group) in the formulas, the caches and the workbook, whose
// outer level column keeps its groups; a deleted category takes its
// point's colour and label with it and leaves the next one its group's
// label; a kind change, in a combo and in a plain pie, keeps per-point
// colours and data labels (a label position the new kind cannot take is
// dropped). Every package and workbook validates against ECMA-376, reads
// back equal, the parts the edit does not touch are byte for byte, and
// LibreOffice renders the edited deck (skipped without it).

#include "chart_edit.h"
#include "pptx_fixture_combo.h"
#include "pptx_reader.h"
#include "pptx_writer.h"
#include "presentation.h"

#include "ooxml_package.h"
#include "ooxml_xml.h"
#include "support/ooxml_schema.h"
#include "support/soffice_env.h"

#include <workbook.h>
#include <xlsx_bridge.h>

#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace QindaOffice::Show;
namespace Ooxml = QindaOffice::Ooxml;
namespace Calc = QindaOffice::Calc;

namespace {

using PptxComboFixture::comboChart;
using PptxComboFixture::deckWith;
using PptxComboFixture::kRel;
using PptxComboFixture::pieChart;

const QColor kRed(0xFF, 0x00, 0x00);

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The workbook PowerPoint embeds for `rows` (the header row first), with
// its Excel table over them.
QByteArray workbookOf(const QString &dir, const QList<QStringList> &rows)
{
    Calc::Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    for (int r = 0; r < rows.size(); ++r) {
        for (int c = 0; c < rows.at(r).size(); ++c) {
            bool number = false;
            const double value = rows.at(r).at(c).toDouble(&number);
            if (!rows.at(r).at(c).isEmpty()) {
                book.setCellValue(0, {r, c}, number && r > 0 ? Calc::Value(value) : Calc::Value(rows.at(r).at(c)));
            }
        }
    }
    Calc::TableSpec table;
    table.name = QStringLiteral("Table1");
    table.range = {{0, 0}, {int(rows.size()) - 1, int(rows.first().size()) - 1}};
    table.columnNames = rows.first();
    book.sheetAt(0).setTables({table});
    const QString path = dir + QStringLiteral("/data.xlsx");
    return Calc::IO::writeXlsxFile(book, path) ? fileBytes(path) : QByteArray();
}

struct Written {
    Ooxml::XmlElementPtr xml;
    QString text;
    QByteArray workbook;
};

Written written(const QByteArray &zip)
{
    Written out;
    const auto package = Ooxml::Package::fromData(zip);
    const QString slide = QStringLiteral("ppt/slides/slide4.xml");
    if (const auto rel = package->firstOfType(slide, kRel + QStringLiteral("chart"))) {
        const QString part = package->targetPart(slide, *rel);
        out.xml = package->xml(part);
        out.text = QString::fromUtf8(package->data(part));
        if (const auto data = package->firstOfType(part, kRel + QStringLiteral("package"))) {
            out.workbook = package->data(package->targetPart(part, *data));
        }
    }
    return out;
}

void validates(const QByteArray &zip, const char *name)
{
    if (!QindaOffice::Test::schemaValidationAvailable()) {
        return;
    }
    const auto package = Ooxml::Package::fromData(zip);
    QVERIFY(package);
    QStringList errors = QindaOffice::Test::validatePackage(*package);
    const Written chart = written(zip);
    if (!chart.workbook.isEmpty()) {
        errors += QindaOffice::Test::validatePackage(*Ooxml::Package::fromData(chart.workbook));
    }
    QVERIFY2(errors.isEmpty(), qPrintable(QLatin1String(name) + QStringLiteral(":\n") + errors.join(QLatin1Char('\n'))));
}

// Every series formula in document order.
QStringList formulas(const QString &xml)
{
    QStringList out;
    static const QRegularExpression kF(QStringLiteral("<c:f>([^<]*)</c:f>"));
    for (auto it = kF.globalMatch(xml); it.hasNext();) {
        out.append(it.next().captured(1));
    }
    return out;
}

int kindNamed(const char *label)
{
    for (int i = 0; i < ChartEdit::kinds().size(); ++i) {
        if (QLatin1String(ChartEdit::kinds().at(i).label) == QLatin1String(label)) {
            return i;
        }
    }
    return -1;
}

ChartData &chartOf(Presentation &deck)
{
    return deck.slides[3].shapes[0].chart;
}

ChartData readBack(const QByteArray &zip)
{
    QString error;
    Presentation deck = readPptxData(zip, &error);
    if (!error.isEmpty() || deck.slides.size() < 4 || deck.slides.at(3).shapes.isEmpty()) {
        qWarning("cannot read the written deck back: %s", qPrintable(error));
        return {};
    }
    return chartOf(deck);
}

// The parts an edit of slide 4's chart does not touch, against the deck
// saved without the edit (the first save normalises the hand-built XML of
// the slides) and, for the media and the theme, against the source.
void untouchedPartsKept(const QByteArray &source, const QByteArray &after)
{
    const auto original = Ooxml::Package::fromData(source);
    const auto plain = Ooxml::Package::fromData(writePptx(readPptxData(source)));
    const auto edited = Ooxml::Package::fromData(after);
    for (const char *part : {"ppt/slides/slide1.xml", "ppt/slides/slide2.xml", "ppt/slides/slide3.xml",
                             "ppt/slides/slide5.xml", "ppt/notesSlides/notesSlide1.xml"}) {
        QVERIFY2(!plain->data(QLatin1String(part)).isEmpty() && plain->data(QLatin1String(part)) == edited->data(QLatin1String(part)), part);
    }
    for (const char *part : {"ppt/media/image1.png", "ppt/theme/theme1.xml"}) {
        QVERIFY2(original->data(QLatin1String(part)) == edited->data(QLatin1String(part)), part);
    }
}

} // namespace

class ChartComboTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void bothPlotsAndTheGroupsRead();
    void dataEditsReachBothPlots();
    void deletedCategoryTakesItsPointAlong();
    void kindChangesKeepPointColoursAndLabels();
    void libreOfficeRendersTheEditedCombo();

private:
    QTemporaryDir m_dir;
    QByteArray m_combo;
    QByteArray m_pie;
};

void ChartComboTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    const QByteArray data = workbookOf(m_dir.path(), {{"Region", "Quarter", "North", "South", "Total"},
                                                      {"East", "Q1", "3", "4", "7"},
                                                      {"", "Q2", "5", "1", "6"},
                                                      {"West", "Q1", "2", "6", "8"},
                                                      {"", "Q2", "4", "2", "6"}});
    QVERIFY(!data.isEmpty());
    m_combo = deckWith(comboChart(), data);
    m_pie = deckWith(pieChart(), workbookOf(m_dir.path(), {{"Item", "Share"}, {"Rent", "50"}, {"Food", "30"}, {"Travel", "20"}}));
    validates(m_combo, "combo fixture");
    validates(m_pie, "pie fixture");
}

void ChartComboTest::bothPlotsAndTheGroupsRead()
{
    Presentation deck = readPptxData(m_combo);
    const ChartData &chart = chartOf(deck);
    QCOMPARE(chart.plots.size(), 2);
    QCOMPARE(chart.plots.at(0), (ChartPlot{QStringLiteral("column"), QStringLiteral("clustered")}));
    QCOMPARE(chart.plots.at(1), (ChartPlot{QStringLiteral("line"), QStringLiteral("standard")}));
    QCOMPARE(chart.type, QStringLiteral("column"));
    QCOMPARE(chart.series.size(), 3);
    QCOMPARE(chart.series.at(2).name, QStringLiteral("Total"));
    QCOMPARE(chart.series.at(2).plot, 1);
    QCOMPARE(chart.series.at(2).sourceIdx, 2);
    QCOMPARE(chart.series.at(0).categories, (QStringList{QStringLiteral("Q1"), QStringLiteral("Q2"), QStringLiteral("Q1"), QStringLiteral("Q2")}));
    QCOMPARE(chart.categoryLevels, (QList<QStringList>{{QStringLiteral("East"), QString(), QStringLiteral("West"), QString()}}));
    QCOMPARE(chart.series.at(0).pointColors, (QList<QColor>{QColor(), kRed}));
    QCOMPARE(chart.categorySource, (QList<int>{0, 1, 2, 3}));
    QCOMPARE(ChartEdit::plotKindIndex(chart, 1), kindNamed("Line"));
    // Unedited, it travels as it was.
    deck.slides[3].shapes[0].rect.translate(10, 0);
    const QByteArray moved = writePptx(deck);
    QCOMPARE(written(moved).text.toUtf8(), comboChart());
}

void ChartComboTest::dataEditsReachBothPlots()
{
    Presentation deck = readPptxData(m_combo);
    ChartData &chart = chartOf(deck);
    ChartEdit::ChartTable table = ChartEdit::tableOf(chart);
    table.values[0][1] = 9;                           // North, East Q2
    table.categories.append(QStringLiteral("Q3"));    // a West Q3
    table.categorySource.append(-1);
    for (QList<qreal> &values : table.values) {
        values.append(1);
    }
    table.series.append(QStringLiteral("Target"));    // next to Total: a line
    table.seriesSource.append(-1);
    table.values.append({6, 6, 6, 6, 6});
    ChartEdit::setTable(chart, table);
    QCOMPARE(chart.series.at(3).plot, 1);
    QCOMPARE(chart.categoryLevels.first(), (QStringList{QStringLiteral("East"), QString(), QStringLiteral("West"), QString(), QString()}));
    QString error;
    const QByteArray zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    validates(zip, "edited combo");
    untouchedPartsKept(m_combo, zip);
    const ChartData back = readBack(zip);
    QCOMPARE(back, chart);

    const Written out = written(zip);
    // Both plots' series follow the data; the categories keep both levels.
    QCOMPARE(formulas(out.text), (QStringList{
        QStringLiteral("Sheet1!$C$1"), QStringLiteral("Sheet1!$A$2:$B$6"), QStringLiteral("Sheet1!$C$2:$C$6"),
        QStringLiteral("Sheet1!$D$1"), QStringLiteral("Sheet1!$A$2:$B$6"), QStringLiteral("Sheet1!$D$2:$D$6"),
        QStringLiteral("Sheet1!$E$1"), QStringLiteral("Sheet1!$A$2:$B$6"), QStringLiteral("Sheet1!$E$2:$E$6"),
        QStringLiteral("Sheet1!$F$1"), QStringLiteral("Sheet1!$A$2:$B$6"), QStringLiteral("Sheet1!$F$2:$F$6")}));
    const Ooxml::XmlElement *plotArea = out.xml->child(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart"), u"chart")
                                            ->child(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart"), u"plotArea");
    const QString c = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart");
    QCOMPARE(plotArea->child(c, u"lineChart")->elements(c, u"ser").size(), 2);
    QCOMPARE(plotArea->child(c, u"barChart")->elements(c, u"ser").size(), 2);
    QVERIFY(out.text.contains(QStringLiteral("<c:dPt><c:idx val=\"1\"/>")));      // North's red point
    QVERIFY(out.text.contains(QStringLiteral("<c:dLblPos val=\"outEnd\"/>")));    // its labels, untouched
    QVERIFY(out.text.contains(QStringLiteral("<c:pt idx=\"2\"><c:v>West</c:v></c:pt></c:lvl>")));

    // The workbook PowerPoint's Edit Data opens: both levels, the new data.
    const QString path = m_dir.filePath(QStringLiteral("combo.xlsx"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly) && file.write(out.workbook) == out.workbook.size());
    file.close();
    Calc::Workbook book;
    QVERIFY(Calc::IO::readXlsxFile(path, &book));
    const auto cell = [&book](int row, int column) { return book.cellValue(0, row, column); };
    QCOMPARE(Calc::toText(cell(1, 0)), QStringLiteral("East"));
    QVERIFY(Calc::isBlank(cell(2, 0)));
    QCOMPARE(Calc::toText(cell(3, 0)), QStringLiteral("West"));
    QVERIFY(Calc::isBlank(cell(5, 0)));
    QCOMPARE(Calc::toText(cell(5, 1)), QStringLiteral("Q3"));
    QCOMPARE(*Calc::toNumber(cell(2, 2)), 9.0);
    QCOMPARE(Calc::toText(cell(0, 5)), QStringLiteral("Target"));
    QCOMPARE(*Calc::toNumber(cell(5, 4)), 1.0);
    QCOMPARE(book.sheetAt(0).tables().first().range, (Calc::CellRange{{0, 0}, {5, 5}}));
}

void ChartComboTest::deletedCategoryTakesItsPointAlong()
{
    Presentation deck = readPptxData(m_combo);
    ChartData &chart = chartOf(deck);
    ChartEdit::ChartTable table = ChartEdit::tableOf(chart);
    // East Q1 goes: East's label moves to Q2; North's red point (East Q2)
    // is now the first, its own label (West Q2) the third.
    table.categories.removeFirst();
    table.categorySource.removeFirst();
    for (QList<qreal> &values : table.values) {
        values.removeFirst();
    }
    ChartEdit::setTable(chart, table);
    QCOMPARE(chart.categoryLevels.first(), (QStringList{QStringLiteral("East"), QStringLiteral("West"), QString()}));
    QCOMPARE(chart.series.at(0).pointColors, (QList<QColor>{kRed}));
    const QByteArray zip = writePptx(deck);
    validates(zip, "deleted category");
    QCOMPARE(readBack(zip), chart);
    const QString xml = written(zip).text;
    QVERIFY(xml.contains(QStringLiteral("<c:dPt><c:idx val=\"0\"/>")));
    QVERIFY(xml.contains(QStringLiteral("<c:dLbl><c:idx val=\"2\"/>")));
    QVERIFY(!xml.contains(QStringLiteral("<c:dLbl><c:idx val=\"3\"/>")));
}

void ChartComboTest::kindChangesKeepPointColoursAndLabels()
{
    // A combo's columns become a line; the line stays a line.
    {
        Presentation deck = readPptxData(m_combo);
        ChartData &chart = chartOf(deck);
        ChartEdit::setPlotKind(chart, 0, kindNamed("Line"));
        QCOMPARE(chart.plots.at(0).type, QStringLiteral("line"));
        const QByteArray zip = writePptx(deck);
        validates(zip, "combo kind change");
        const ChartData back = readBack(zip);
        QCOMPARE(back, chart);
        QCOMPARE(back.series.at(0).pointColors, (QList<QColor>{QColor(), kRed}));
        const QString xml = written(zip).text;
        QCOMPARE(xml.count(QStringLiteral("<c:lineChart>")), 2);
        QVERIFY(xml.contains(QStringLiteral("<c:showVal val=\"1\"/>")));      // the labels came along…
        QVERIFY(!xml.contains(QStringLiteral("<c:dLblPos")));                 // …at the line's own place
    }
    // Stacked columns cannot put labels past their ends.
    {
        Presentation deck = readPptxData(m_combo);
        ChartEdit::setPlotKind(chartOf(deck), 0, kindNamed("Stacked column"));
        const QByteArray zip = writePptx(deck);
        validates(zip, "stacked combo");
        const QString xml = written(zip).text;
        QVERIFY(xml.contains(QStringLiteral("<c:grouping val=\"stacked\"/>")));
        QVERIFY(!xml.contains(QStringLiteral("outEnd")));
        QVERIFY(xml.contains(QStringLiteral("<c:dPt><c:idx val=\"1\"/>")));
    }
    // A pie's slices keep their colours as columns, its labels their content.
    {
        Presentation deck = readPptxData(m_pie);
        ChartData &chart = chartOf(deck);
        const QList<QColor> slices = chart.series.at(0).pointColors;
        QCOMPARE(slices.size(), 3);
        ChartEdit::setKind(chart, kindNamed("Clustered column"));
        const QByteArray zip = writePptx(deck);
        validates(zip, "pie to columns");
        untouchedPartsKept(m_pie, zip);
        const ChartData back = readBack(zip);
        QCOMPARE(back.type, QStringLiteral("column"));
        QCOMPARE(back.series.at(0).pointColors, slices);
        const QString xml = written(zip).text;
        QVERIFY(xml.contains(QStringLiteral("<c:showPercent val=\"1\"/>")));
        QVERIFY(!xml.contains(QStringLiteral("bestFit")));
    }
}

void ChartComboTest::libreOfficeRendersTheEditedCombo()
{
    QProcess probe;
    QindaOffice::Test::startSoffice(probe, {QStringLiteral("--headless"), QStringLiteral("--version")});
    if (!probe.waitForFinished(15000) || probe.exitCode() != 0) {
        QSKIP("LibreOffice unavailable in this environment");
    }
    Presentation deck = readPptxData(m_combo);
    ChartEdit::ChartTable table = ChartEdit::tableOf(chartOf(deck));
    table.values[2] = {9, 9, 9, 9};
    ChartEdit::setTable(chartOf(deck), table);
    const QString path = m_dir.filePath(QStringLiteral("combo.pptx"));
    QFile out(path);
    const QByteArray bytes = writePptx(deck);
    QVERIFY(out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size());
    out.close();
    QProcess soffice;
    QindaOffice::Test::startSoffice(soffice, {QStringLiteral("--headless"), QStringLiteral("--norestore"),
                                              QStringLiteral("-env:UserInstallation=file://%1/profile").arg(m_dir.path()),
                                              QStringLiteral("--convert-to"), QStringLiteral("pdf"),
                                              QStringLiteral("--outdir"), m_dir.filePath(QStringLiteral("pdf")), path});
    QVERIFY(soffice.waitForFinished(180000));
    const QByteArray pdf = fileBytes(m_dir.filePath(QStringLiteral("pdf/combo.pdf")));
    QVERIFY2(!pdf.isEmpty(), qPrintable(QString::fromUtf8(soffice.readAllStandardError())));
    static const QRegularExpression kPage(QStringLiteral("/Type\\s*/Page[^s]"));
    int pages = 0;
    for (auto it = kPage.globalMatch(QString::fromLatin1(pdf)); it.hasNext(); it.next()) {
        ++pages;
    }
    QCOMPARE(pages, 4); // the hidden slide left out
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    ChartComboTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pptx_chart_combo.moc"
