// SPDX-License-Identifier: GPL-3.0-or-later
// M75 (D-084) Slide Master gates over the hand-built PowerPoint package:
// masters and layouts are read with their placeholders and text styles;
// unedited ones are written back byte for byte; the master's title and
// body styles, a layout's placeholder positions, a layout placeholder's
// own look, the master's background and decorations are written where
// PowerPoint keeps them and every slide that inherits them follows once
// the deck is re-resolved; layouts are inserted, duplicated, renamed,
// preserved and deleted (not while a slide uses one), with Title and
// Footers switched and placeholders inserted; a master survives .fodp.

#include "fodp_reader.h"
#include "fodp_writer.h"
#include "master_edit.h"
#include "pptx_design.h"
#include "pptx_fixture_m75.h"
#include "pptx_origin.h"
#include "pptx_reader.h"
#include "pptx_rules.h"
#include "pptx_writer.h"
#include "slide_renderer.h"

#include "support/ooxml_schema.h"

#include <QGuiApplication>
#include <QtTest>

using namespace QindaOffice::Show;
namespace Ooxml = QindaOffice::Ooxml;

class PptxMastersTest : public QObject {
    Q_OBJECT
private slots:
    void mastersCarryPlaceholdersAndStyles();
    void uneditedMastersByteForByte();
    void titleStyleFlowsToSlides();
    void layoutPlaceholderFlowsToSlides();
    void layoutPlaceholderLookInItsListStyle();
    void layoutOperations();
    void masterDecorationsAndBackground();
    void masterSurvivesFodp();

private:
    static Presentation fixture();
    static Presentation written(const Presentation &deck);
};

namespace {

QByteArray part(const QByteArray &zip, const QString &name)
{
    const auto package = Ooxml::Package::fromData(zip);
    return package ? package->data(name) : QByteArray();
}

bool nearRect(const QRectF &a, const QRectF &b)
{
    return qAbs(a.x() - b.x()) < 0.01 && qAbs(a.y() - b.y()) < 0.01 && qAbs(a.width() - b.width()) < 0.01
        && qAbs(a.height() - b.height()) < 0.01;
}

const Shape *placeholderOf(const QList<Shape> &shapes, const QString &type)
{
    for (const Shape &shape : shapes) {
        if (shape.placeholder.type == type) {
            return &shape;
        }
    }
    return nullptr;
}

} // namespace

Presentation PptxMastersTest::fixture()
{
    return readPptxData(PptxFixture::build());
}

// The deck saved: PowerPoint's rules, the schemas, read back.
Presentation PptxMastersTest::written(const Presentation &deck)
{
    const QByteArray zip = writePptx(deck);
    const QStringList rules = PptxFixture::ruleViolations(zip);
    if (!rules.isEmpty()) {
        qWarning() << rules;
        return {};
    }
    if (QindaOffice::Test::schemaValidationAvailable()) {
        const QStringList errors = QindaOffice::Test::validatePackage(*Ooxml::Package::fromData(zip));
        if (!errors.isEmpty()) {
            qWarning() << errors;
            return {};
        }
    }
    return readPptxData(zip);
}

void PptxMastersTest::mastersCarryPlaceholdersAndStyles()
{
    const Presentation deck = fixture();
    const SlideMaster &master = deck.masters.first();
    QCOMPARE(master.placeholders.size(), 2); // title and body; the logo is a decoration
    QCOMPARE(master.shapes.size(), 1);
    QVERIFY(nearRect(placeholderOf(master.placeholders, QStringLiteral("title"))->rect, QRectF(36, 18, 888, 72)));
    QCOMPARE(placeholderOf(master.placeholders, QStringLiteral("body"))->displayText(), QStringLiteral("Master text"));
    // Nine levels a style; the level look as effective values.
    QCOMPARE(master.titleStyle.size(), 9);
    QCOMPARE(master.titleStyle.at(0).runs.first().pointSize, 44.0);
    QCOMPARE(master.titleStyle.at(0).runs.first().fontFamily, QStringLiteral("Calibri Light"));
    QCOMPARE(master.bodyStyle.at(0).runs.first().pointSize, 28.0);
    QCOMPARE(master.bodyStyle.at(1).runs.first().pointSize, 24.0);
    QCOMPARE(master.bodyStyle.at(1).bulletChar, QStringLiteral("–"));
    QCOMPARE(master.otherStyle.at(0).runs.first().pointSize, 18.0);
    // The layout: the title as the master has it, the body its own.
    const SlideLayout &layout = deck.layouts.first();
    QCOMPARE(layout.placeholders.size(), 2);
    QVERIFY(nearRect(placeholderOf(layout.placeholders, QStringLiteral("title"))->rect, QRectF(36, 18, 888, 72)));
    const Shape *body = placeholderOf(layout.placeholders, QStringLiteral("obj"));
    QVERIFY(body != nullptr);
    QVERIFY(nearRect(body->rect, QRectF(72, 144, 432, 288)));
    QVERIFY(layout.preserve);
    QCOMPARE(layout.origin->pristine, layout);
    QCOMPARE(master.origin->pristine, master);
}

void PptxMastersTest::uneditedMastersByteForByte()
{
    // A slide edit leaves the master, its layout and theme as they were.
    Presentation deck = fixture();
    deck.slides[0].shapes[2].rect.translate(10, 10);
    const QByteArray source = PptxFixture::build();
    const QByteArray zip = writePptx(deck);
    for (const char *name : {"ppt/slideMasters/slideMaster1.xml", "ppt/slideLayouts/slideLayout1.xml", "ppt/theme/theme1.xml"}) {
        QCOMPARE(part(zip, QLatin1String(name)), part(source, QLatin1String(name)));
    }
    // So does an edit of the master: its unedited layout stays byte for byte.
    QVERIFY(setStyleLevel(deck, deck.masters.first().id, StyleList::Title, 0,
                          [&] {
                              TextParagraph look = deck.masters.first().titleStyle.first();
                              look.runs.first().pointSize = 40;
                              return look;
                          }()));
    const QByteArray edited = writePptx(deck);
    QVERIFY(part(edited, QStringLiteral("ppt/slideMasters/slideMaster1.xml")) != part(source, QStringLiteral("ppt/slideMasters/slideMaster1.xml")));
    QCOMPARE(part(edited, QStringLiteral("ppt/slideLayouts/slideLayout1.xml")), part(source, QStringLiteral("ppt/slideLayouts/slideLayout1.xml")));
    QCOMPARE(part(edited, QStringLiteral("ppt/theme/theme1.xml")), part(source, QStringLiteral("ppt/theme/theme1.xml")));
}

void PptxMastersTest::titleStyleFlowsToSlides()
{
    Presentation deck = fixture();
    const QString masterId = deck.masters.first().id;
    TextParagraph look = deck.masters.first().titleStyle.first();
    look.runs.first().pointSize = 40;
    look.runs.first().color = QColor(0xC0, 0x00, 0x00);
    look.runs.first().bold = true;
    look.alignment = int(Qt::AlignHCenter);
    QVERIFY(setStyleLevel(deck, masterId, StyleList::Title, 0, look));
    QVERIFY(!setStyleLevel(deck, masterId, StyleList::Title, 0, look)); // unchanged
    TextParagraph second = deck.masters.first().bodyStyle.at(1);
    second.runs.first().pointSize = 22;
    second.bulletChar = QStringLiteral("»");
    QVERIFY(setStyleLevel(deck, masterId, StyleList::Body, 1, second));
    QString error;
    const Presentation flowed = reresolved(deck, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // Slide 1's title never chose a size, colour or alignment: it follows.
    const Shape &title = flowed.slides.at(0).shapes.at(0);
    const TextRun &run = title.paragraphs.first().runs.first();
    QCOMPARE(run.pointSize, 40.0);
    QCOMPARE(run.color, QColor(0xC0, 0x00, 0x00));
    QVERIFY(run.bold);
    QCOMPARE(title.paragraphs.first().alignment, int(Qt::AlignHCenter));
    QCOMPARE(flowed.masters.first().titleStyle.first().runs.first().pointSize, 40.0);
    // The body's second level is the layout's (20 pt at level 1 only): the
    // "Sub point" takes the master's new 22 pt and bullet.
    const TextParagraph &sub = flowed.slides.at(0).shapes.at(1).paragraphs.at(1);
    QCOMPARE(sub.runs.first().pointSize, 22.0);
    QCOMPARE(sub.bulletChar, QStringLiteral("»"));
    // Written where PowerPoint keeps it; the slide's title still spells
    // nothing of its own (it inherits).
    const QByteArray zip = writePptx(flowed);
    const QByteArray master = part(zip, QStringLiteral("ppt/slideMasters/slideMaster1.xml"));
    QVERIFY(master.contains(R"(sz="4000")"));
    QVERIFY(master.contains(R"(<a:srgbClr val="C00000"/>)"));
    QVERIFY(master.contains(QStringLiteral(R"(<a:buChar char="»"/>)").toUtf8()));
    QVERIFY(part(zip, QStringLiteral("ppt/slides/slide1.xml"))
                .contains(R"(<p:spPr/><p:txBody><a:bodyPr/><a:lstStyle/><a:p><a:r><a:rPr lang="en-US" dirty="0"/><a:t>Inherited title</a:t>)"));
    const Presentation back = written(flowed);
    QVERIFY(back.slides == flowed.slides);
    QVERIFY(back.masters == flowed.masters);
}

void PptxMastersTest::layoutPlaceholderFlowsToSlides()
{
    Presentation deck = fixture();
    const QString masterId = deck.masters.first().id;
    const QString layoutId = deck.layouts.first().id;
    // The Slide Master view moves the layout's body placeholder.
    Slide view = templateSlide(deck, masterId, layoutId);
    QCOMPARE(view.shapes.size(), 2);
    for (Shape &shape : view.shapes) {
        if (shape.placeholder.type == u"obj") {
            shape.rect = QRectF(480, 160, 400, 300);
        }
    }
    QVERIFY(applyTemplateSlide(deck, masterId, layoutId, view));
    QVERIFY(!applyTemplateSlide(deck, masterId, layoutId, view));
    QString error;
    const Presentation flowed = reresolved(deck, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // Slide 1's body spells no box: it moves with the layout.
    QVERIFY(nearRect(flowed.slides.at(0).shapes.at(1).rect, QRectF(480, 160, 400, 300)));
    const QByteArray zip = writePptx(flowed);
    QVERIFY(part(zip, QStringLiteral("ppt/slideLayouts/slideLayout1.xml")).contains(R"(<a:off x="6096000" y="2032000"/>)"));
    QCOMPARE(part(zip, QStringLiteral("ppt/slideMasters/slideMaster1.xml")).size() > 0, true);
    const Presentation back = written(flowed);
    QVERIFY(back.slides == flowed.slides);
    QVERIFY(back.layouts == flowed.layouts);
}

void PptxMastersTest::layoutPlaceholderLookInItsListStyle()
{
    Presentation deck = fixture();
    const QString masterId = deck.masters.first().id;
    const QString layoutId = deck.layouts.first().id;
    // The layout's title gets prompt text in italic blue (it had none).
    TextRun prompt = deck.masters.first().titleStyle.first().runs.first();
    prompt.text = QStringLiteral("Click to edit the title");
    prompt.italic = true;
    prompt.color = QColor(0x00, 0x70, 0xC0);
    Slide view = templateSlide(deck, masterId, layoutId);
    for (Shape &shape : view.shapes) {
        if (shape.placeholder.type == u"title") {
            shape.paragraphs = {TextParagraph{.runs = {prompt}}};
        }
    }
    QVERIFY(applyTemplateSlide(deck, masterId, layoutId, view));
    const Presentation flowed = reresolved(deck);
    // The slide's title follows its layout's title look, not its words.
    const Shape &title = flowed.slides.at(0).shapes.at(0);
    QCOMPARE(title.displayText(), QStringLiteral("Inherited title"));
    QVERIFY(title.paragraphs.first().runs.first().italic);
    QCOMPARE(title.paragraphs.first().runs.first().color, QColor(0x00, 0x70, 0xC0));
    // The look is in the layout placeholder's a:lstStyle; its prompt's runs
    // spell nothing but the language.
    const QByteArray layout = part(writePptx(flowed), QStringLiteral("ppt/slideLayouts/slideLayout1.xml"));
    const qsizetype from = layout.indexOf(R"(name="Title 1")");
    QVERIFY(from > 0);
    const QByteArray titleXml = layout.mid(from, layout.indexOf("</p:sp>", from) - from);
    const qsizetype list = titleXml.indexOf("<a:lstStyle>");
    const QByteArray level = titleXml.mid(list, titleXml.indexOf("</a:lstStyle>") - list);
    QVERIFY2(level.contains(R"(<a:lvl1pPr)"), titleXml.constData());
    QVERIFY(level.contains(R"(i="1")"));
    QVERIFY(level.contains(R"(<a:srgbClr val="0070C0"/>)"));
    QVERIFY(titleXml.contains(R"(<a:r><a:rPr lang="en-US"/><a:t>Click to edit the title</a:t></a:r>)"));
    QVERIFY(written(flowed).slides == flowed.slides);
}

void PptxMastersTest::layoutOperations()
{
    Presentation deck = fixture();
    const QString masterId = deck.masters.first().id;
    const QString used = deck.layouts.first().id;
    QVERIFY(!deleteLayout(deck, used)); // five slides use it
    const QString inserted = insertLayout(deck, masterId, QString());
    QCOMPARE(inserted, QStringLiteral("slideLayout2"));
    const QString copy = duplicateLayout(deck, used);
    QCOMPARE(copy, QStringLiteral("slideLayout3"));
    QCOMPARE(deck.layouts.at(1).id, copy); // right after the original
    QVERIFY(renameLayout(deck, inserted, QStringLiteral("Quote")));
    QVERIFY(setLayoutPreserved(deck, inserted, false));
    const SlideLayout *quote = deck.findLayout(inserted);
    QVERIFY(layoutHasTitle(*quote));
    QVERIFY(!layoutHasFooters(*quote)); // the fixture's master has none
    QVERIFY(setLayoutTitle(deck, inserted, false));
    QVERIFY(!layoutHasTitle(*deck.findLayout(inserted)));
    QVERIFY(setLayoutTitle(deck, inserted, true));
    const QString picture = insertPlaceholder(deck, inserted, QStringLiteral("pic"), QRectF(100, 120, 300, 200));
    QVERIFY(!picture.isEmpty());
    QCOMPARE(deck.findLayout(inserted)->placeholders.last().placeholder.index, qint64(13));
    QVERIFY(renameMaster(deck, masterId, QStringLiteral("Brand")));
    const Presentation back = written(reresolved(deck));
    QCOMPARE(back.layouts.size(), 3);
    QCOMPARE(back.layouts.at(0).id, used);
    QCOMPARE(back.layouts.at(1).id, copy);
    QCOMPARE(back.layouts.at(1).name, QStringLiteral("1_Title and Content"));
    QCOMPARE(back.layouts.at(1).placeholders.size(), 2);
    QCOMPARE(back.layouts.at(2).id, inserted); // the part keeps the layout's id
    QCOMPARE(back.layouts.at(2).name, QStringLiteral("Quote"));
    QVERIFY(!back.layouts.at(2).preserve);
    QCOMPARE(back.layouts.at(2).type, QStringLiteral("cust"));
    QCOMPARE(back.layouts.at(2).placeholders.size(), 2); // the title and the picture
    QCOMPARE(placeholderOf(back.layouts.at(2).placeholders, QStringLiteral("pic"))->rect, QRectF(100, 120, 300, 200));
    QCOMPARE(back.masters.first().name, QStringLiteral("Brand"));
    // An unused layout goes; the slides keep theirs.
    Presentation fewer = back;
    QVERIFY(deleteLayout(fewer, inserted));
    QCOMPARE(written(fewer).layouts.size(), 2);
}

void PptxMastersTest::masterDecorationsAndBackground()
{
    Presentation deck = fixture();
    const QString masterId = deck.masters.first().id;
    Slide view = templateSlide(deck, masterId, QString());
    Shape band;
    band.id = QStringLiteral("band");
    band.kind = ShapeKind::Rectangle;
    band.rect = QRectF(0, 520, 960, 20);
    band.fill = QColor(0x1F, 0x3A, 0x5F);
    band.strokeWidth = 0;
    view.shapes.prepend(band); // behind everything
    Shape logo;
    logo.id = QStringLiteral("logo");
    logo.kind = ShapeKind::Image;
    logo.rect = QRectF(20, 490, 40, 20);
    logo.imagePng = PptxFixture::pngBytes(QColor(250, 200, 0));
    logo.image.mime = QStringLiteral("image/png");
    view.shapes.append(logo);
    QVERIFY(applyTemplateSlide(deck, masterId, QString(), view));
    deck.masters.first().background = QColor(0xF4, 0xF1, 0xEA);
    const Presentation flowed = reresolved(deck);
    QCOMPARE(flowed.masters.first().shapes.size(), 3);
    // Every slide draws the band and the new background.
    for (const Slide &slide : flowed.slides) {
        const QImage image = renderSlideToImage(flowed, slide, QSize(960, 540));
        QCOMPARE(image.pixelColor(480, 530), QColor(0x1F, 0x3A, 0x5F));
        QCOMPARE(image.pixelColor(2, 2), QColor(0xF4, 0xF1, 0xEA));
    }
    const QByteArray zip = writePptx(flowed);
    const QByteArray master = part(zip, QStringLiteral("ppt/slideMasters/slideMaster1.xml"));
    // The band first (behind the placeholders), the picture with its media.
    QVERIFY(master.indexOf("1F3A5F") < master.indexOf("Title Placeholder 1"));
    QVERIFY(master.contains("<p:pic>"));
    const Presentation back = written(flowed);
    QVERIFY(back.masters == flowed.masters);
}

void PptxMastersTest::masterSurvivesFodp()
{
    // Through .fodp the master keeps its theme, styles and placeholders, and
    // a later .pptx save generates a master of them.
    Presentation deck = themed(fixture(), builtInThemes().at(3), ThemePart::Whole);
    const Presentation viaFodp = readFodp(writeFodp(deck));
    QCOMPARE(viaFodp.masters.first().theme.name, QStringLiteral("Ember"));
    QCOMPARE(viaFodp.masters.first().titleStyle, deck.masters.first().titleStyle);
    QCOMPARE(viaFodp.masters.first().placeholders.size(), 2);
    QCOMPARE(viaFodp.masters.first().placeholders.first().placeholder.type, QStringLiteral("title"));
    QCOMPARE(viaFodp.layouts.first().placeholders.size(), 2);
    QCOMPARE(viaFodp.layouts.first().preserve, deck.layouts.first().preserve);
    const Presentation back = written(viaFodp);
    QCOMPARE(back.masters.first().theme.name, QStringLiteral("Ember"));
    QCOMPARE(back.masters.first().titleStyle.first().runs.first().pointSize, 44.0);
    QCOMPARE(back.masters.first().placeholders.size(), 2);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    PptxMastersTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pptx_masters.moc"
