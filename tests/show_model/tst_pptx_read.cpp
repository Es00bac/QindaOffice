// SPDX-License-Identifier: GPL-3.0-or-later
// Native .pptx reader gates (M62 step 3, D-070), over a package built by
// hand (pptx_fixture_slides.h): placeholders inherit geometry and text
// style from the layout and the master; theme colours resolve with their
// transforms (accent1 lumMod 75 % = PowerPoint's "Darker 25 %"); bullets
// and auto-numbering; a table with a merge and the default style; a
// cropped picture; a nested group in its child space; a chart from its
// cached values; an unknown graphic frame kept verbatim; notes, hidden
// slides, transitions and builds; master decorations and backgrounds; and
// the renderer paints all of it (a grab lands in QQO_GRAB_DIR when set).

#include "fodp_reader.h"
#include "fodp_writer.h"
#include "pptx_fixture_slides.h"
#include "pptx_reader.h"
#include "pptx_writer.h"
#include "presentation.h"
#include "shape_json.h"
#include "slide_renderer.h"
#include "text_painter.h"

#include "ooxml_theme.h"
#include "ooxml_xml.h"

#include <QDir>
#include <QGuiApplication>
#include <QtTest>

using namespace QindaOffice::Show;

class PptxReadTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void readsDeckAndMasters();
    void placeholdersInheritFromLayoutAndMaster();
    void themeColoursWithLumMod();
    void bulletsAndAutoNumbering();
    void tableWithMerge();
    void croppedPicture();
    void nestedGroup();
    void chartFromCachedValues();
    void unknownFrameIsPreserved();
    void styleMatrixConnectorAndHyperlink();
    void notesHiddenTransitionsBuilds();
    void rendersEverySlide();
    void clipboardCarriesEveryShape();
    void savedAsFodpLooksTheSame();
    void refusesWhatIsNotAPresentation();

private:
    Presentation m_deck;
    PptxReadReport m_report;
};

namespace {

bool near(const QColor &a, const QColor &b, int tolerance = 2)
{
    return qAbs(a.red() - b.red()) <= tolerance && qAbs(a.green() - b.green()) <= tolerance
        && qAbs(a.blue() - b.blue()) <= tolerance && a.alpha() == b.alpha();
}

bool nearRect(const QRectF &a, const QRectF &b)
{
    return qAbs(a.x() - b.x()) < 1e-6 && qAbs(a.y() - b.y()) < 1e-6 && qAbs(a.width() - b.width()) < 1e-6
        && qAbs(a.height() - b.height()) < 1e-6;
}

// A shape without its identity (ids, placeholder, connections): what a
// paste — and .fodp's qq:shape — is not meant to carry.
Shape anonymous(Shape shape)
{
    shape.id.clear();
    shape.nvId = 0;
    shape.groupId.clear();
    shape.placeholder = {};
    shape.start = {};
    shape.end = {};
    for (Shape &child : shape.children) {
        child = anonymous(child);
    }
    return shape;
}

} // namespace

void PptxReadTest::initTestCase()
{
    QString error;
    m_deck = readPptxData(PptxFixture::build(), &error, &m_report);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(m_deck.slides.size(), 5);
}

void PptxReadTest::readsDeckAndMasters()
{
    QCOMPARE(m_deck.slideSize, QSizeF(960, 540));
    QCOMPARE(m_deck.notesSize, QSizeF(540, 720));
    QCOMPARE(m_report.slides, 5);
    QCOMPARE(m_deck.masters.size(), 1);
    const SlideMaster &master = m_deck.masters.first();
    QCOMPARE(master.id, QStringLiteral("slideMaster1"));
    QCOMPARE(master.name, QStringLiteral("Fixture Master"));
    QCOMPARE(master.background, QColor(0xFF, 0xFF, 0xF0));
    QVERIFY(!master.typeDefaults);
    QCOMPARE(master.fontFamily, QStringLiteral("Calibri"));
    // The decorative logo is drawn on every slide; the placeholders are not.
    QCOMPARE(master.shapes.size(), 1);
    QCOMPARE(master.shapes.first().fill, QColor(0x70, 0xAD, 0x47));
    QVERIFY(nearRect(master.shapes.first().rect, QRectF(900, 500, 40, 20)));
    QCOMPARE(m_deck.layouts.size(), 1);
    QCOMPARE(m_deck.layouts.first().type, QStringLiteral("obj"));
    QCOMPARE(m_deck.layouts.first().master, master.id);
    for (const Slide &slide : m_deck.slides) {
        QCOMPARE(slide.master, master.id);
        QCOMPARE(slide.layoutId, QStringLiteral("slideLayout1"));
        QCOMPARE(slide.layout, QStringLiteral("title+content"));
    }
    QCOMPARE(m_deck.slides.first().id, QStringLiteral("s256"));
}

void PptxReadTest::placeholdersInheritFromLayoutAndMaster()
{
    const Slide &slide = m_deck.slides.at(0);
    QCOMPARE(slide.title, QStringLiteral("Inherited title"));
    // The title has no xfrm and the layout's has none either: the master's.
    const Shape &title = slide.shapes.at(0);
    QCOMPARE(title.kind, ShapeKind::TextBox);
    QCOMPARE(title.placeholder.type, QStringLiteral("title"));
    QVERIFY(nearRect(title.rect, QRectF(36, 18, 888, 72)));
    QCOMPARE(title.textBody.anchor, TextAnchor::Middle); // the master's bodyPr
    const TextRun &titleRun = title.paragraphs.first().runs.first();
    QCOMPARE(titleRun.pointSize, 44.0);                 // titleStyle
    QCOMPARE(titleRun.fontFamily, QStringLiteral("Calibri Light")); // +mj-lt
    QCOMPARE(titleRun.color, QColor(Qt::black));        // tx1 → dk1
    // The body placeholder (idx 1, no type) matches the layout's by index:
    // its geometry, and its lstStyle over the master's bodyStyle.
    const Shape &body = slide.shapes.at(1);
    QCOMPARE(body.placeholder.type, QStringLiteral("obj"));
    QCOMPARE(body.placeholder.index, qint64(1));
    QVERIFY(nearRect(body.rect, QRectF(72, 144, 432, 288)));
    QCOMPARE(body.textBody.autoFit, AutoFit::Normal);
    QCOMPARE(body.textBody.fontScale, 92.5);
    QCOMPARE(body.textBody.lineSpaceReduction, 10.0);
    QCOMPARE(body.textBody.insetLeft, 7.2); // DrawingML's default, not QindaShow's
    const TextParagraph &first = body.paragraphs.at(0);
    QCOMPARE(first.runs.first().pointSize, 20.0);                  // layout lvl1
    QCOMPARE(first.runs.first().color, QColor(0xED, 0x7D, 0x31));  // accent2
    QCOMPARE(first.marginLeft, 18.0);                               // master bodyStyle
    QCOMPARE(first.indent, -18.0);
    QCOMPARE(first.spaceBefore, Spacing::points(10));
    const TextParagraph &second = body.paragraphs.at(1);
    QCOMPARE(second.listLevel, 1);
    QCOMPARE(second.runs.first().pointSize, 24.0); // master lvl2
    QCOMPARE(second.marginLeft, 54.0);
    QVERIFY(second.runs.first().bold);
    QCOMPARE(second.runs.first().color, QColor(Qt::black));
}

void PptxReadTest::themeColoursWithLumMod()
{
    const Shape &box = m_deck.slides.at(0).shapes.at(2);
    QCOMPARE(box.kind, ShapeKind::TextBox);
    QVERIFY(!box.framed);
    QVERIFY(nearRect(box.rect, QRectF(540, 144, 360, 180)));
    QCOMPARE(box.textBody.insetLeft, 0.0);
    QCOMPARE(box.textBody.autoFit, AutoFit::Shape);
    const TextRun &run = box.paragraphs.at(0).runs.first();
    // accent1 4472C4 at 75 % luminance: PowerPoint's "Darker 25 %" 2F5597.
    QVERIFY2(near(run.color, QColor(0x2F, 0x55, 0x97)), qPrintable(run.color.name()));
    QCOMPARE(run.underline, QStringLiteral("sng"));
    QCOMPARE(run.pointSize, 24.0);
    QCOMPARE(run.fontFamily, QStringLiteral("Calibri")); // otherStyle +mn-lt
}

void PptxReadTest::bulletsAndAutoNumbering()
{
    const Slide &slide = m_deck.slides.at(0);
    const TextParagraph &point = slide.shapes.at(1).paragraphs.at(0);
    QVERIFY(point.bullet);
    QCOMPARE(point.bulletChar, QStringLiteral("•"));
    QCOMPARE(point.bulletFont, QStringLiteral("Arial"));
    QCOMPARE(slide.shapes.at(1).paragraphs.at(1).bulletChar, QStringLiteral("–"));
    const Shape &box = slide.shapes.at(2);
    QVERIFY(!box.paragraphs.at(0).bullet);
    for (int i = 1; i <= 3; ++i) {
        QVERIFY(box.paragraphs.at(i).bullet);
        QCOMPARE(box.paragraphs.at(i).marginLeft, 27.0);
        QCOMPARE(box.paragraphs.at(i).bulletFont, QStringLiteral("Calibri Light"));
    }
    QCOMPARE(box.paragraphs.at(1).autoNumber, QStringLiteral("arabicPeriod"));
    QCOMPARE(box.paragraphs.at(2).autoNumber, QStringLiteral("arabicPeriod"));
    QCOMPARE(box.paragraphs.at(3).autoNumber, QStringLiteral("alphaLcParenR"));
    QCOMPARE(box.paragraphs.at(3).autoNumberStart, 3);
    QCOMPARE(Render::autoNumberLabel(QStringLiteral("arabicPeriod"), 2), QStringLiteral("2."));
    QCOMPARE(Render::autoNumberLabel(QStringLiteral("alphaLcParenR"), 3), QStringLiteral("c)"));
    QCOMPARE(Render::autoNumberLabel(QStringLiteral("romanUcPeriod"), 14), QStringLiteral("XIV."));
    // a:br is a line break inside the run; the superscript keeps its baseline.
    const TextParagraph &second = box.paragraphs.at(2);
    QCOMPARE(second.runs.size(), 2);
    QCOMPARE(second.runs.at(0).text, QStringLiteral("Second") + QChar(QChar::LineSeparator));
    QCOMPARE(second.runs.at(1).baseline, 30000);
}

void PptxReadTest::tableWithMerge()
{
    const Shape &frame = m_deck.slides.at(1).shapes.first();
    QCOMPARE(frame.kind, ShapeKind::Table);
    QVERIFY(nearRect(frame.rect, QRectF(72, 72, 480, 58.4)));
    const Table &table = frame.table;
    QCOMPARE(table.columns, QList<qreal>({240.0, 240.0}));
    QCOMPARE(table.rows.size(), 2);
    QVERIFY(table.firstRow && table.bandRows);
    QCOMPARE(table.styleId, QStringLiteral("{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}"));
    QVERIFY(table.look.known); // PowerPoint's default, built in
    QCOMPARE(table.rows.at(0).cells.at(0).gridSpan, 2);
    QVERIFY(table.rows.at(0).cells.at(1).hMerge);
    const TextRun &header = table.rows.at(0).cells.at(0).paragraphs.first().runs.first();
    QCOMPARE(header.text, QStringLiteral("Merged header"));
    QCOMPARE(header.color, QColor(Qt::white)); // the style's header text
    QVERIFY(header.bold);
    QCOMPARE(table.rows.at(1).cells.at(0).paragraphs.first().runs.first().color, QColor(Qt::black));
    QCOMPARE(table.rows.at(1).cells.at(1).fill, QColor(Qt::yellow));
    QCOMPARE(table.rows.at(1).cells.at(1).anchor, TextAnchor::Middle);
    QCOMPARE(frame.displayText(), QStringLiteral("Merged header\n\nA\nB"));
}

void PptxReadTest::croppedPicture()
{
    const Shape &picture = m_deck.slides.at(2).shapes.at(0);
    QCOMPARE(picture.kind, ShapeKind::Image);
    QCOMPARE(picture.description, QStringLiteral("A test swatch"));
    QVERIFY(nearRect(picture.rect, QRectF(72, 72, 200, 100)));
    QCOMPARE(picture.image.mime, QStringLiteral("image/png"));
    QCOMPARE(picture.image.pixels, QSize(40, 20));
    QCOMPARE(picture.image.cropLeft, 0.1);
    QCOMPARE(picture.image.cropRight, 0.2);
    QCOMPARE(picture.image.cropTop, 0.0);
    QCOMPARE(picture.imagePng, PptxFixture::pngBytes(QColor(200, 80, 40)));
}

void PptxReadTest::nestedGroup()
{
    const Shape &outer = m_deck.slides.at(2).shapes.at(1);
    QCOMPARE(outer.kind, ShapeKind::Group);
    QVERIFY(nearRect(outer.rect, QRectF(400, 100, 200, 200)));
    QVERIFY(nearRect(outer.childRect, QRectF(0, 0, 400, 400)));
    QCOMPARE(outer.children.size(), std::size_t(2));
    const Shape &oval = outer.children.at(0);
    QCOMPARE(oval.kind, ShapeKind::Ellipse);
    QCOMPARE(oval.fill, QColor(0x44, 0x72, 0xC4));
    const Shape &inner = outer.children.at(1);
    QCOMPARE(inner.kind, ShapeKind::Group);
    QVERIFY(nearRect(inner.rect, QRectF(200, 200, 200, 200)));
    QVERIFY(nearRect(inner.childRect, QRectF(0, 0, 100, 100)));
    QCOMPARE(inner.children.size(), std::size_t(1));
    QCOMPARE(inner.children.front().kind, ShapeKind::Rectangle);
    QCOMPARE(inner.children.front().fill, QColor(Qt::red));
}

void PptxReadTest::chartFromCachedValues()
{
    const Shape &frame = m_deck.slides.at(3).shapes.first();
    QCOMPARE(frame.kind, ShapeKind::Chart);
    QVERIFY(!frame.preserved.isEmpty()); // the frame itself travels verbatim
    const ChartData &chart = frame.chart;
    QCOMPARE(chart.type, QStringLiteral("column"));
    QCOMPARE(chart.grouping, QStringLiteral("clustered"));
    QCOMPARE(chart.title, QStringLiteral("Sales"));
    QVERIFY(chart.legend);
    QCOMPARE(chart.legendPosition, QStringLiteral("r"));
    QCOMPARE(chart.series.size(), 2);
    QCOMPARE(chart.series.at(0).name, QStringLiteral("North"));
    QCOMPARE(chart.series.at(0).categories, QStringList({QStringLiteral("Q1"), QStringLiteral("Q2"), QStringLiteral("Q3")}));
    QCOMPARE(chart.series.at(0).values, QList<qreal>({3, 5, 2}));
    QCOMPARE(chart.series.at(1).values, QList<qreal>({4, 1, 6}));
    QCOMPARE(chart.series.at(1).color, QColor(0xED, 0x7D, 0x31));
    QCOMPARE(chart.palette.first(), QColor(0x44, 0x72, 0xC4));
}

void PptxReadTest::unknownFrameIsPreserved()
{
    const Shape &frame = m_deck.slides.at(4).shapes.at(0);
    QCOMPARE(frame.kind, ShapeKind::Preserved);
    QCOMPARE(frame.nvId, 7u);
    QVERIFY(nearRect(frame.rect, QRectF(100, 100, 200, 100)));
    QCOMPARE(frame.preserved.part, QStringLiteral("ppt/slides/slide5.xml"));
    // Exactly the element, re-spelled with conventional prefixes only.
    const QByteArray wrapped = QByteArray("<root ") + PptxFixture::kNs + ">" + PptxFixture::kUnknownFrame + "</root>";
    const auto parsed = QindaOffice::Ooxml::parseXml(wrapped);
    QVERIFY(parsed);
    QCOMPARE(frame.preserved.fragment, QindaOffice::Ooxml::serializeFragment(*parsed->elements().first()));
    QVERIFY(m_report.preserved >= 1);
}

void PptxReadTest::styleMatrixConnectorAndHyperlink()
{
    const Slide &slide = m_deck.slides.at(4);
    const Shape &styled = slide.shapes.at(1);
    QCOMPARE(styled.kind, ShapeKind::AutoShape);
    QCOMPARE(styled.geometry, QStringLiteral("roundRect"));
    QCOMPARE(styled.adjust.size(), 1);
    QCOMPARE(styled.adjust.first().second, qint64(25000));
    QCOMPARE(styled.rotation, 30.0);
    QVERIFY(styled.flipH);
    QCOMPARE(styled.fillStyle.kind, FillKind::Gradient);
    QCOMPARE(styled.fillStyle.stops.size(), 2);
    QCOMPARE(styled.fillStyle.angle, 90.0);
    QCOMPARE(styled.fillStyle.stops.last().color, QColor(0x44, 0x72, 0xC4));
    // The outline comes from the theme's line style 2 with the ref's colour.
    QCOMPARE(styled.strokeWidth, 1.0);
    QVERIFY(near(styled.stroke, QindaOffice::Ooxml::applyShade(QColor(0x44, 0x72, 0xC4), 0.5)));
    // fontRef: white text.
    QCOMPARE(styled.paragraphs.first().runs.first().color, QColor(Qt::white));
    QCOMPARE(styled.paragraphs.first().alignment, int(Qt::AlignHCenter));
    QCOMPARE(styled.hyperlink, QStringLiteral("https://example.org/qindashow"));
    const Shape &connector = slide.shapes.at(2);
    QCOMPARE(connector.kind, ShapeKind::Arrow);
    QVERIFY(connector.connector);
    QCOMPARE(connector.geometry, QStringLiteral("straightConnector1"));
    QVERIFY(connector.flipV);
    QCOMPARE(connector.strokeWidth, 2.25);
    QCOMPARE(connector.stroke, QColor(0xC0, 0x00, 0x00));
    QCOMPARE(connector.line.dash, QStringLiteral("dash"));
    QCOMPARE(connector.line.tail.type, QStringLiteral("triangle"));
    QCOMPARE(connector.start, (Connection{7, 3}));
    QCOMPARE(connector.end, (Connection{8, 1}));
}

void PptxReadTest::notesHiddenTransitionsBuilds()
{
    QCOMPARE(m_deck.slides.at(0).notes, QStringLiteral("Speaker notes line one\nline two"));
    QVERIFY(m_deck.slides.at(1).notes.isEmpty());
    QVERIFY(m_deck.slides.at(4).hidden);
    QVERIFY(!m_deck.slides.at(0).hidden);
    QCOMPARE(m_deck.slides.at(1).transition, QStringLiteral("fade"));
    QCOMPARE(m_deck.slides.at(0).transition, QStringLiteral("cut"));
    const Shape &built = m_deck.slides.at(0).shapes.at(2);
    QCOMPARE(built.buildStep, 1);
    QCOMPARE(built.animation, ShapeAnimation::Fade);
    QCOMPARE(built.animationMs, 500);
    QCOMPARE(m_deck.slides.at(0).shapes.at(0).buildStep, 0);
}

void PptxReadTest::rendersEverySlide()
{
    const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR");
    for (qsizetype i = 0; i < m_deck.slides.size(); ++i) {
        const QImage image = renderSlideToImage(m_deck, m_deck.slides.at(i), QSize(960, 540));
        // The master's ivory background shows, and something is drawn on it.
        QCOMPARE(image.pixelColor(2, 2), QColor(0xFF, 0xFF, 0xF0));
        int painted = 0;
        for (int y = 0; y < image.height(); y += 4) {
            for (int x = 0; x < image.width(); x += 4) {
                painted += image.pixelColor(x, y) != QColor(0xFF, 0xFF, 0xF0) ? 1 : 0;
            }
        }
        QVERIFY2(painted > 50, qPrintable(QStringLiteral("slide %1 is blank").arg(i + 1)));
        if (!grabs.isEmpty()) {
            QDir().mkpath(grabs);
            image.save(QDir(grabs).filePath(QStringLiteral("pptx_read_slide%1.png").arg(i + 1)));
        }
    }
    // The logo from the master is on every slide (accent6 at 900,500).
    const QImage first = renderSlideToImage(m_deck, m_deck.slides.first(), QSize(960, 540));
    QCOMPARE(first.pixelColor(920, 510), QColor(0x70, 0xAD, 0x47));
}

void PptxReadTest::clipboardCarriesEveryShape()
{
    // The clipboard codec is total for the M62 model: every fixture shape
    // (tables, charts, groups, preserved content included) pastes back
    // equal but for its identity.
    int shapes = 0;
    for (const Slide &slide : m_deck.slides) {
        for (const Shape &shape : slide.shapes) {
            const QList<Shape> back = shapesFromJson(shapesToJson({shape}));
            QCOMPARE(back.size(), 1);
            QVERIFY2(anonymous(back.first()) == anonymous(shape), qPrintable(shape.name));
            ++shapes;
        }
    }
    QCOMPARE(shapes, 10);
}

void PptxReadTest::savedAsFodpLooksTheSame()
{
    // A PowerPoint deck saved as .fodp and opened again draws every slide
    // pixel for pixel as it did (qq:shape, masters, layouts, backgrounds).
    QString error;
    const QByteArray fodp = writeFodp(m_deck, &error);
    const Presentation back = readFodp(fodp, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.slides.size(), m_deck.slides.size());
    QCOMPARE(back.layouts.size(), m_deck.layouts.size());
    QCOMPARE(back.masters.first().shapes.size(), m_deck.masters.first().shapes.size());
    QCOMPARE(back.notesSize, m_deck.notesSize);
    for (qsizetype i = 0; i < m_deck.slides.size(); ++i) {
        QCOMPARE(back.slides.at(i).hidden, m_deck.slides.at(i).hidden);
        QCOMPARE(back.slides.at(i).notes, m_deck.slides.at(i).notes);
        const QImage before = renderSlideToImage(m_deck, m_deck.slides.at(i), QSize(960, 540));
        const QImage after = renderSlideToImage(back, back.slides.at(i), QSize(960, 540));
        QVERIFY2(before == after, qPrintable(QStringLiteral("slide %1 draws differently from .fodp").arg(i + 1)));
    }
    // The spelling is stable: written again, the same bytes.
    QCOMPARE(writeFodp(back, &error), fodp);
    if (const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR"); !grabs.isEmpty()) {
        QDir().mkpath(grabs); // for a look at LibreOffice's view of it
        QFile file(QDir(grabs).filePath(QStringLiteral("pptx_fixture.fodp")));
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write(fodp) == fodp.size());
    }
    // LibreOffice's view: tables, charts, groups and preserved content are
    // pictures; the logo is a picture on the master page.
    QVERIFY(fodp.contains("<style:master-page style:name=\"M1\""));
    QVERIFY(fodp.contains("qq:shape="));
    // And on to .pptx: a generated master with the logo, readable again.
    const Presentation again = readPptxData(writePptx(back, &error), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(again.slides.size(), m_deck.slides.size());
    const QImage first = renderSlideToImage(again, again.slides.first(), QSize(960, 540));
    QCOMPARE(first.pixelColor(920, 510), QColor(0x70, 0xAD, 0x47));
    QCOMPARE(first.pixelColor(2, 2), QColor(0xFF, 0xFF, 0xF0));
}

void PptxReadTest::refusesWhatIsNotAPresentation()
{
    QString error;
    const Presentation none = readPptxData(QByteArray("not a zip"), &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(none.slides.isEmpty());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    PptxReadTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pptx_read.moc"
