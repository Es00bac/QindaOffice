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

#include <QGuiApplication>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Show;

class ShowModelTest : public QObject {
    Q_OBJECT
private slots:
    void fodpRoundTrip();
    void fodpConvertsToPptx();
    void rendererPaints();
    void buildsHideFutureShapes();
    void shapeJsonCodecRoundTrips();
};

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
        title.paragraphs.append({{{QStringLiteral("Quarterly %1").arg(i + 1), true, false,
                                   QColor(Qt::darkBlue), 28, QString()}},
                                 int(Qt::AlignHCenter),
                                 false,
                                 0});
        slide.shapes.append(title);

        Shape body;
        body.kind = ShapeKind::Rectangle;
        body.rect = QRectF(60, 140, 280, 160);
        body.fill = QColor(200, 220, 255);
        body.stroke = QColor(Qt::black);
        body.strokeWidth = 1.5;
        body.paragraphs.append({{{QStringLiteral("bullet one"), false, false, QColor(), 18, QString()}},
                                0, true, 0});
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
    // ODF stores cm: allow sub-0.01pt drift on the round trip.
    QVERIFY(qAbs(back.slideSize.width() - deck.slideSize.width()) < 0.01);
    QVERIFY(qAbs(back.slideSize.height() - deck.slideSize.height()) < 0.01);
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(back.slides.at(i).title, deck.slides.at(i).title);
        QCOMPARE(back.slides.at(i).transition, deck.slides.at(i).transition);
        QCOMPARE(back.slides.at(i).notes, deck.slides.at(i).notes);
        QCOMPARE(back.slides.at(i).shapes.size(), deck.slides.at(i).shapes.size());
        QCOMPARE(back.slides.at(i).shapes.first().displayText(),
                 deck.slides.at(i).shapes.first().displayText());
        QCOMPARE(back.slides.at(i).shapes.at(1).buildStep,
                 deck.slides.at(i).shapes.at(1).buildStep);
        QVERIFY(back.slides.at(i).shapes.at(1).fill.isValid());
        QCOMPARE(back.slides.at(i).shapes.at(3).kind, ShapeKind::Image);
        QVERIFY(!back.slides.at(i).shapes.at(3).imagePng.isEmpty());
    }
    // Arrow geometry survived as a line with a marker.
    QCOMPARE(back.slides.at(0).shapes.at(2).kind, ShapeKind::Arrow);
}

void ShowModelTest::fodpConvertsToPptx()
{
    QProcess probe;
    probe.start(QStringLiteral("soffice"), {QStringLiteral("--headless"),
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
        soffice.start(QStringLiteral("soffice"),
                      {QStringLiteral("--headless"), QStringLiteral("--norestore"),
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

    // The pptx opens in Impress: convert it back to fodp and parse it.
    const QString back = convert(pptx, QStringLiteral("fodp"));
    if (!QFile::exists(back)) {
        QSKIP("LibreOffice pptx->fodp conversion unavailable here");
    }
    QFile file(back);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString error;
    const Presentation reparsed = readFodp(file.readAll(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(reparsed.slides.size(), 5);
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
    QCOMPARE(runBack.pointSize, 24);
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
