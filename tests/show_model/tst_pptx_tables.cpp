// SPDX-License-Identifier: GPL-3.0-or-later
// Table and notes editing gates (M67, D-076): the table operations on the
// model (rows and columns around merged blocks, merge and split, borders
// on both sides of an edge, the style options re-deriving the header's
// look), an edited PowerPoint table written back through its source — the
// unedited cells byte for byte, the style id and options kept, the edits
// read back equal — a new table written as a real a:tbl in the deck's
// default table style, edited speaker notes written into the notes slide
// they came from, and every package valid against the ECMA-376 schemas.

#include "pptx_fixture_slides.h"
#include "pptx_reader.h"
#include "pptx_writer.h"
#include "presentation.h"
#include "table_edit.h"

#include "ooxml_package.h"
#include "ooxml_xml.h"
#include "support/ooxml_schema.h"
#include "support/soffice_env.h"

#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>

using namespace QindaOffice::Show;
namespace Ooxml = QindaOffice::Ooxml;

namespace {

const QString kDefaultStyle = QStringLiteral("{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}");

TextParagraph paragraph(const QString &text, const TextRun &look = TextRun())
{
    TextRun run = look;
    run.text = text;
    TextParagraph p;
    p.alignment = int(Qt::AlignLeft); // as a written paragraph reads back
    p.endPointSize = run.pointSize;
    p.runs = {run};
    return p;
}

QString cellText(const Table &table, int row, int column)
{
    QString text;
    for (const TextParagraph &p : table.rows.at(row).cells.at(column).paragraphs) {
        for (const TextRun &r : p.runs) {
            text += r.text;
        }
    }
    return text;
}

// A 3 × 3 table with text "rc" in every cell.
Table grid()
{
    Table table = TableEdit::create(3, 3, 300, 30, kDefaultStyle, TableLook());
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            table.rows[r].cells[c].paragraphs = {paragraph(QStringLiteral("%1%2").arg(r).arg(c))};
        }
    }
    return table;
}

const Ooxml::XmlElement *find(const Ooxml::XmlElement &root, QStringView name)
{
    if (root.name == name) {
        return &root;
    }
    for (const Ooxml::XmlElement *child : root.elements()) {
        if (const Ooxml::XmlElement *found = find(*child, name)) {
            return found;
        }
    }
    return nullptr;
}

QString tcFragment(const Ooxml::Package &package, const QString &part, int row, int column)
{
    const Ooxml::XmlElementPtr root = package.xml(part);
    const Ooxml::XmlElement *tbl = root ? find(*root, u"tbl") : nullptr;
    if (tbl == nullptr) {
        return {};
    }
    const QList<const Ooxml::XmlElement *> rows = tbl->elements(QString::fromUtf16(Ooxml::Ns::A), u"tr");
    if (row >= rows.size()) {
        return {};
    }
    const QList<const Ooxml::XmlElement *> cells = rows.at(row)->elements(QString::fromUtf16(Ooxml::Ns::A), u"tc");
    return column < cells.size() ? Ooxml::serializeFragment(*cells.at(column)) : QString();
}

void validates(const QByteArray &zip, const char *name)
{
    if (!QindaOffice::Test::schemaValidationAvailable()) {
        return;
    }
    const auto package = Ooxml::Package::fromData(zip);
    QVERIFY(package);
    const QStringList errors = QindaOffice::Test::validatePackage(*package);
    QVERIFY2(errors.isEmpty(), qPrintable(QLatin1String(name) + QStringLiteral(":\n") + errors.join(QLatin1Char('\n'))));
}

} // namespace

class TableEditTest : public QObject {
    Q_OBJECT
private slots:
    void rowsAndColumnsAroundMergedBlocks();
    void mergeAndSplit();
    void bordersAndFills();
    void styleOptionsRestyleTheHeader();
    void editedTableKeepsUneditedCells();
    void newTableIsARealTable();
    void editedNotesKeepTheirNotesSlide();
    void libreOfficeConvertsTheEditedDeck();
};

void TableEditTest::rowsAndColumnsAroundMergedBlocks()
{
    Table table = grid();
    QVERIFY(TableEdit::merge(table, {0, 0, 1, 0})); // a vertical block in column 0
    QCOMPARE(table.rows.at(0).cells.at(0).rowSpan, 2);
    QVERIFY(table.rows.at(1).cells.at(0).vMerge);
    QCOMPARE(cellText(table, 0, 0), QStringLiteral("0010")); // both texts kept
    // A row inserted inside the block grows it; below it, it does not.
    QVERIFY(TableEdit::insertRows(table, 1, 1, 0));
    QCOMPARE(table.rows.size(), 4);
    QCOMPARE(table.rows.at(0).cells.at(0).rowSpan, 3);
    QVERIFY(table.rows.at(1).cells.at(0).vMerge);
    QCOMPARE(cellText(table, 1, 1), QString());
    QVERIFY(TableEdit::insertRows(table, 3, 1, 2));
    QCOMPARE(table.rows.at(0).cells.at(0).rowSpan, 3);
    QVERIFY(!table.rows.at(3).cells.at(0).vMerge);
    // Deleting the block's top row moves its anchor (and text) down.
    QVERIFY(TableEdit::deleteRows(table, 0, 1));
    QCOMPARE(table.rows.at(0).cells.at(0).rowSpan, 2);
    QVERIFY(!table.rows.at(0).cells.at(0).vMerge);
    QCOMPARE(cellText(table, 0, 0), QStringLiteral("0010"));
    QCOMPARE(table.origin.rows.size(), table.rows.size());
    // Columns: a horizontal block grows over an inserted column.
    QVERIFY(TableEdit::merge(table, {3, 1, 3, 2}));
    QVERIFY(TableEdit::insertColumns(table, 2, 1, 1));
    QCOMPARE(table.columns.size(), 4);
    QCOMPARE(table.rows.at(3).cells.at(1).gridSpan, 3);
    QVERIFY(table.rows.at(3).cells.at(2).hMerge && table.rows.at(3).cells.at(3).hMerge);
    QVERIFY(TableEdit::deleteColumns(table, 1, 1)); // the block's anchor column
    QCOMPARE(table.rows.at(3).cells.at(1).gridSpan, 2);
    QVERIFY(!table.rows.at(3).cells.at(1).hMerge);
    // The last row or column never goes; every row keeps one cell per column.
    QVERIFY(!TableEdit::deleteRows(table, 0, int(table.rows.size())));
    for (const TableRow &row : table.rows) {
        QCOMPARE(row.cells.size(), table.columns.size());
    }
    QCOMPARE(table.origin.columns.size(), table.columns.size());
}

void TableEditTest::mergeAndSplit()
{
    Table table = grid();
    // A merge grows to whole blocks it touches.
    QVERIFY(TableEdit::merge(table, {0, 0, 0, 1}));
    QVERIFY(TableEdit::merge(table, {0, 1, 1, 1}));
    QCOMPARE(table.rows.at(0).cells.at(0).gridSpan, 2);
    QCOMPARE(table.rows.at(0).cells.at(0).rowSpan, 2);
    QCOMPARE(TableEdit::blockAt(table, 1, 1), (TableRange{0, 0, 1, 1}));
    // A merged block re-divides into parts that divide it, and only those.
    QVERIFY(!TableEdit::split(table, 0, 0, 3, 1));
    QVERIFY(TableEdit::split(table, 1, 1, 2, 1));
    QCOMPARE(table.rows.at(0).cells.at(0).rowSpan, 2);
    QCOMPARE(table.rows.at(0).cells.at(0).gridSpan, 1);
    QCOMPARE(table.rows.at(0).cells.at(1).rowSpan, 2);
    QCOMPARE(cellText(table, 0, 1), QString());
    // A single cell gains grid lines; the cells they cross span them.
    Table plain = grid();
    QVERIFY(TableEdit::split(plain, 1, 1, 2, 2));
    QCOMPARE(plain.columns.size(), 4);
    QCOMPARE(plain.rows.size(), 4);
    QCOMPARE(plain.columns.at(1) + plain.columns.at(2), 100.0);
    QCOMPARE(plain.rows.at(0).cells.at(1).gridSpan, 2);  // above: spans the new column
    QCOMPARE(plain.rows.at(1).cells.at(0).rowSpan, 2);   // beside: spans the new row
    QCOMPARE(plain.rows.at(1).cells.at(3).rowSpan, 2);
    for (int r = 1; r <= 2; ++r) {
        for (int c = 1; c <= 2; ++c) {
            const TableCell &cell = plain.rows.at(r).cells.at(c);
            QVERIFY(!cell.hMerge && !cell.vMerge && cell.gridSpan == 1 && cell.rowSpan == 1);
        }
    }
    QCOMPARE(cellText(plain, 1, 1), QStringLiteral("11"));
}

void TableEditTest::bordersAndFills()
{
    Table table = grid();
    CellBorder pen;
    pen.color = QColor(Qt::red);
    pen.width = 2;
    TableEdit::setBorders(table, {1, 1, 1, 1}, TableEdit::Borders::Outside, pen);
    const TableCell &middle = table.rows.at(1).cells.at(1);
    QVERIFY(middle.top.set && middle.bottom.set && middle.left.set && middle.right.set);
    QCOMPARE(middle.top.color, QColor(Qt::red));
    // The facing edges of the neighbours carry the same line.
    QCOMPARE(table.rows.at(0).cells.at(1).bottom, middle.top);
    QCOMPARE(table.rows.at(1).cells.at(2).left, middle.right);
    QVERIFY(!table.rows.at(0).cells.at(0).bottom.set);
    // Inside borders of a range leave its outline alone.
    TableEdit::setBorders(table, {0, 0, 2, 2}, TableEdit::Borders::None, pen);
    QVERIFY(table.rows.at(1).cells.at(1).top.set && !table.rows.at(1).cells.at(1).top.visible());
    TableEdit::setBorders(table, {0, 0, 2, 2}, TableEdit::Borders::InsideHorizontal, pen);
    QVERIFY(table.rows.at(1).cells.at(1).top.visible());
    QVERIFY(!table.rows.at(1).cells.at(1).left.visible());
    QVERIFY(!table.rows.at(0).cells.at(0).top.visible());
    TableEdit::setFill(table, {0, 0, 0, 2}, QColor(10, 20, 30));
    QCOMPARE(table.rows.at(0).cells.at(2).fill, QColor(10, 20, 30));
    TableEdit::setFill(table, {0, 0, 0, 0}, QColor(Qt::transparent));
    QVERIFY(table.rows.at(0).cells.at(0).noFill);
    TableEdit::setFill(table, {0, 0, 0, 0}, QColor());
    QVERIFY(!table.rows.at(0).cells.at(0).noFill && !table.rows.at(0).cells.at(0).fill.isValid());
}

void TableEditTest::styleOptionsRestyleTheHeader()
{
    Table table = grid();
    table.look.known = true;
    table.look.whole.text = QColor(Qt::black);
    table.look.firstRow.text = QColor(Qt::white);
    table.look.firstRow.bold = true;
    for (TableCell &cell : table.rows[0].cells) {
        cell.paragraphs.first().runs.first().color = Qt::white;
        cell.paragraphs.first().runs.first().bold = true;
    }
    TableEdit::setStyleOption(table, TableEdit::StyleOption::HeaderRow, false);
    QVERIFY(!table.firstRow);
    QCOMPARE(table.rows.at(0).cells.at(0).paragraphs.first().runs.first().color, QColor(Qt::black));
    QVERIFY(!table.rows.at(0).cells.at(0).paragraphs.first().runs.first().bold);
    TableEdit::setStyleOption(table, TableEdit::StyleOption::HeaderRow, true);
    QCOMPARE(table.rows.at(0).cells.at(1).paragraphs.first().runs.first().color, QColor(Qt::white));
    // A row inserted above the header becomes the header; the old one body.
    QVERIFY(TableEdit::insertRows(table, 0, 1, 0));
    QCOMPARE(table.rows.at(1).cells.at(0).paragraphs.first().runs.first().color, QColor(Qt::black));
    // Text typed into the new header takes the header's look.
    const TextRun look = TableEdit::textLook(table, 0, 0);
    QCOMPARE(look.color, QColor(Qt::white));
    QVERIFY(look.bold);
    QVERIFY(look.text.isEmpty());
}

void TableEditTest::editedTableKeepsUneditedCells()
{
    const QByteArray source = PptxFixture::build();
    Presentation deck = readPptxData(source);
    QCOMPARE(deck.slides.size(), 5);
    Shape &frame = deck.slides[1].shapes[0];
    QCOMPARE(frame.kind, ShapeKind::Table);
    Table &table = frame.table;
    const QString styleId = table.styleId;
    QVERIFY(TableEdit::insertRows(table, 0, 1, 0));    // a new header row
    QVERIFY(TableEdit::insertColumns(table, 0, 1, 0)); // a new first column
    table.rows[0].cells[1].paragraphs = {paragraph(QStringLiteral("Quarter"), TableEdit::textLook(table, 0, 1))};
    QVERIFY(TableEdit::setColumnWidth(table, 0, 90));
    TableEdit::fitFrame(frame);
    QCOMPARE(frame.rect.width(), 90.0 + 480.0);
    QString error;
    const QByteArray zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    validates(zip, "edited table");
    const Presentation back = readPptxData(zip, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const Shape &written = back.slides.at(1).shapes.at(0);
    QCOMPARE(written.rect, frame.rect);
    QCOMPARE(written.table.rows.size(), 3);
    QCOMPARE(written.table.columns.size(), 3);
    QCOMPARE(written.table, table);
    QCOMPARE(written.table.styleId, styleId);
    QVERIFY(written.table.firstRow && written.table.bandRows);
    // The cells nobody edited are their source a:tc, byte for byte —
    // "A" and the yellow "B" moved from (1,0)/(1,1) to (2,1)/(2,2).
    const auto before = Ooxml::Package::fromData(source);
    const auto after = Ooxml::Package::fromData(zip);
    const QString slide = QStringLiteral("ppt/slides/slide2.xml");
    QVERIFY(!tcFragment(*before, slide, 1, 1).isEmpty());
    QCOMPARE(tcFragment(*after, slide, 2, 1), tcFragment(*before, slide, 1, 0));
    QCOMPARE(tcFragment(*after, slide, 2, 2), tcFragment(*before, slide, 1, 1));
    // The old header moved into the body: its text keeps its source a:rPr
    // (the white bold was the style's) and now reads in the body's look.
    QVERIFY(tcFragment(*after, slide, 1, 1).contains(QStringLiteral("<a:rPr lang=\"en-US\"/>")));
    QCOMPARE(written.table.rows.at(1).cells.at(1).paragraphs.first().runs.first().color,
             table.rows.at(1).cells.at(1).paragraphs.first().runs.first().color);
    // Undoing everything gives the source table back, written verbatim.
    Presentation again = readPptxData(source);
    Table &undone = again.slides[1].shapes[0].table;
    QVERIFY(TableEdit::insertRows(undone, 2, 1, 1));
    QVERIFY(TableEdit::deleteRows(undone, 2, 1));
    QCOMPARE(tcFragment(*Ooxml::Package::fromData(writePptx(again)), slide, 0, 0), tcFragment(*before, slide, 0, 0));
}

void TableEditTest::newTableIsARealTable()
{
    for (const bool fromPowerPoint : {true, false}) {
        Presentation deck = fromPowerPoint ? readPptxData(PptxFixture::build()) : Presentation();
        if (!fromPowerPoint) {
            (void)deck.addSlide(QStringLiteral("Plain"));
        }
        Slide &slide = deck.slides.first();
        const NewTableStyle style = newTableStyle(deck, slide);
        QCOMPARE(style.styleId, kDefaultStyle);
        QVERIFY(style.look.known);
        Shape shape;
        shape.id = QStringLiteral("newTable");
        shape.kind = ShapeKind::Table;
        shape.rect = QRectF(50, 60, 0, 0);
        shape.table = TableEdit::create(2, 3, 360, 29.2, style.styleId, style.look);
        TextRun fallback;
        fallback.fontFamily = style.fontFamily;
        fallback.color = style.textColor;
        const TextRun look = TableEdit::textLook(shape.table, 0, 0, fallback);
        QVERIFY(look.bold); // the header row's look
        shape.table.rows[0].cells[0].paragraphs = {paragraph(QStringLiteral("Header"), look)};
        shape.table.rows[1].cells[2].paragraphs = {paragraph(QStringLiteral("Body"), TableEdit::textLook(shape.table, 1, 2, fallback))};
        TableEdit::fitFrame(shape);
        slide.shapes.append(shape);
        QString error;
        const QByteArray zip = writePptx(deck, &error);
        QVERIFY2(!zip.isEmpty(), qPrintable(error));
        validates(zip, fromPowerPoint ? "new table in a PowerPoint deck" : "new table in a QindaShow deck");
        const auto package = Ooxml::Package::fromData(zip);
        const Ooxml::XmlElementPtr root = package->xml(QStringLiteral("ppt/slides/slide1.xml"));
        QVERIFY(QString::fromUtf8(Ooxml::serializeDocument(*root))
                    .contains(QStringLiteral("uri=\"http://schemas.openxmlformats.org/drawingml/2006/table\"")));
        const Ooxml::XmlElement *tbl = find(*root, u"tbl");
        QVERIFY(tbl != nullptr);
        const QString xml = Ooxml::serializeFragment(*tbl);
        QVERIFY(xml.contains(QStringLiteral("<a:tableStyleId>%1</a:tableStyleId>").arg(kDefaultStyle)));
        QVERIFY(xml.contains(QStringLiteral("<a:tblPr firstRow=\"1\" bandRow=\"1\">")));
        // The style, not the text, carries the header's white bold.
        QVERIFY(!xml.contains(QStringLiteral("b=\"1\"")));
        const Presentation back = readPptxData(zip, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const Shape &read = back.slides.first().shapes.last();
        QCOMPARE(read.kind, ShapeKind::Table);
        QCOMPARE(read.table, shape.table);
        QCOMPARE(read.rect, shape.rect);
    }
}

void TableEditTest::editedNotesKeepTheirNotesSlide()
{
    const QByteArray source = PptxFixture::build();
    Presentation deck = readPptxData(source);
    QCOMPARE(deck.slides.first().notes, QStringLiteral("Speaker notes line one\nline two"));
    deck.slides[0].notes = QStringLiteral("Speaker notes line one\nA new middle line\nline two");
    deck.slides[2].notes = QStringLiteral("Notes for a slide that had none");
    QString error;
    const QByteArray zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    validates(zip, "edited notes");
    const Presentation back = readPptxData(zip, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.slides.at(0).notes, deck.slides.at(0).notes);
    QCOMPARE(back.slides.at(2).notes, deck.slides.at(2).notes);
    // The notes slide is the source's: its slide image placeholder and the
    // unchanged lines' runs as PowerPoint wrote them.
    const auto package = Ooxml::Package::fromData(zip);
    const QString notesXml = QString::fromUtf8(package->data(QStringLiteral("ppt/notesSlides/notesSlide1.xml")));
    QVERIFY(notesXml.contains(QStringLiteral("name=\"Slide Image Placeholder 1\"")));
    QVERIFY(notesXml.contains(QStringLiteral("<a:r><a:rPr lang=\"en-US\"/><a:t>line two</a:t></a:r>")));
    QVERIFY(notesXml.contains(QStringLiteral("<a:t>A new middle line</a:t>")));
    // Both notes slides belong to the deck's own notes master.
    QCOMPARE(package->partNames().filter(QRegularExpression(QStringLiteral("^ppt/notesMasters/notesMaster\\d+\\.xml$"))).size(), 1);
}

void TableEditTest::libreOfficeConvertsTheEditedDeck()
{
    QProcess probe;
    QindaOffice::Test::startSoffice(probe, {QStringLiteral("--headless"), QStringLiteral("--version")});
    if (!probe.waitForFinished(15000) || probe.exitCode() != 0) {
        QSKIP("LibreOffice unavailable in this environment");
    }
    Presentation deck = readPptxData(PptxFixture::build());
    Shape &frame = deck.slides[1].shapes[0];
    QVERIFY(TableEdit::insertRows(frame.table, 2, 1, 1));
    QVERIFY(TableEdit::merge(frame.table, {1, 0, 2, 0}));
    TableEdit::setBorders(frame.table, {0, 0, 2, 1}, TableEdit::Borders::All, CellBorder{true, QColor(Qt::black), 1, QString()});
    TableEdit::fitFrame(frame);
    deck.slides[0].notes = QStringLiteral("Edited notes");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("edited.pptx"));
    QFile out(path);
    const QByteArray bytes = writePptx(deck);
    QVERIFY(out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size());
    out.close();
    QProcess soffice;
    QindaOffice::Test::startSoffice(soffice, {QStringLiteral("--headless"), QStringLiteral("--norestore"),
                                              QStringLiteral("-env:UserInstallation=file://%1/profile").arg(dir.path()),
                                              QStringLiteral("--convert-to"), QStringLiteral("pdf"),
                                              QStringLiteral("--outdir"), dir.filePath(QStringLiteral("pdf")), path});
    QVERIFY(soffice.waitForFinished(180000));
    QFile pdf(dir.filePath(QStringLiteral("pdf/edited.pdf")));
    QVERIFY2(pdf.open(QIODevice::ReadOnly), qPrintable(QString::fromUtf8(soffice.readAllStandardError())));
    static const QRegularExpression kPage(QStringLiteral("/Type\\s*/Page[^s]"));
    int pages = 0;
    for (auto it = kPage.globalMatch(QString::fromLatin1(pdf.readAll())); it.hasNext(); it.next()) {
        ++pages;
    }
    QCOMPARE(pages, 4); // the hidden slide left out
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    TableEditTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pptx_tables.moc"
