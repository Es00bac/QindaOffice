// SPDX-License-Identifier: GPL-3.0-or-later
// M75 (D-084) Design gates over the hand-built PowerPoint package
// (pptx_fixture*.h): the theme is read; Design ▸ Themes and Variants
// (colours, fonts, effects, a whole theme, a theme from another file)
// re-resolve every slide through the reader while the master and layouts
// stay byte for byte and the theme part is written as PowerPoint writes
// one; Slide Size scales every slide's, layout's and master's content
// (Ensure Fit, Maximize) with placeholders still inheriting; Format
// Background writes solid, gradient, picture-with-transparency and
// texture backgrounds on one slide or, Apply to All, on the master; a deck
// QindaShow made takes a theme. Every package validates
// (QQO_OOXML_SCHEMA_DIR), keeps PowerPoint's rules and reads back equal.

#include "pptx_design.h"
#include "pptx_fixture_m75.h"
#include "pptx_origin.h"
#include "pptx_reader.h"
#include "pptx_rules.h"
#include "pptx_writer.h"
#include "show_theme.h"
#include "slide_renderer.h"

#include "ooxml_theme.h"
#include "support/ooxml_schema.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Show;
namespace Ooxml = QindaOffice::Ooxml;

class PptxDesignTest : public QObject {
    Q_OBJECT
private slots:
    void themeIsRead();
    void reresolutionIsStable();
    void themeColorsFlowToSlides();
    void themeFontsFlowToSlides();
    void wholeThemeWrittenAsPowerPointDoes();
    void themeFromAnotherFile();
    void resizeAsksAsPowerPointDoes();
    void slideSizeEnsureFit();
    void slideSizeMaximize();
    void backgroundsWritten();
    void generatedDeckTakesATheme();

private:
    static Presentation fixture();
    static void checkPackage(const QByteArray &zip, const Presentation &expected);
};

namespace {

QColor resolved(const char *xml, const ThemeColors &colors)
{
    Ooxml::Theme theme;
    for (size_t i = 0; i < colors.values.size(); ++i) {
        theme.colors.insert(QLatin1String(themeSlotNames().at(i)), colors.values.at(i));
    }
    const Ooxml::XmlElementPtr e = Ooxml::parseXml(
        QByteArray(R"(<a:holder xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main">)") + xml + "</a:holder>");
    return QColor::fromRgba(Ooxml::resolveChildColor(e.get(), theme).rgba());
}

QByteArray part(const QByteArray &zip, const QString &name)
{
    const auto package = Ooxml::Package::fromData(zip);
    return package ? package->data(name) : QByteArray();
}

bool near(qreal a, qreal b)
{
    return qAbs(a - b) < 0.01;
}

bool nearRect(const QRectF &a, const QRectF &b)
{
    return near(a.x(), b.x()) && near(a.y(), b.y()) && near(a.width(), b.width()) && near(a.height(), b.height());
}

} // namespace

Presentation PptxDesignTest::fixture()
{
    QString error;
    Presentation deck = readPptxData(PptxFixture::build(), &error);
    if (!error.isEmpty()) {
        qWarning() << error;
    }
    return deck;
}

// Written, the package keeps PowerPoint's rules, validates and reads back
// as `expected` (the re-resolved deck).
void PptxDesignTest::checkPackage(const QByteArray &zip, const Presentation &expected)
{
    QVERIFY(!zip.isEmpty());
    const QStringList rules = PptxFixture::ruleViolations(zip);
    QVERIFY2(rules.isEmpty(), qPrintable(rules.join(QLatin1Char('\n'))));
    if (QindaOffice::Test::schemaValidationAvailable()) {
        const QStringList errors = QindaOffice::Test::validatePackage(*Ooxml::Package::fromData(zip));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
    }
    QString error;
    const Presentation back = readPptxData(zip, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.slides.size(), expected.slides.size());
    for (qsizetype i = 0; i < expected.slides.size(); ++i) {
        QVERIFY2(back.slides.at(i).shapes == expected.slides.at(i).shapes, qPrintable(QStringLiteral("slide %1").arg(i + 1)));
        QCOMPARE(back.slides.at(i).background, expected.slides.at(i).background);
    }
    QCOMPARE(back.masters, expected.masters);
    QCOMPARE(back.layouts, expected.layouts);
    QCOMPARE(back.slideSize, expected.slideSize);
}

void PptxDesignTest::themeIsRead()
{
    const Presentation deck = fixture();
    QCOMPARE(deck.masters.size(), 1);
    const ThemeSpec &theme = deck.masters.first().theme;
    QCOMPARE(theme.name, QStringLiteral("Fixture Theme"));
    QCOMPARE(theme.colors.name, QStringLiteral("Office"));
    QCOMPARE(theme.colors.slot(QStringLiteral("accent1")), QColor(0x44, 0x72, 0xC4));
    QCOMPARE(theme.colors.slot(QStringLiteral("dk1")), QColor(Qt::black)); // through sysClr lastClr
    QCOMPARE(theme.fonts.major, QStringLiteral("Calibri Light"));
    QCOMPARE(theme.fonts.minor, QStringLiteral("Calibri"));
    QCOMPARE(theme.effects, QStringLiteral("Office"));
    QVERIFY(theme.part.isEmpty()); // the master's own part
    QCOMPARE(deck.masters.first().origin->themePart, QStringLiteral("ppt/theme/theme1.xml"));
    QCOMPARE(deckTheme(deck).name, QStringLiteral("Fixture Theme"));
    QCOMPARE(deckTheme(Presentation()).name, QStringLiteral("Office Theme")); // QindaShow's own deck
}

void PptxDesignTest::reresolutionIsStable()
{
    // An unedited deck re-resolved is the same deck.
    const Presentation deck = fixture();
    QString error;
    const Presentation again = reresolved(deck, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(again.slides == deck.slides);
    QVERIFY(again.masters == deck.masters);
    QVERIFY(again.layouts == deck.layouts);
    QVERIFY(again.origin.get() != deck.origin.get()); // the package it was read back from
}

void PptxDesignTest::themeColorsFlowToSlides()
{
    const Presentation deck = fixture();
    const ThemeColors harbor = builtInColorSets().at(1);
    QCOMPARE(harbor.name, QStringLiteral("Harbor"));
    QString error;
    const Presentation themedDeck = themed(deck, ThemeSpec{QString(), harbor, {}, QString(), {}}, ThemePart::Colors, QString(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(themedDeck.masters.first().theme.colors, harbor);
    QCOMPARE(themedDeck.masters.first().theme.fonts, deck.masters.first().theme.fonts); // fonts kept
    // The text box's accent1 at 75 % luminance follows the theme…
    const TextRun &accent = themedDeck.slides.at(0).shapes.at(2).paragraphs.at(0).runs.at(0);
    QCOMPARE(accent.color, resolved(R"(<a:schemeClr val="accent1"><a:lumMod val="75000"/></a:schemeClr>)", harbor));
    QVERIFY(accent.color != deck.slides.at(0).shapes.at(2).paragraphs.at(0).runs.at(0).color);
    // … so does the group's accent1 oval, the master's accent6 logo; an
    // explicit red does not.
    const Shape &group = themedDeck.slides.at(2).shapes.at(1);
    QCOMPARE(group.children.at(0).fill, QColor(0x1F, 0x6F, 0xB2));
    QCOMPARE(group.children.at(1).children.at(0).fill, QColor(Qt::red));
    QCOMPARE(themedDeck.masters.first().shapes.first().fill, harbor.slot(QStringLiteral("accent6")));
    // Written: the master and layout byte for byte, the theme part patched.
    const QByteArray source = PptxFixture::build();
    const QByteArray zip = writePptx(themedDeck);
    QCOMPARE(part(zip, QStringLiteral("ppt/slideMasters/slideMaster1.xml")), part(source, QStringLiteral("ppt/slideMasters/slideMaster1.xml")));
    QCOMPARE(part(zip, QStringLiteral("ppt/slideLayouts/slideLayout1.xml")), part(source, QStringLiteral("ppt/slideLayouts/slideLayout1.xml")));
    const QByteArray theme = part(zip, QStringLiteral("ppt/theme/theme1.xml"));
    QVERIFY(theme.contains(R"(<a:clrScheme name="Harbor">)"));
    QVERIFY(theme.contains(R"(<a:accent1><a:srgbClr val="1F6FB2"/></a:accent1>)"));
    QVERIFY(theme.contains(R"(name="Fixture Theme")")); // the theme keeps its name
    checkPackage(zip, themedDeck);
}

void PptxDesignTest::themeFontsFlowToSlides()
{
    const Presentation deck = fixture();
    const ThemeFonts bookish = builtInFontSets().at(3);
    QCOMPARE(bookish.major, QStringLiteral("Georgia"));
    QString error;
    const Presentation themedDeck = themed(deck, ThemeSpec{QString(), {}, bookish, QString(), {}}, ThemePart::Fonts, QString(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // The title (+mj-lt), the body and the text box (+mn-lt) all follow.
    const Slide &one = themedDeck.slides.at(0);
    QCOMPARE(one.shapes.at(0).paragraphs.at(0).runs.at(0).fontFamily, QStringLiteral("Georgia"));
    QCOMPARE(one.shapes.at(1).paragraphs.at(0).runs.at(0).fontFamily, QStringLiteral("Georgia"));
    QCOMPARE(one.shapes.at(2).paragraphs.at(0).runs.at(0).fontFamily, QStringLiteral("Georgia"));
    QCOMPARE(themedDeck.masters.first().titleStyle.first().runs.first().fontFamily, QStringLiteral("Georgia"));
    const QByteArray zip = writePptx(themedDeck);
    QVERIFY(part(zip, QStringLiteral("ppt/theme/theme1.xml")).contains(R"(<a:latin typeface="Georgia"/>)"));
    checkPackage(zip, themedDeck);
}

void PptxDesignTest::wholeThemeWrittenAsPowerPointDoes()
{
    const Presentation deck = fixture();
    const ThemeSpec meadow = builtInThemes().at(2);
    QCOMPARE(meadow.name, QStringLiteral("Meadow"));
    QString error;
    const Presentation themedDeck = themed(deck, meadow, ThemePart::Whole, QString(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const ThemeSpec &now = themedDeck.masters.first().theme;
    QCOMPARE(now.name, QStringLiteral("Meadow"));
    QCOMPARE(now.colors, meadow.colors);
    QCOMPARE(now.fonts, meadow.fonts);
    QCOMPARE(now.effects, QStringLiteral("Soft Shadow"));
    const QByteArray zip = writePptx(themedDeck);
    const QByteArray theme = part(zip, QStringLiteral("ppt/theme/theme1.xml"));
    for (const char *piece : {R"(<a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="Meadow">)",
                              R"(<a:fontScheme name="Friendly"><a:majorFont><a:latin typeface="Trebuchet MS"/>)",
                              R"(<a:fmtScheme name="Soft Shadow">)", "<a:outerShdw blurRad=\"76200\"", "<a:objectDefaults/>",
                              "<a:extraClrSchemeLst/>", R"(<thm15:themeFamily xmlns:thm15="http://schemas.microsoft.com/office/thememl/2012/main" name="Meadow")"}) {
        QVERIFY2(theme.contains(piece), piece);
    }
    // The Office theme spells Office's script fonts and family ids.
    const QByteArray office = themePartXml(officeTheme());
    QVERIFY(office.contains(R"(<a:latin typeface="Calibri Light" panose="020F0302020204030204"/>)"));
    QVERIFY(office.contains(QStringLiteral(R"(<a:font script="Jpan" typeface="游ゴシック Light"/>)").toUtf8()));
    QVERIFY(office.contains("{62F939B6-93AF-4DB8-9C6B-D6C7DFDC589F}"));
    QCOMPARE(themeFromPart(office).colors, officeTheme().colors);
    checkPackage(zip, themedDeck);
}

void PptxDesignTest::themeFromAnotherFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // A deck with the Ember theme, saved as a template…
    QString error;
    const Presentation ember = themed(fixture(), builtInThemes().at(3), ThemePart::Whole, QString(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QString potx = dir.filePath(QStringLiteral("ember.potx"));
    QVERIFY(writePptxFile(ember, potx, &error));
    const std::optional<ThemeSpec> fromDeck = themeFromFile(potx, &error);
    QVERIFY2(fromDeck.has_value(), qPrintable(error));
    QCOMPARE(fromDeck->name, QStringLiteral("Ember"));
    QVERIFY(!fromDeck->part.isEmpty());
    // … and a .thmx (a theme package of its own).
    Ooxml::PackageWriter thmx;
    thmx.addPart(QStringLiteral("theme/theme/theme1.xml"), themePartXml(builtInThemes().at(4)),
                 QStringLiteral("application/vnd.openxmlformats-officedocument.theme+xml"));
    thmx.addRelationship(QString(), QString::fromUtf16(Ooxml::RelType::OfficeDocument), QStringLiteral("theme/theme/theme1.xml"));
    QVERIFY(thmx.save(dir.filePath(QStringLiteral("slate.thmx"))));
    const std::optional<ThemeSpec> fromTheme = themeFromFile(dir.filePath(QStringLiteral("slate.thmx")), &error);
    QVERIFY2(fromTheme.has_value(), qPrintable(error));
    QCOMPARE(fromTheme->name, QStringLiteral("Slate"));
    QVERIFY(!themeFromFile(dir.filePath(QStringLiteral("missing.pptx")), &error).has_value());
    // Applied: the file's theme part, as it came.
    error.clear();
    const Presentation applied = themed(fixture(), *fromDeck, ThemePart::Whole, QString(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(applied.masters.first().theme.colors, builtInThemes().at(3).colors);
    const QByteArray zip = writePptx(applied);
    QCOMPARE(part(zip, QStringLiteral("ppt/theme/theme1.xml")), fromDeck->part);
    checkPackage(zip, applied);
}

void PptxDesignTest::resizeAsksAsPowerPointDoes()
{
    const QSizeF wide(960, 540);
    const QSizeF standard(720, 540);
    QVERIFY(resizeAsks(wide, standard));      // content would shrink: Maximize or Ensure Fit?
    QVERIFY(!resizeAsks(standard, wide));     // it fits as it is
    QVERIFY(!resizeAsks(wide, QSizeF(480, 270))); // the same shape: scaled
    QCOMPARE(resizeFactor(wide, standard, SlideScale::EnsureFit), 0.75);
    QCOMPARE(resizeFactor(wide, standard, SlideScale::Maximize), 1.0);
    QCOMPARE(resizeFactor(standard, wide, SlideScale::EnsureFit), 1.0);
}

void PptxDesignTest::slideSizeEnsureFit()
{
    const Presentation deck = fixture();
    QCOMPARE(deck.slideSize, QSizeF(960, 540));
    QString error;
    const Presentation fit = resized(deck, QSizeF(720, 540), SlideScale::EnsureFit, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(fit.slideSize, QSizeF(720, 540));
    // 0.75 about the top-left, centred: (0, 67.5) down.
    const Slide &one = fit.slides.at(0);
    QVERIFY(nearRect(one.shapes.at(2).rect, QRectF(405, 175.5, 270, 135)));
    QCOMPARE(one.shapes.at(2).paragraphs.at(0).runs.at(0).pointSize, 18.0);
    // The title has no box of its own: it still inherits the master's, scaled.
    QVERIFY(nearRect(one.shapes.at(0).rect, QRectF(27, 81, 666, 54)));
    QCOMPARE(one.shapes.at(0).paragraphs.at(0).runs.at(0).pointSize, 33.0);
    QVERIFY(nearRect(fit.masters.first().placeholders.first().rect, QRectF(27, 81, 666, 54)));
    QCOMPARE(fit.masters.first().titleStyle.first().runs.first().pointSize, 33.0);
    // The table's grid, the group's box (its child space unchanged).
    const Table &table = fit.slides.at(1).shapes.at(0).table;
    QCOMPARE(table.columns.size(), 2);
    QVERIFY(near(table.columns.first(), 180));
    const Shape &group = fit.slides.at(2).shapes.at(1);
    QVERIFY(nearRect(group.rect, QRectF(300, 142.5, 150, 150)));
    QCOMPARE(group.childRect, deck.slides.at(2).shapes.at(1).childRect);
    // The slide still uses its layout; the written package says the size.
    QCOMPARE(one.layoutId, deck.slides.at(0).layoutId);
    const QByteArray zip = writePptx(fit);
    QVERIFY(part(zip, QStringLiteral("ppt/presentation.xml")).contains(R"(<p:sldSz cx="9144000" cy="6858000"/>)"));
    // The title spells no box in the slide: it inherits.
    const QByteArray slide = part(zip, QStringLiteral("ppt/slides/slide1.xml"));
    QVERIFY(slide.contains(R"(<p:nvPr><p:ph type="title"/></p:nvPr></p:nvSpPr><p:spPr/>)"));
    checkPackage(zip, fit);
}

void PptxDesignTest::slideSizeMaximize()
{
    QString error;
    const Presentation deck = fixture();
    const Presentation max = resized(deck, QSizeF(720, 540), SlideScale::Maximize, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // Factor 1, centred: everything 120 pt to the left (the sides cut off).
    QVERIFY(nearRect(max.slides.at(0).shapes.at(2).rect, QRectF(420, 144, 360, 180)));
    QCOMPARE(max.slides.at(0).shapes.at(2).paragraphs.at(0).runs.at(0).pointSize, 24.0);
    checkPackage(writePptx(max), max);
    // Unchanged size: nothing happens.
    QVERIFY(resized(deck, deck.slideSize, SlideScale::EnsureFit).slides == deck.slides);
}

void PptxDesignTest::backgroundsWritten()
{
    const Presentation deck = fixture();
    // A gradient on slide 2 only.
    Background gradient;
    gradient.set = true;
    gradient.color = QColor(0x10, 0x20, 0x30);
    gradient.style.kind = FillKind::Gradient;
    gradient.style.stops = {{0.0, QColor(0x10, 0x20, 0x30)}, {1.0, QColor(0x80, 0xA0, 0xC0)}};
    gradient.style.angle = 45;
    Presentation edited = withBackground(deck, deck.slides.at(1).id, gradient, false);
    QCOMPARE(edited.slides.at(1).background, gradient);
    QVERIFY(!edited.slides.at(0).background.set);
    // A picture at 40 % transparency, tiled (a texture), on slide 3.
    Background picture;
    picture.set = true;
    picture.color = QColor(200, 200, 200);
    picture.style.kind = FillKind::Picture;
    picture.style.image = PptxFixture::pngBytes(QColor(90, 140, 60), QSize(16, 16));
    picture.style.imageMime = QStringLiteral("image/png");
    picture.style.tile = true;
    picture.style.imageAlpha = 0.6;
    edited = withBackground(edited, deck.slides.at(2).id, picture, false);
    QByteArray zip = writePptx(edited);
    QVERIFY(part(zip, QStringLiteral("ppt/slides/slide2.xml")).contains("<p:bg><p:bgPr><a:gradFill"));
    const QByteArray three = part(zip, QStringLiteral("ppt/slides/slide3.xml"));
    QVERIFY(three.contains(R"(<a:alphaModFix amt="60000"/>)"));
    QVERIFY(three.contains("<a:tile/>"));
    checkPackage(zip, edited);
    const Presentation back = readPptxData(zip);
    QCOMPARE(back.slides.at(2).background.style.imageAlpha, 0.6);
    // Apply to All: the master gets it, the layouts and slides let go of theirs.
    Background solid;
    solid.set = true;
    solid.color = QColor(0xFA, 0xEB, 0xD7);
    const Presentation all = withBackground(edited, deck.slides.at(1).id, solid, true);
    QCOMPARE(all.masters.first().background, solid.color);
    for (const Slide &slide : all.slides) {
        QVERIFY(!slide.background.set);
        const QImage image = renderSlideToImage(all, slide, QSize(96, 54));
        QCOMPARE(image.pixelColor(2, 2), solid.color);
    }
    zip = writePptx(all);
    QVERIFY(part(zip, QStringLiteral("ppt/slideMasters/slideMaster1.xml")).contains(R"(<a:srgbClr val="FAEBD7"/>)"));
    QVERIFY(!part(zip, QStringLiteral("ppt/slides/slide2.xml")).contains("<p:bg>"));
    // The layout was not edited: byte for byte.
    QCOMPARE(part(zip, QStringLiteral("ppt/slideLayouts/slideLayout1.xml")),
             part(PptxFixture::build(), QStringLiteral("ppt/slideLayouts/slideLayout1.xml")));
    checkPackage(zip, all);
}

void PptxDesignTest::generatedDeckTakesATheme()
{
    // A deck QindaShow made: no master, a text box in black.
    Presentation deck;
    Slide &slide = deck.addSlide(QStringLiteral("Mine"));
    Shape box;
    box.id = QStringLiteral("t");
    box.kind = ShapeKind::TextBox;
    box.rect = QRectF(72, 72, 400, 80);
    box.strokeWidth = 0;
    TextRun run;
    run.text = QStringLiteral("Hello");
    box.paragraphs = {TextParagraph{.runs = {run}}};
    slide.shapes.append(box);
    QString error;
    const Presentation themedDeck = themed(deck, builtInThemes().at(1), ThemePart::Whole, QString(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(themedDeck.masters.size(), 1);
    QCOMPARE(themedDeck.masters.first().theme.name, QStringLiteral("Harbor"));
    QCOMPARE(themedDeck.slides.size(), 1);
    QCOMPARE(themedDeck.slides.first().title, QStringLiteral("Mine")); // kept through the round trip
    QCOMPARE(themedDeck.slides.first().shapes.first().displayText(), QStringLiteral("Hello"));
    QVERIFY(!themedDeck.layouts.isEmpty());
    checkPackage(writePptx(themedDeck), themedDeck);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    PptxDesignTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pptx_design.moc"
