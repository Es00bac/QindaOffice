// SPDX-License-Identifier: GPL-3.0-or-later
// Chart data gates (M67, D-076), over the hand-built deck of
// pptx_fixture_slides.h whose chart is given an embedded workbook the way
// PowerPoint embeds one (c:externalData → an .xlsx holding the data in an
// Excel table): edited values, an added category and series and a renamed
// series are written into the chart part's formulas and caches AND into
// the embedded workbook — re-read through calc_io it holds the edited
// numbers where the formulas point, its table grown over them; the chart
// reads back equal; kind changes (line, pie, scatter, bar) rebuild the plot
// with the axes each needs; every package (and every embedded workbook)
// validates against the ECMA-376 schemas; LibreOffice converts the edited
// deck (skipped without it).

#include "chart_edit.h"
#include "pptx_fixture_slides.h"
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

const QString kChartPart = QStringLiteral("ppt/charts/chart1.xml");
const QString kRel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/");

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The fixture's data (North 3/5/2, South 4/1/6 over Q1..Q3) as PowerPoint
// keeps it: Sheet1, a header row, an Excel table over A1:C4.
QByteArray fixtureWorkbook(const QString &dir)
{
    Calc::Workbook book;
    book.addSheet(QStringLiteral("Sheet1"));
    const auto set = [&book](int row, int column, const Calc::Value &value) { book.setCellValue(0, {row, column}, value); };
    set(0, 0, QStringLiteral("Quarter"));
    set(0, 1, QStringLiteral("North"));
    set(0, 2, QStringLiteral("South"));
    const double north[] = {3, 5, 2};
    const double south[] = {4, 1, 6};
    for (int k = 0; k < 3; ++k) {
        set(k + 1, 0, QStringLiteral("Q%1").arg(k + 1));
        set(k + 1, 1, north[k]);
        set(k + 1, 2, south[k]);
    }
    Calc::TableSpec table;
    table.name = QStringLiteral("Table1");
    table.range = {{0, 0}, {3, 2}};
    table.columnNames = {QStringLiteral("Quarter"), QStringLiteral("North"), QStringLiteral("South")};
    book.sheetAt(0).setTables({table});
    const QString path = dir + QStringLiteral("/fixture.xlsx");
    return Calc::IO::writeXlsxFile(book, path) ? fileBytes(path) : QByteArray();
}

// The fixture deck with its chart's workbook embedded.
QByteArray chartDeck(const QString &dir)
{
    const auto base = Ooxml::Package::fromData(PptxFixture::build());
    Ooxml::PackageWriter w;
    const QStringList parts = base->partNames();
    for (const QString &part : parts) {
        if (part.startsWith(QLatin1Char('[')) || part.endsWith(QLatin1String(".rels"))) {
            continue;
        }
        QByteArray data = base->data(part);
        if (part == kChartPart) {
            data.replace("<c:chartSpace ", "<c:chartSpace xmlns:r=\"" + kRel.chopped(1).toUtf8() + "\" ");
            data.replace("</c:chartSpace>", "<c:externalData r:id=\"rId1\"><c:autoUpdate val=\"0\"/></c:externalData></c:chartSpace>");
        }
        w.addPart(part, data, base->contentType(part));
    }
    for (const QString &part : QStringList{QString()} + parts) {
        for (const Ooxml::Relationship &rel : base->relationships(part)) {
            w.addRelationshipWithId(part, rel.id, rel.type, rel.external ? rel.target : base->targetPart(part, rel), rel.external);
        }
    }
    const QString embedding = QStringLiteral("ppt/embeddings/Microsoft_Excel_Worksheet.xlsx");
    w.addPart(embedding, fixtureWorkbook(dir), QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"));
    w.addRelationshipWithId(kChartPart, QStringLiteral("rId1"), kRel + QStringLiteral("package"), embedding);
    return w.toZip();
}

// The chart part slide 4 points at in a written package, and its workbook.
struct WrittenChart {
    QString part;
    Ooxml::XmlElementPtr xml;
    QByteArray workbook;
};

WrittenChart writtenChart(const Ooxml::Package &package)
{
    WrittenChart out;
    const QString slide = QStringLiteral("ppt/slides/slide4.xml");
    if (const auto rel = package.firstOfType(slide, kRel + QStringLiteral("chart"))) {
        out.part = package.targetPart(slide, *rel);
        out.xml = package.xml(out.part);
        if (const auto data = package.firstOfType(out.part, kRel + QStringLiteral("package"))) {
            out.workbook = package.data(package.targetPart(out.part, *data));
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
    const WrittenChart chart = writtenChart(*package);
    if (!chart.workbook.isEmpty()) {
        errors += QindaOffice::Test::validatePackage(*Ooxml::Package::fromData(chart.workbook));
    }
    QVERIFY2(errors.isEmpty(), qPrintable(QLatin1String(name) + QStringLiteral(":\n") + errors.join(QLatin1Char('\n'))));
}

QString formulas(const Ooxml::XmlElement &chartSpace)
{
    QStringList out;
    std::function<void(const Ooxml::XmlElement &)> walk = [&](const Ooxml::XmlElement &e) {
        if (e.name == u"f") {
            out.append(e.text());
        }
        for (const Ooxml::XmlElement *c : e.elements()) {
            walk(*c);
        }
    };
    walk(chartSpace);
    return out.join(QLatin1Char(' '));
}

} // namespace

class ChartEditTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void tableOfAChart();
    void editedDataReachesCachesAndWorkbook();
    void unchangedChartIsCarriedVerbatim();
    void kindChangesRebuildThePlot();
    void libreOfficeConvertsTheEditedDeck();

private:
    QTemporaryDir m_dir;
    QByteArray m_deck;
};

void ChartEditTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_deck = chartDeck(m_dir.path());
    QVERIFY(!m_deck.isEmpty());
}

void ChartEditTest::tableOfAChart()
{
    const Presentation deck = readPptxData(m_deck);
    const Shape &chart = deck.slides.at(3).shapes.at(0);
    QCOMPARE(chart.kind, ShapeKind::Chart);
    const ChartEdit::ChartTable table = ChartEdit::tableOf(chart.chart);
    QCOMPARE(table.categories, QStringList({QStringLiteral("Q1"), QStringLiteral("Q2"), QStringLiteral("Q3")}));
    QCOMPARE(table.series, QStringList({QStringLiteral("North"), QStringLiteral("South")}));
    QCOMPARE(table.values.at(1), QList<qreal>({4, 1, 6}));
    // setTable(tableOf(x)) changes nothing; names are made Excel-safe.
    ChartData same = chart.chart;
    ChartEdit::setTable(same, table);
    QCOMPARE(same, chart.chart);
    ChartEdit::ChartTable renamed = table;
    renamed.series = {QString(), QStringLiteral("South")};
    renamed.series.append(QStringLiteral("south"));
    renamed.values.append({1, 2, 3});
    ChartEdit::setTable(same, renamed);
    QCOMPARE(same.series.at(0).name, QStringLiteral("Series 1"));
    QCOMPARE(same.series.at(2).name, QStringLiteral("south2"));
    QCOMPARE(ChartEdit::kinds().at(ChartEdit::kindIndex(chart.chart)).type, QStringLiteral("column"));
    QVERIFY(std::isnan(ChartEdit::parseValue(QStringLiteral("n/a"))));
    QCOMPARE(ChartEdit::valueText(4.3), QStringLiteral("4.3"));
}

void ChartEditTest::editedDataReachesCachesAndWorkbook()
{
    Presentation deck = readPptxData(m_deck);
    Shape &shape = deck.slides[3].shapes[0];
    ChartEdit::ChartTable table = ChartEdit::tableOf(shape.chart);
    table.categories.append(QStringLiteral("Q4"));
    table.series[1] = QStringLiteral("South region");
    table.values[0] = {3, 7.5, 2, 9};
    table.values[1] = {4, 1, 6, std::nan("")};
    table.series.append(QStringLiteral("East"));
    table.values.append({1.25, 2, 3, 4});
    ChartEdit::setTable(shape.chart, table);
    QString error;
    const QByteArray zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    validates(zip, "edited chart");
    const Presentation back = readPptxData(zip, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.slides.at(3).shapes.at(0).chart, shape.chart);

    const auto package = Ooxml::Package::fromData(zip);
    const WrittenChart written = writtenChart(*package);
    QVERIFY(written.xml);
    // Formulas follow the data where it sits; only the new chart part is in
    // the package (the stale one was not carried along).
    QCOMPARE(formulas(*written.xml), QStringLiteral("Sheet1!$B$1 Sheet1!$A$2:$A$5 Sheet1!$B$2:$B$5 "
                                                   "Sheet1!$C$1 Sheet1!$A$2:$A$5 Sheet1!$C$2:$C$5 "
                                                   "Sheet1!$D$1 Sheet1!$A$2:$A$5 Sheet1!$D$2:$D$5"));
    QCOMPARE(package->partNames().filter(QRegularExpression(QStringLiteral("^ppt/charts/chart\\d+\\.xml$"))).size(), 1);
    QCOMPARE(package->partNames().filter(QStringLiteral("ppt/embeddings/")).size(), 1);

    // PowerPoint's Edit Data opens this workbook: it holds the new numbers.
    QVERIFY(!written.workbook.isEmpty());
    const QString path = m_dir.filePath(QStringLiteral("written.xlsx"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly) && file.write(written.workbook) == written.workbook.size());
    file.close();
    Calc::Workbook book;
    QVERIFY(Calc::IO::readXlsxFile(path, &book));
    const qint32 sheet = book.sheetIndex(QStringLiteral("Sheet1"));
    QVERIFY(sheet >= 0);
    const auto cell = [&book, sheet](int row, int column) { return book.cellValue(sheet, row, column); };
    QCOMPARE(Calc::toText(cell(0, 1)), QStringLiteral("North"));
    QCOMPARE(Calc::toText(cell(0, 2)), QStringLiteral("South region"));
    QCOMPARE(Calc::toText(cell(0, 3)), QStringLiteral("East"));
    QCOMPARE(Calc::toText(cell(4, 0)), QStringLiteral("Q4"));
    QCOMPARE(*Calc::toNumber(cell(2, 1)), 7.5);
    QCOMPARE(*Calc::toNumber(cell(4, 1)), 9.0);
    QVERIFY(Calc::isBlank(cell(4, 2)));
    QCOMPARE(*Calc::toNumber(cell(1, 3)), 1.25);
    QCOMPARE(*Calc::toNumber(cell(4, 3)), 4.0);
    QCOMPARE(Calc::toText(cell(0, 0)), QStringLiteral("Quarter")); // what the edit did not touch
    const QList<Calc::TableSpec> &tables = book.sheetAt(sheet).tables();
    QCOMPARE(tables.size(), 1);
    QCOMPARE(tables.first().range, (Calc::CellRange{{0, 0}, {4, 3}}));

    // A shrink empties what the data no longer reaches.
    Presentation smaller = readPptxData(zip);
    ChartEdit::ChartTable less = ChartEdit::tableOf(smaller.slides[3].shapes[0].chart);
    less.categories.removeLast();
    less.series.removeLast();
    less.values.removeLast();
    for (QList<qreal> &values : less.values) {
        values.removeLast();
    }
    ChartEdit::setTable(smaller.slides[3].shapes[0].chart, less);
    const QByteArray shrunk = writePptx(smaller);
    validates(shrunk, "shrunk chart");
    QCOMPARE(readPptxData(shrunk).slides.at(3).shapes.at(0).chart, smaller.slides.at(3).shapes.at(0).chart);
    QFile again(path);
    QVERIFY(again.open(QIODevice::WriteOnly));
    again.write(writtenChart(*Ooxml::Package::fromData(shrunk)).workbook);
    again.close();
    Calc::Workbook book2;
    QVERIFY(Calc::IO::readXlsxFile(path, &book2));
    QVERIFY(Calc::isBlank(book2.cellValue(0, 0, 3)));
    QVERIFY(Calc::isBlank(book2.cellValue(0, 4, 1)));
    QCOMPARE(book2.sheetAt(0).tables().first().range, (Calc::CellRange{{0, 0}, {3, 2}}));
}

void ChartEditTest::unchangedChartIsCarriedVerbatim()
{
    Presentation deck = readPptxData(m_deck);
    deck.slides[3].shapes[0].rect.translate(10, 0); // moved, not edited
    const auto package = Ooxml::Package::fromData(writePptx(deck));
    const WrittenChart written = writtenChart(*package);
    const auto source = Ooxml::Package::fromData(m_deck);
    QCOMPARE(package->data(written.part), source->data(kChartPart));
    QCOMPARE(written.workbook, source->data(QStringLiteral("ppt/embeddings/Microsoft_Excel_Worksheet.xlsx")));
}

void ChartEditTest::kindChangesRebuildThePlot()
{
    for (const QString &label : {QStringLiteral("Line"), QStringLiteral("Pie"), QStringLiteral("Scatter"),
                                 QStringLiteral("Stacked bar"), QStringLiteral("Doughnut")}) {
        Presentation deck = readPptxData(m_deck);
        ChartData &chart = deck.slides[3].shapes[0].chart;
        int index = -1;
        for (int i = 0; i < ChartEdit::kinds().size(); ++i) {
            if (QLatin1String(ChartEdit::kinds().at(i).label) == label) {
                index = i;
            }
        }
        QVERIFY(index >= 0);
        ChartEdit::setKind(chart, index);
        QString error;
        const QByteArray zip = writePptx(deck, &error);
        QVERIFY2(!zip.isEmpty(), qPrintable(error));
        validates(zip, qPrintable(label));
        const ChartData back = readPptxData(zip).slides.at(3).shapes.at(0).chart;
        QCOMPARE(back.type, chart.type);
        QCOMPARE(back, chart);
        const WrittenChart written = writtenChart(*Ooxml::Package::fromData(zip));
        const QString xml = Ooxml::serializeFragment(*written.xml);
        const bool round = chart.type == u"pie" || chart.type == u"doughnut";
        QCOMPARE(xml.contains(QStringLiteral("<c:catAx>")), !round && chart.type != u"scatter");
        QCOMPARE(xml.count(QStringLiteral("<c:valAx>")), round ? 0 : (chart.type == u"scatter" ? 2 : 1));
        QVERIFY(!written.workbook.isEmpty());
    }
}

void ChartEditTest::libreOfficeConvertsTheEditedDeck()
{
    QProcess probe;
    QindaOffice::Test::startSoffice(probe, {QStringLiteral("--headless"), QStringLiteral("--version")});
    if (!probe.waitForFinished(15000) || probe.exitCode() != 0) {
        QSKIP("LibreOffice unavailable in this environment");
    }
    Presentation deck = readPptxData(m_deck);
    ChartEdit::ChartTable table = ChartEdit::tableOf(deck.slides[3].shapes[0].chart);
    table.categories.append(QStringLiteral("Q4"));
    for (QList<qreal> &values : table.values) {
        values.append(8);
    }
    ChartEdit::setTable(deck.slides[3].shapes[0].chart, table);
    ChartEdit::setKind(deck.slides[3].shapes[0].chart, 6); // line
    const QString path = m_dir.filePath(QStringLiteral("edited.pptx"));
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
    const QByteArray pdf = fileBytes(m_dir.filePath(QStringLiteral("pdf/edited.pdf")));
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
    ChartEditTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pptx_charts.moc"
