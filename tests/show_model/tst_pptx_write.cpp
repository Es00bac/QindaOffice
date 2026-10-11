// SPDX-License-Identifier: GPL-3.0-or-later
// Native .pptx writer gates (M62 step 4, D-070): pptx → model → pptx →
// model is equal; the unknown graphic frame comes back byte for byte;
// edits are written (and only they change); a deck QindaShow made is
// written with a generated master, layouts, theme and notes master; every
// package validates against the ECMA-376 Transitional schemas
// (QQO_OOXML_SCHEMA_DIR) and keeps PowerPoint's own rules (ids, layout and
// notes relationships, content types); LibreOffice converts what we write
// to PDF with one page per shown slide.

#include "fodp_reader.h"
#include "fodp_writer.h"
#include "pptx_fixture_slides.h"
#include "pptx_rules.h"
#include "pptx_reader.h"
#include "pptx_writer.h"
#include "presentation.h"

#include "ooxml_package.h"
#include "ooxml_xml.h"
#include "support/ooxml_schema.h"
#include "support/soffice_env.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>

using namespace QindaOffice::Show;
namespace Ooxml = QindaOffice::Ooxml;

class PptxWriteTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripIsEqual();
    void unknownFrameComesBackByteForByte();
    void editsAreWrittenAndTheRestKept();
    void generatedDeckReadsBack();
    void packagesValidate();
    void powerPointRules();
    void libreOfficeConvertsToPdf();

private:
    static Presentation fixture();
    static Presentation generated();
    static QStringList ruleViolations(const QByteArray &zip);
};

Presentation PptxWriteTest::fixture()
{
    QString error;
    Presentation deck = readPptxData(PptxFixture::build(), &error);
    if (!error.isEmpty()) {
        qWarning() << error;
    }
    return deck;
}

Presentation PptxWriteTest::generated()
{
    Presentation deck;
    deck.title = QStringLiteral("Made in QindaShow");
    SlideMaster master;
    master.id = QStringLiteral("dark");
    master.name = QStringLiteral("Dark");
    master.background = QColor(20, 30, 50);
    master.bodyColor = QColor(230, 230, 230);
    deck.masters.append(master);
    Slide &one = deck.addSlide(QStringLiteral("Opening"));
    one.master = master.id;
    one.layout = QStringLiteral("title");
    one.notes = QStringLiteral("Say hello\nthen begin");
    Shape title;
    title.id = QStringLiteral("t");
    title.kind = ShapeKind::TextBox;
    title.rect = QRectF(72, 100, 576, 80);
    title.strokeWidth = 0;
    TextRun run;
    run.text = QStringLiteral("Hello & welcome");
    run.pointSize = 40;
    run.bold = true;
    run.color = QColor(250, 200, 40);
    TextParagraph paragraph;
    paragraph.alignment = int(Qt::AlignHCenter);
    paragraph.runs = {run};
    title.paragraphs = {paragraph};
    one.shapes.append(title);
    Shape box;
    box.id = QStringLiteral("b");
    box.kind = ShapeKind::Rectangle;
    box.rect = QRectF(100, 220, 200, 100);
    box.fill = QColor(40, 120, 200);
    box.stroke = QColor(Qt::white);
    box.strokeWidth = 2;
    box.rotation = 15;
    box.buildStep = 1;
    box.animation = ShapeAnimation::Fade;
    one.shapes.append(box);
    Shape arrow;
    arrow.id = QStringLiteral("a");
    arrow.kind = ShapeKind::Arrow;
    arrow.rect = QRectF(600, 300, -150, -60); // pointing back up-left
    arrow.stroke = QColor(Qt::red);
    arrow.strokeWidth = 3;
    one.shapes.append(arrow);
    Slide &two = deck.addSlide(QStringLiteral("Detail"));
    two.transition = QStringLiteral("slide");
    Shape picture;
    picture.id = QStringLiteral("p");
    picture.kind = ShapeKind::Image;
    picture.rect = QRectF(50, 50, 120, 60);
    picture.imagePng = PptxFixture::pngBytes(QColor(10, 200, 90));
    picture.image.cropLeft = 0.25;
    two.shapes.append(picture);
    Shape bullets;
    bullets.id = QStringLiteral("l");
    bullets.kind = ShapeKind::TextBox;
    bullets.rect = QRectF(200, 50, 400, 200);
    for (const QString &text : {QStringLiteral("One"), QStringLiteral("Two")}) {
        TextParagraph p;
        p.bullet = true;
        TextRun r;
        r.text = text;
        p.runs = {r};
        bullets.paragraphs.append(p);
    }
    two.shapes.append(bullets);
    Shape table;
    table.id = QStringLiteral("tbl");
    table.kind = ShapeKind::Table;
    table.rect = QRectF(100, 280, 300, 60);
    table.table.columns = {150, 150};
    table.table.firstRow = true;
    for (int r = 0; r < 2; ++r) {
        TableRow row;
        row.height = 30;
        for (int c = 0; c < 2; ++c) {
            TableCell cell;
            TextRun text;
            text.text = QStringLiteral("r%1c%2").arg(r).arg(c);
            TextParagraph p;
            p.runs = {text};
            cell.paragraphs = {p};
            cell.fill = r == 0 ? QColor(68, 114, 196) : QColor();
            row.cells.append(cell);
        }
        table.table.rows.append(row);
    }
    two.shapes.append(table);
    Slide &three = deck.addSlide(QStringLiteral("Plain"));
    three.layout = QStringLiteral("blank");
    three.hidden = true;
    return deck;
}

QStringList PptxWriteTest::ruleViolations(const QByteArray &zip)
{
    return PptxFixture::ruleViolations(zip);
}

void PptxWriteTest::roundTripIsEqual()
{
    const Presentation first = fixture();
    QCOMPARE(first.slides.size(), 5);
    QString error;
    const QByteArray zip = writePptx(first, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    const Presentation second = readPptxData(zip, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(second.slides.size(), first.slides.size());
    for (qsizetype i = 0; i < first.slides.size(); ++i) {
        const Slide &a = first.slides.at(i);
        const Slide &b = second.slides.at(i);
        QCOMPARE(b.shapes.size(), a.shapes.size());
        for (qsizetype s = 0; s < a.shapes.size(); ++s) {
            QCOMPARE(b.shapes.at(s).rect, a.shapes.at(s).rect);
            QCOMPARE(b.shapes.at(s).paragraphs, a.shapes.at(s).paragraphs);
            QVERIFY2(b.shapes.at(s) == a.shapes.at(s), qPrintable(QStringLiteral("slide %1 shape %2").arg(i + 1).arg(s)));
        }
        QCOMPARE(b.notes, a.notes);
        QCOMPARE(b.transition, a.transition);
        QCOMPARE(b.hidden, a.hidden);
        QCOMPARE(b, a);
    }
    QCOMPARE(second.masters, first.masters);
    QCOMPARE(second.layouts, first.layouts);
    QCOMPARE(second, first);
    // And once more from the copy: still the same deck.
    QCOMPARE(readPptxData(writePptx(second)), first);
}

void PptxWriteTest::unknownFrameComesBackByteForByte()
{
    const Presentation first = fixture();
    const QByteArray zip = writePptx(first);
    const auto package = Ooxml::Package::fromData(zip);
    QVERIFY(package);
    const auto slide = package->xml(QStringLiteral("ppt/slides/slide5.xml"));
    QVERIFY(slide);
    const Ooxml::XmlElement *frame = nullptr;
    const std::function<void(const Ooxml::XmlElement &)> find = [&](const Ooxml::XmlElement &e) {
        for (const auto *c : e.elements()) {
            if (frame == nullptr && c->name == u"graphicFrame") {
                frame = c;
            }
            find(*c);
        }
    };
    find(*slide);
    QVERIFY(frame != nullptr);
    QCOMPARE(Ooxml::serializeFragment(*frame), first.slides.at(4).shapes.at(0).preserved.fragment);
}

void PptxWriteTest::editsAreWrittenAndTheRestKept()
{
    Presentation deck = fixture();
    Slide &one = deck.slides[0];
    // Move the text box, retitle the slide, recolour the master logo's
    // neighbour, delete the built shape's partner, add a slide.
    one.shapes[0].paragraphs[0].runs[0].text = QStringLiteral("A new title");
    one.shapes[2].rect.translate(-100, 50);
    Slide &three = deck.slides[2];
    three.shapes[1].children[0].fill = QColor(Qt::magenta);
    deck.slides[1].shapes[0].table.rows[1].cells[0].paragraphs[0].runs[0].text = QStringLiteral("edited cell");
    deck.slides.removeAt(3);
    Slide &added = deck.addSlide(QStringLiteral("Added"));
    Shape note;
    note.id = QStringLiteral("n");
    note.kind = ShapeKind::TextBox;
    note.rect = QRectF(100, 100, 300, 50);
    TextRun run;
    run.text = QStringLiteral("added text");
    note.paragraphs = {TextParagraph{.runs = {run}}};
    added.shapes.append(note);
    QString error;
    const QByteArray zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    QVERIFY2(ruleViolations(zip).isEmpty(), qPrintable(ruleViolations(zip).join(QLatin1Char('\n'))));
    const Presentation back = readPptxData(zip, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.slides.size(), 5);
    const Shape &title = back.slides.at(0).shapes.at(0);
    QCOMPARE(title.displayText(), QStringLiteral("A new title"));
    QCOMPARE(title.placeholder.type, QStringLiteral("title")); // still a placeholder…
    QCOMPARE(title.paragraphs.first().runs.first().pointSize, 44.0); // …inheriting its look
    QCOMPARE(back.slides.at(0).shapes.at(2).rect, one.shapes.at(2).rect);
    QCOMPARE(back.slides.at(0).shapes.at(2).buildStep, 1); // the build stays
    QCOMPARE(back.slides.at(0).shapes.at(1), deck.slides.at(0).shapes.at(1)); // untouched
    QCOMPARE(back.slides.at(2).shapes.at(1).children[0].fill, QColor(Qt::magenta));
    QCOMPARE(back.slides.at(2).shapes.at(1).children[1], deck.slides.at(2).shapes.at(1).children[1]);
    QCOMPARE(back.slides.at(1).shapes.at(0).table.rows.at(1).cells.at(0).paragraphs.first().runs.first().text,
             QStringLiteral("edited cell"));
    QCOMPARE(back.slides.at(1).shapes.at(0).table.rows.at(1).cells.at(1).fill, QColor(Qt::yellow));
    // The hidden slide with the unknown frame, untouched.
    const Slide &hiddenBack = back.slides.at(3);
    const Slide &hiddenWas = deck.slides.at(3);
    QCOMPARE(hiddenBack.id, hiddenWas.id);
    QCOMPARE(hiddenBack.title, hiddenWas.title);
    QCOMPARE(hiddenBack.layout, hiddenWas.layout);
    QCOMPARE(hiddenBack.layoutId, hiddenWas.layoutId);
    QCOMPARE(hiddenBack.master, hiddenWas.master);
    QCOMPARE(hiddenBack.transition, hiddenWas.transition);
    QCOMPARE(hiddenBack.shapes.size(), hiddenWas.shapes.size());
    for (qsizetype s = 0; s < hiddenWas.shapes.size(); ++s) {
        const Shape &a = hiddenBack.shapes.at(s);
        const Shape &b = hiddenWas.shapes.at(s);
        QCOMPARE(a.id, b.id);
        QCOMPARE(a.click, b.click);
        QCOMPARE(a.rect, b.rect);
        QCOMPARE(a.paragraphs, b.paragraphs);
        QVERIFY2(a == b, qPrintable(QString::number(s)));
    }
    QCOMPARE(hiddenBack, hiddenWas);
    QCOMPARE(back.slides.at(4).shapes.first().displayText(), QStringLiteral("added text"));
    QCOMPARE(back.slides.at(0).notes, deck.slides.at(0).notes);
}

void PptxWriteTest::generatedDeckReadsBack()
{
    const Presentation deck = generated();
    QString error;
    const QByteArray zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    QVERIFY2(ruleViolations(zip).isEmpty(), qPrintable(ruleViolations(zip).join(QLatin1Char('\n'))));
    const Presentation back = readPptxData(zip, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.title, deck.title);
    QCOMPARE(back.slideSize, deck.slideSize);
    QCOMPARE(back.slides.size(), 3);
    QCOMPARE(back.masters.size(), 2); // the dark one and the plain one
    QCOMPARE(back.masters.first().background, QColor(20, 30, 50));
    const Slide &one = back.slides.at(0);
    QCOMPARE(one.layout, QStringLiteral("title"));
    QCOMPARE(one.notes, QStringLiteral("Say hello\nthen begin"));
    const TextRun &title = one.shapes.at(0).paragraphs.first().runs.first();
    QCOMPARE(title.text, QStringLiteral("Hello & welcome"));
    QCOMPARE(title.pointSize, 40.0);
    QVERIFY(title.bold);
    QCOMPARE(title.color, QColor(250, 200, 40));
    QCOMPARE(one.shapes.at(0).paragraphs.first().alignment, int(Qt::AlignHCenter));
    const Shape &box = one.shapes.at(1);
    QCOMPARE(box.kind, ShapeKind::Rectangle);
    QCOMPARE(box.fill, QColor(40, 120, 200));
    QCOMPARE(box.rotation, 15.0);
    QCOMPARE(box.buildStep, 1);
    QCOMPARE(box.animation, ShapeAnimation::Fade);
    const Shape &arrow = one.shapes.at(2);
    QCOMPARE(arrow.kind, ShapeKind::Arrow);
    QCOMPARE(arrow.rect, QRectF(450, 240, 150, 60));
    QVERIFY(arrow.flipH && arrow.flipV); // the direction survives as flips
    QCOMPARE(arrow.line.tail.type, QStringLiteral("triangle"));
    const Slide &two = back.slides.at(1);
    QCOMPARE(two.transition, QStringLiteral("slide"));
    QCOMPARE(two.shapes.at(0).kind, ShapeKind::Image);
    QCOMPARE(two.shapes.at(0).imagePng, deck.slides.at(1).shapes.at(0).imagePng);
    QCOMPARE(two.shapes.at(0).image.cropLeft, 0.25);
    QVERIFY(two.shapes.at(1).paragraphs.at(1).bullet);
    QCOMPARE(two.shapes.at(2).kind, ShapeKind::Table);
    QCOMPARE(two.shapes.at(2).table.rows.at(1).cells.at(1).paragraphs.first().runs.first().text, QStringLiteral("r1c1"));
    QCOMPARE(two.shapes.at(2).table.rows.at(0).cells.at(0).fill, QColor(68, 114, 196));
    QVERIFY(back.slides.at(2).hidden);
    QCOMPARE(back.slides.at(2).layout, QStringLiteral("blank"));
}

void PptxWriteTest::packagesValidate()
{
    if (!QindaOffice::Test::schemaValidationAvailable()) {
        QSKIP("QQO_OOXML_SCHEMA_DIR / xmllint not available");
    }
    Presentation edited = fixture();
    edited.slides[0].shapes[2].rect.translate(10, 10);
    edited.slides[0].shapes[2].paragraphs[1].runs[0].text = QStringLiteral("changed");
    const QList<std::pair<const char *, QByteArray>> packages{
        {"round trip", writePptx(fixture())}, {"edited", writePptx(edited)}, {"generated", writePptx(generated())},
        {"through .fodp", writePptx(readFodp(writeFodp(fixture())))}};
    for (const auto &[name, zip] : packages) {
        const auto package = Ooxml::Package::fromData(zip);
        QVERIFY(package);
        const QStringList errors = QindaOffice::Test::validatePackage(*package);
        QVERIFY2(errors.isEmpty(), qPrintable(QLatin1String(name) + QStringLiteral(":\n") + errors.join(QLatin1Char('\n'))));
    }
}

void PptxWriteTest::powerPointRules()
{
    QVERIFY2(ruleViolations(writePptx(fixture())).isEmpty(), qPrintable(ruleViolations(writePptx(fixture())).join(QLatin1Char('\n'))));
    QVERIFY(ruleViolations(writePptx(Presentation())).isEmpty()); // even an empty deck
    const QByteArray throughFodp = writePptx(readFodp(writeFodp(fixture())));
    QVERIFY2(ruleViolations(throughFodp).isEmpty(), qPrintable(ruleViolations(throughFodp).join(QLatin1Char('\n'))));
}

void PptxWriteTest::libreOfficeConvertsToPdf()
{
    QProcess probe;
    QindaOffice::Test::startSoffice(probe, {QStringLiteral("--headless"), QStringLiteral("--version")});
    if (!probe.waitForFinished(15000) || probe.exitCode() != 0) {
        QSKIP("LibreOffice unavailable in this environment");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // The deck as .pptx, a generated one, and the deck as .fodp (D-070 7).
    const QList<std::tuple<QString, QByteArray, int>> decks{
        {QStringLiteral("fixture.pptx"), writePptx(fixture()), 4},
        {QStringLiteral("generated.pptx"), writePptx(generated()), 2},
        {QStringLiteral("fixture.fodp"), writeFodp(fixture()), 4}};
    for (const auto &[file, bytes, shown] : decks) {
        const QString path = dir.filePath(file);
        const QString name = QFileInfo(file).completeBaseName() + QLatin1Char('-') + QFileInfo(file).suffix();
        QVERIFY(QDir().mkpath(dir.filePath(name)));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size());
        out.close();
        QProcess soffice;
        QindaOffice::Test::startSoffice(soffice, {QStringLiteral("--headless"), QStringLiteral("--norestore"),
                                                  QStringLiteral("-env:UserInstallation=file://%1/profile").arg(dir.path()),
                                                  QStringLiteral("--convert-to"), QStringLiteral("pdf"),
                                                  QStringLiteral("--outdir"), dir.filePath(name), path});
        QVERIFY(soffice.waitForFinished(180000));
        QFile pdf(dir.filePath(name + QLatin1Char('/') + QFileInfo(file).completeBaseName() + QStringLiteral(".pdf")));
        QVERIFY2(pdf.open(QIODevice::ReadOnly), qPrintable(QString::fromUtf8(soffice.readAllStandardError())));
        const QByteArray pdfBytes = pdf.readAll();
        static const QRegularExpression kPage(QStringLiteral("/Type\\s*/Page[^s]"));
        int pages = 0;
        for (auto it = kPage.globalMatch(QString::fromLatin1(pdfBytes)); it.hasNext(); it.next()) {
            ++pages;
        }
        // LibreOffice leaves hidden slides out of a PDF, as PowerPoint does.
        QVERIFY2(pages == shown, qPrintable(QStringLiteral("%1: %2 pages").arg(file).arg(pages)));
    }
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    PptxWriteTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pptx_write.moc"
