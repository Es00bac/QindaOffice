// SPDX-License-Identifier: GPL-3.0-or-later
// show_model gates (plan section 8): a 5-slide deck round-trips through
// the .fodp writer/reader with shapes, text runs, images, notes, builds
// and transitions intact; the written file converts to .pptx through
// LibreOffice and reads back (live, skipped when unavailable); the
// renderer paints non-blank slides.

#include "fodp_reader.h"
#include "fodp_writer.h"
#include "presentation.h"
#include "shape_json.h"
#include "slide_renderer.h"
#include "support/soffice_env.h"

#include "ooxml_namespaces.h"
#include "ooxml_package.h"

#include <QGuiApplication>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>

using namespace QindaOffice::Show;

class ShowModelTest : public QObject {
    Q_OBJECT
private slots:
    void fodpRoundTrip();
    void fodpCarriesTheOfficeModel();
    void fodpConvertsToPptx();
    void rendererPaints();
    void buildsHideFutureShapes();
    void shapeJsonCodecRoundTrips();
};

static TextParagraph textParagraph(const QString &text, bool bold, const QColor &color, qreal size, int alignment,
                                   bool bullet)
{
    TextRun run;
    run.text = text;
    run.bold = bold;
    run.color = color;
    run.pointSize = size;
    TextParagraph paragraph;
    paragraph.runs = {run};
    paragraph.alignment = alignment;
    paragraph.bullet = bullet;
    return paragraph;
}

static Presentation sampleDeck()
{
    Presentation presentation;
    presentation.title = QStringLiteral("M8 Sample");
    for (int i = 0; i < 5; ++i) {
        Slide &slide = presentation.addSlide(QStringLiteral("Slide %1").arg(i + 1));
        slide.layout = i == 0 ? QStringLiteral("title") : QStringLiteral("title+content");
        slide.transition = i % 2 == 0 ? QStringLiteral("fade") : QStringLiteral("cut");
        slide.notes = QStringLiteral("speaker note %1").arg(i + 1);

        Shape title;
        title.kind = ShapeKind::TextBox;
        title.rect = QRectF(40, 30, 640, 80);
        title.paragraphs.append(textParagraph(QStringLiteral("Quarterly %1").arg(i + 1), true, QColor(Qt::darkBlue), 28,
                                              int(Qt::AlignHCenter), false));
        slide.shapes.append(title);

        Shape body;
        body.kind = ShapeKind::Rectangle;
        body.rect = QRectF(60, 140, 280, 160);
        body.fill = QColor(200, 220, 255);
        body.stroke = QColor(Qt::black);
        body.strokeWidth = 1.5;
        body.paragraphs.append(textParagraph(QStringLiteral("bullet one"), false, QColor(), 18, 0, true));
        body.buildStep = i % 2; // every other slide animates its body
        slide.shapes.append(body);

        Shape arrow;
        arrow.kind = ShapeKind::Arrow;
        arrow.rect = QRectF(380, 200, 120, 60);
        arrow.stroke = QColor(Qt::darkRed);
        arrow.strokeWidth = 2.0;
        slide.shapes.append(arrow);

        Shape image;
        image.kind = ShapeKind::Image;
        image.rect = QRectF(520, 140, 120, 120);
        QPixmap pixmap(32, 32);
        pixmap.fill(QColor(255, 180, 60));
        QBuffer buffer(&image.imagePng);
        buffer.open(QIODevice::WriteOnly);
        pixmap.save(&buffer, "PNG");
        slide.shapes.append(image);
    }
    return presentation;
}

void ShowModelTest::fodpRoundTrip()
{
    const Presentation deck = sampleDeck();
    QString error;
    const QByteArray fodp = writeFodp(deck, &error);
    QVERIFY2(!fodp.isEmpty(), qPrintable(error));
    QVERIFY(fodp.contains("application/vnd.oasis.opendocument.presentation"));

    const Presentation back = readFodp(fodp, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.slides.size(), 5);
    // Lengths travel in points with their unit: exact, not "close".
    QCOMPARE(back.slideSize, deck.slideSize);
    for (int i = 0; i < 5; ++i) {
        const Slide &was = deck.slides.at(i);
        const Slide &is = back.slides.at(i);
        QCOMPARE(is.title, was.title);
        QCOMPARE(is.transition, was.transition);
        QCOMPARE(is.notes, was.notes);
        QCOMPARE(is.shapes.size(), was.shapes.size());
        for (int s = 0; s < was.shapes.size(); ++s) {
            // Geometry: the pre-M62 writer stored unit-less cm the reader
            // took as points (shapes shrank to 3.5 % on reopen).
            QCOMPARE(is.shapes.at(s).rect, was.shapes.at(s).rect);
            QCOMPARE(is.shapes.at(s).kind, was.shapes.at(s).kind);
            // Fills and outlines: the styles are linked now.
            QCOMPARE(is.shapes.at(s).fill, was.shapes.at(s).fill);
            QCOMPARE(is.shapes.at(s).stroke, was.shapes.at(s).stroke);
            QCOMPARE(is.shapes.at(s).strokeWidth, was.shapes.at(s).strokeWidth);
            QCOMPARE(is.shapes.at(s).buildStep, was.shapes.at(s).buildStep);
            // Run formatting lives in T* styles, not invalid span attributes.
            QCOMPARE(is.shapes.at(s).paragraphs, was.shapes.at(s).paragraphs);
        }
        QCOMPARE(is.shapes.at(3).imagePng, was.shapes.at(3).imagePng);
        QCOMPARE(is, was);
    }
    QCOMPARE(back.slides.at(0).shapes.at(0).paragraphs.first().runs.first().color, QColor(Qt::darkBlue));
    QVERIFY(back.slides.at(0).shapes.at(0).paragraphs.first().runs.first().bold);
    QCOMPARE(back.slides.at(0).shapes.at(0).paragraphs.first().runs.first().pointSize, 28.0);
    QCOMPARE(back.slides.at(0).shapes.at(0).paragraphs.first().alignment, int(Qt::AlignHCenter));
    QVERIFY(back.slides.at(0).shapes.at(1).paragraphs.first().bullet);
    // Arrow geometry survived as a line with a marker, in its direction.
    QCOMPARE(back.slides.at(0).shapes.at(2).kind, ShapeKind::Arrow);
    QCOMPARE(back, deck);
}

void ShowModelTest::fodpCarriesTheOfficeModel()
{
    // What .fodp keeps beyond ODF (D-070): Office shapes and tables, slide
    // backgrounds and visibility, master decorations and fills, layouts.
    Presentation deck;
    deck.notesSize = QSizeF(612, 792);
    Shape logo;
    logo.id = QStringLiteral("logo");
    logo.kind = ShapeKind::Rectangle;
    logo.rect = QRectF(650, 360, 50, 30);
    logo.fill = QColor(Qt::green);
    SlideMaster master;
    master.id = QStringLiteral("m1");
    master.typeDefaults = false;
    master.backgroundStyle.kind = FillKind::Gradient;
    master.backgroundStyle.stops = {{0, QColor(Qt::white)}, {1, QColor(Qt::gray)}};
    master.shapes = {logo};
    deck.masters = {master};
    SlideLayout layout;
    layout.id = QStringLiteral("l1");
    layout.name = QStringLiteral("Banner");
    layout.type = QStringLiteral("obj");
    layout.master = master.id;
    layout.background = {true, QColor(250, 240, 230), FillStyle()};
    logo.id = QStringLiteral("band");
    layout.shapes = {logo};
    deck.layouts = {layout};
    Slide &slide = deck.addSlide(QStringLiteral("Office"));
    slide.master = master.id;
    slide.layoutId = layout.id;
    slide.hidden = true;
    slide.showMasterShapes = false;
    slide.background = {true, QColor(0, 0, 64), FillStyle()};
    Shape arrow;
    arrow.id = QStringLiteral("arrow");
    arrow.kind = ShapeKind::AutoShape;
    arrow.geometry = QStringLiteral("rightArrow");
    arrow.adjust = {{QStringLiteral("adj1"), 40000}};
    arrow.rect = QRectF(50, 50, 200, 80);
    arrow.flipH = true;
    arrow.fillStyle = master.backgroundStyle;
    arrow.line.dash = QStringLiteral("dash");
    arrow.textBody.anchor = TextAnchor::Middle;
    TextRun run;
    run.text = QStringLiteral("Next");
    run.underline = QStringLiteral("sng");
    run.baseline = 30000;
    TextParagraph paragraph;
    paragraph.runs = {run};
    paragraph.marginLeft = 18;
    paragraph.autoNumber = QStringLiteral("arabicPeriod");
    arrow.paragraphs = {paragraph};
    slide.shapes.append(arrow);
    Shape table;
    table.id = QStringLiteral("table");
    table.kind = ShapeKind::Table;
    table.rect = QRectF(50, 200, 300, 60);
    table.table.columns = {150, 150};
    TableCell cell;
    cell.paragraphs = {paragraph};
    cell.fill = QColor(Qt::yellow);
    cell.gridSpan = 2;
    TableCell covered;
    covered.hMerge = true;
    table.table.rows = {TableRow{30, {cell, covered}}, TableRow{30, {TableCell(), TableCell()}}};
    slide.shapes.append(table);

    QString error;
    const QByteArray fodp = writeFodp(deck, &error);
    const Presentation back = readFodp(fodp, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back, deck);
    QCOMPARE(writeFodp(back), fodp);
    // LibreOffice's view: an arrow, a picture of the table, a hidden page.
    QVERIFY(fodp.contains("draw:type=\"right-arrow\""));
    QVERIFY(fodp.contains("presentation:visibility=\"hidden\""));
}

void ShowModelTest::fodpConvertsToPptx()
{
    QProcess probe;
    QindaOffice::Test::startSoffice(
        probe, {QStringLiteral("--headless"),
                QStringLiteral("--version")});
    if (!probe.waitForFinished(15000) || probe.exitCode() != 0) {
        QSKIP("LibreOffice unavailable in this environment");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString fodpPath = dir.path() + QStringLiteral("/deck.fodp");
    {
        QFile file(fodpPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(writeFodp(sampleDeck()));
    }
    auto convert = [&](const QString &from, const QString &to) -> QString {
        QProcess soffice;
        QindaOffice::Test::startSoffice(
            soffice, {QStringLiteral("--headless"), QStringLiteral("--norestore"),
                      QStringLiteral("--nologo"),
                      QStringLiteral("-env:UserInstallation=file://%1/profile").arg(dir.path()),
                      QStringLiteral("--convert-to"), to, QStringLiteral("--outdir"),
                      dir.path(), from});
        if (!soffice.waitForFinished(120000)) {
            return {};
        }
        const QString base = QFileInfo(from).completeBaseName();
        return dir.path() + QStringLiteral("/%1.%2").arg(base, to);
    };
    const QString pptx = convert(fodpPath, QStringLiteral("pptx"));
    QVERIFY2(QFile::exists(pptx), "fodp -> pptx conversion failed");

    // LibreOffice places the shapes where the model has them (the pre-M62
    // unit-less lengths piled every shape into the top-left corner) and
    // keeps their fills (the styles are linked).
    namespace Ooxml = QindaOffice::Ooxml;
    QString error;
    const auto package = Ooxml::Package::load(pptx, &error);
    QVERIFY2(package, qPrintable(error));
    const Ooxml::XmlElementPtr slide = package->xml(QStringLiteral("ppt/slides/slide1.xml"));
    QVERIFY(slide);
    const QString a = QString::fromUtf16(Ooxml::Ns::A);
    QList<QPointF> offsets;
    QStringList fills;
    const std::function<void(const Ooxml::XmlElement &)> walk = [&](const Ooxml::XmlElement &e) {
        if (e.is(a, u"off")) {
            offsets.append(QPointF(e.attr(u"x").toDouble() / 12700.0, e.attr(u"y").toDouble() / 12700.0));
        }
        if (e.is(a, u"srgbClr")) {
            fills.append(e.attr(u"val").toLower());
        }
        for (const Ooxml::XmlElement *c : e.elements()) {
            walk(*c);
        }
    };
    walk(*slide);
    const auto near = [&offsets](const QPointF &expected) {
        return std::any_of(offsets.cbegin(), offsets.cend(), [&expected](const QPointF &p) {
            return qAbs(p.x() - expected.x()) < 1.0 && qAbs(p.y() - expected.y()) < 1.0;
        });
    };
    QVERIFY2(near(QPointF(40, 30)), "title box not at (40, 30) pt");
    QVERIFY2(near(QPointF(60, 140)), "rectangle not at (60, 140) pt");
    QVERIFY2(fills.contains(QStringLiteral("c8dcff")), "rectangle fill lost");

    // The pptx opens in Impress: convert it back to fodp and parse it.
    const QString back = convert(pptx, QStringLiteral("fodp"));
    if (!QFile::exists(back)) {
        QSKIP("LibreOffice pptx->fodp conversion unavailable here");
    }
    QFile file(back);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const Presentation reparsed = readFodp(file.readAll(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(reparsed.slides.size(), 5);
    // …and what LibreOffice wrote reads back at the right size and place.
    const Shape &title = reparsed.slides.first().shapes.first();
    QVERIFY2(qAbs(title.rect.x() - 40) < 1 && qAbs(title.rect.width() - 640) < 1,
             qPrintable(QStringLiteral("%1 %2").arg(title.rect.x()).arg(title.rect.width())));
}

void ShowModelTest::rendererPaints()
{
    const Presentation deck = sampleDeck();
    const QPixmap pixmap = renderSlideToPixmap(deck.slides.first(), deck.slideSize, {320, 180});
    bool painted = false;
    for (int y = 0; y < pixmap.height() && !painted; ++y) {
        for (int x = 0; x < pixmap.width(); ++x) {
            if (pixmap.toImage().pixelColor(x, y) != QColor(Qt::white)) {
                painted = true;
                break;
            }
        }
    }
    QVERIFY(painted);
}

void ShowModelTest::buildsHideFutureShapes()
{
    Slide slide;
    Shape visible;
    visible.rect = QRectF(0, 0, 50, 50);
    visible.fill = Qt::black;
    visible.buildStep = 0;
    Shape later;
    later.rect = QRectF(60, 0, 50, 50);
    later.fill = Qt::black;
    later.buildStep = 2;
    slide.shapes = {visible, later};

    QImage early(200, 100, QImage::Format_ARGB32);
    early.fill(Qt::white);
    QPainter painter(&early);
    renderSlide(&painter, slide, QSizeF(200, 100), 1); // build step 1
    painter.end();
    QVERIFY(early.pixelColor(25, 25) != QColor(Qt::white)); // step-0 shape shown
    QVERIFY(early.pixelColor(85, 25) == QColor(Qt::white)); // step-2 shape hidden

    QImage late(200, 100, QImage::Format_ARGB32);
    late.fill(Qt::white);
    QPainter painter2(&late);
    renderSlide(&painter2, slide, QSizeF(200, 100), 2);
    painter2.end();
    QVERIFY(late.pixelColor(85, 25) != QColor(Qt::white)); // now visible
}

void ShowModelTest::shapeJsonCodecRoundTrips()
{
    QindaOffice::Show::Shape shape;
    shape.id = QStringLiteral("ignored-on-wire");
    shape.kind = QindaOffice::Show::ShapeKind::TextBox;
    shape.rect = QRectF(1.5, 2.25, 300, 90);
    shape.rotation = 12.5;
    shape.fill = QColor(10, 20, 30, 40);
    shape.stroke = QColor(200, 100, 50);
    shape.strokeWidth = 2.5;
    shape.buildStep = 2;
    shape.extra.insert(QStringLiteral("qq:note"), QStringLiteral("hi"));
    QindaOffice::Show::TextParagraph paragraph;
    paragraph.alignment = int(Qt::AlignHCenter);
    paragraph.bullet = true;
    paragraph.listLevel = 1;
    QindaOffice::Show::TextRun run;
    run.text = QStringLiteral("Hello \u00e9");
    run.bold = true;
    run.italic = true;
    run.color = QColor(1, 2, 3);
    run.pointSize = 24;
    run.fontFamily = QStringLiteral("DejaVu Sans");
    paragraph.runs.append(run);
    shape.paragraphs.append(paragraph);
    shape.imagePng = QByteArray("\x89PNG\x01\x02", 6);

    const QByteArray payload = QindaOffice::Show::shapesToJson({shape});
    const QList<QindaOffice::Show::Shape> back = QindaOffice::Show::shapesFromJson(payload);
    QCOMPARE(back.size(), 1);
    const QindaOffice::Show::Shape &decoded = back.first();
    // The codec assigns a fresh id (clipboard consumers must not reuse).
    QVERIFY(!decoded.id.isEmpty());
    QVERIFY(decoded.id != shape.id);
    QCOMPARE(int(decoded.kind), int(shape.kind));
    QCOMPARE(decoded.rect, shape.rect);
    QCOMPARE(decoded.rotation, shape.rotation);
    QCOMPARE(decoded.fill, shape.fill);
    QCOMPARE(decoded.stroke, shape.stroke);
    QCOMPARE(decoded.strokeWidth, shape.strokeWidth);
    QCOMPARE(decoded.buildStep, shape.buildStep);
    QCOMPARE(decoded.extra.value(QStringLiteral("qq:note")).toString(),
             QStringLiteral("hi"));
    QCOMPARE(decoded.paragraphs.size(), 1);
    QCOMPARE(decoded.paragraphs.first().runs.size(), 1);
    const QindaOffice::Show::TextRun &runBack = decoded.paragraphs.first().runs.first();
    QCOMPARE(runBack.text, run.text);
    QVERIFY(runBack.bold && runBack.italic);
    QCOMPARE(runBack.color, run.color);
    QCOMPARE(runBack.pointSize, 24.0);
    QCOMPARE(runBack.fontFamily, run.fontFamily);
    QCOMPARE(decoded.paragraphs.first().alignment, paragraph.alignment);
    QVERIFY(decoded.paragraphs.first().bullet);
    QCOMPARE(decoded.paragraphs.first().listLevel, 1);
    QCOMPARE(decoded.imagePng, shape.imagePng);

    // Empty/garbage payloads decode to nothing, never crash.
    QVERIFY(QindaOffice::Show::shapesFromJson({}).isEmpty());
    QVERIFY(QindaOffice::Show::shapesFromJson("{nope").isEmpty());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    ShowModelTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_show_model.moc"
