// SPDX-License-Identifier: GPL-3.0-or-later
// M82 (D-091) slide gates over the hand-built PowerPoint packages
// (pptx_fixture_m82.h): the deck's masters and layouts as New Slide offers
// them; a new slide on a layout written as PowerPoint writes one (related
// to the layout, placeholders with no position or look of their own, the
// footers of the slide before) and inheriting it; Layout keeping the text
// and taking the new places; Reset Slide; Duplicate Slide owning its chart;
// Reuse Slides keeping or adopting the source formatting; Browse for Themes
// taking a template's masters and layouts for every slide or the selected
// ones. Every package written is schema-valid (QQO_OOXML_SCHEMA_DIR), keeps
// PowerPoint's id rules, reads back equal, and leaves the parts it did not
// touch byte for byte; LibreOffice renders a deck made this way.

#include "master_edit.h"
#include "pptx_fixture_m82.h"
#include "pptx_origin.h"
#include "pptx_reader.h"
#include "pptx_rules.h"
#include "pptx_slides.h"
#include "pptx_soffice.h"
#include "pptx_writer.h"

#include "support/ooxml_schema.h"

#include <QGuiApplication>
#include <QtTest>

using namespace QindaOffice::Show;
namespace Ooxml = QindaOffice::Ooxml;

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

const Shape *placeholderOf(const Slide &slide, const QString &type, qint64 index = -2)
{
    for (const Shape &shape : slide.shapes) {
        if (shape.placeholder.type == type && (index == -2 || shape.placeholder.index == index)) {
            return &shape;
        }
    }
    return nullptr;
}

QString layoutIdNamed(const Presentation &deck, const QString &name)
{
    for (const SlideLayout &layout : deck.layouts) {
        if (layout.name == name) {
            return layout.id;
        }
    }
    return {};
}

// The deck saved: PowerPoint's rules, the schemas, then read back — equal.
bool savesCleanly(const Presentation &deck, QByteArray *zipOut = nullptr)
{
    const QByteArray zip = writePptx(deck);
    const QStringList rules = PptxFixture::ruleViolations(zip);
    if (!rules.isEmpty()) {
        qWarning() << rules;
        return false;
    }
    if (QindaOffice::Test::schemaValidationAvailable()) {
        const QStringList errors = QindaOffice::Test::validatePackage(*Ooxml::Package::fromData(zip));
        if (!errors.isEmpty()) {
            qWarning() << errors;
            return false;
        }
    }
    const Presentation back = readPptxData(zip);
    if (back.slides.size() != deck.slides.size()) {
        qWarning() << "slides" << back.slides.size() << deck.slides.size();
        return false;
    }
    for (qsizetype i = 0; i < deck.slides.size(); ++i) {
        Slide a = back.slides.at(i);
        Slide b = deck.slides.at(i);
        a.id = b.id; // ids of what was made since the last save are renewed
        for (qsizetype s = 0; s < qMin(a.shapes.size(), b.shapes.size()); ++s) {
            a.shapes[s].id = b.shapes.at(s).id;
        }
        if (!(a == b)) {
            qWarning() << "slide" << i << "differs after a save";
            return false;
        }
    }
    if (zipOut != nullptr) {
        *zipOut = zip;
    }
    return true;
}

Presentation layoutsDeck()
{
    return readPptxData(PptxFixture::M82::layoutsDeck());
}

std::shared_ptr<const Ooxml::Package> packageOf(const QByteArray &zip)
{
    return Ooxml::Package::fromData(zip);
}

} // namespace

class PptxSlidesTest : public QObject {
    Q_OBJECT
private slots:
    void layoutChoicesListMastersAndLayouts()
    {
        const Presentation deck = layoutsDeck();
        const QList<LayoutChoice> choices = layoutChoices(deck);
        QCOMPARE(choices.size(), 4);
        QStringList names;
        for (const LayoutChoice &choice : choices) {
            names.append(choice.name);
            QCOMPARE(choice.masterName, QStringLiteral("Fixture Master"));
        }
        QCOMPARE(names, (QStringList{QStringLiteral("Title and Content"), QStringLiteral("Title Slide"),
                                     QStringLiteral("Two Content"), QStringLiteral("Blank")}));
        // After a title slide PowerPoint offers Title and Content.
        QCOMPARE(nextSlideLayout(deck, nullptr), layoutIdNamed(deck, QStringLiteral("Title and Content")));
        QCOMPARE(nextSlideLayout(deck, &deck.slides.first()), deck.slides.first().layoutId);
    }

    void newSlideOnALayoutInheritsIt()
    {
        const Presentation deck = layoutsDeck();
        const QString twoContent = layoutIdNamed(deck, QStringLiteral("Two Content"));
        QString newId;
        QString error;
        const Presentation after = withNewSlide(deck, twoContent, 1, &newId, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(after.slides.size(), deck.slides.size() + 1);
        const Slide &slide = after.slides.at(1);
        QCOMPARE(slide.id, newId);
        QCOMPARE(slide.layoutId, twoContent);
        QCOMPARE(slide.master, deck.masters.first().id);
        // The layout's placeholders, empty, where the layout puts them.
        const Shape *title = placeholderOf(slide, QStringLiteral("title"));
        const Shape *left = placeholderOf(slide, QStringLiteral("obj"), 1);
        const Shape *right = placeholderOf(slide, QStringLiteral("obj"), 2);
        QVERIFY(title != nullptr && left != nullptr && right != nullptr);
        QVERIFY(nearRect(title->rect, QRectF(36, 18, 888, 72))); // the master's title
        QVERIFY(nearRect(left->rect, QRectF(36, 108, 432, 360)));
        QVERIFY(nearRect(right->rect, QRectF(492, 108, 432, 360)));
        QVERIFY(title->displayText().isEmpty() && left->displayText().isEmpty());
        // Slide 1 shows a slide number: the new slide after it does too.
        QVERIFY(placeholderOf(slide, QStringLiteral("sldNum")) != nullptr);
        QCOMPARE(placeholderOf(slide, QStringLiteral("sldNum"))->displayText(), QStringLiteral("2")); // its own number
        QVERIFY(placeholderOf(slide, QStringLiteral("dt")) == nullptr);
        // The other slides are as they were.
        QCOMPARE(after.slides.at(0).shapes.size(), deck.slides.at(0).shapes.size());
        QCOMPARE(after.slides.at(2).title, deck.slides.at(1).title);
        // Written as PowerPoint writes one: related to its layout, no
        // position of its own.
        QByteArray zip;
        QVERIFY(savesCleanly(after, &zip));
        const auto package = packageOf(zip);
        const QString slidePart = QStringLiteral("ppt/slides/slide2.xml");
        const auto layoutRel = package->firstOfType(slidePart, QString::fromUtf16(Ooxml::RelType::SlideLayout));
        QVERIFY(layoutRel.has_value());
        const auto layoutRoot = package->xml(package->targetPart(slidePart, *layoutRel));
        QCOMPARE(layoutRoot->child(QString::fromUtf16(Ooxml::Ns::P), u"cSld")->attr(u"name"), QStringLiteral("Two Content"));
        // Its placeholders have no position of their own — but the slide
        // number copied from slide 1, which had one.
        const auto slideRoot = package->xml(slidePart);
        const QString p = QString::fromUtf16(Ooxml::Ns::P);
        const QString a = QString::fromUtf16(Ooxml::Ns::A);
        int positioned = 0;
        for (const auto *shape : slideRoot->child(p, u"cSld")->child(p, u"spTree")->elements(p, u"sp")) {
            positioned += shape->child(p, u"spPr")->child(a, u"xfrm") != nullptr ? 1 : 0;
        }
        QCOMPARE(positioned, 1);
        // Untouched parts byte for byte.
        const QByteArray before = writePptx(deck);
        for (const char *name : {"ppt/slideMasters/slideMaster1.xml", "ppt/slideLayouts/slideLayout3.xml",
                                 "ppt/theme/theme1.xml", "ppt/slides/slide1.xml"}) {
            QCOMPARE(part(zip, QLatin1String(name)), part(before, QLatin1String(name)));
        }
        // The layout moves: the new slide follows it on the next re-read.
        Presentation moved = after;
        for (SlideLayout &layout : moved.layouts) {
            if (layout.id == twoContent) {
                for (Shape &shape : layout.placeholders) {
                    if (shape.placeholder.index == 1) {
                        shape.rect.translate(10, 0);
                    }
                }
            }
        }
        const Presentation reread = readPptxData(writePptx(moved));
        QVERIFY(nearRect(placeholderOf(reread.slides.at(1), QStringLiteral("obj"), 1)->rect, QRectF(46, 108, 432, 360)));
    }

    void layoutChangeKeepsTextAndTakesTheNewPlaces()
    {
        const Presentation deck = layoutsDeck();
        const QString twoContent = layoutIdNamed(deck, QStringLiteral("Two Content"));
        QString error;
        const Presentation after = withSlideLayout(deck, deck.slides.first().id, twoContent, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const Slide &slide = after.slides.first();
        QCOMPARE(slide.layoutId, twoContent);
        QCOMPARE(placeholderOf(slide, QStringLiteral("title"))->displayText(), QStringLiteral("Inherited title"));
        const Shape *left = placeholderOf(slide, QStringLiteral("obj"), 1);
        QVERIFY(left != nullptr);
        QVERIFY(left->displayText().contains(QStringLiteral("Point")));
        QVERIFY(nearRect(left->rect, QRectF(36, 108, 432, 360)));
        // The second content placeholder joins it, empty.
        const Shape *right = placeholderOf(slide, QStringLiteral("obj"), 2);
        QVERIFY(right != nullptr && right->displayText().isEmpty());
        // The text box of its own stays where it was.
        QCOMPARE(slide.shapes.at(2).rect, deck.slides.first().shapes.at(2).rect);
        QVERIFY(savesCleanly(after));
    }

    void resetSlideRestoresTheLayout()
    {
        Presentation deck = layoutsDeck();
        // The title moved and recoloured, the body deleted.
        Slide &first = deck.slides.first();
        Shape *title = nullptr;
        for (Shape &shape : first.shapes) {
            if (shape.placeholder.type == u"title") {
                title = &shape;
            }
        }
        QVERIFY(title != nullptr);
        title->rect.translate(50, 40);
        title->paragraphs.first().runs.first().color = QColor(0xc0, 0, 0);
        first.shapes.removeIf([](const Shape &shape) { return shape.placeholder.index == 1; });
        const Presentation moved = readPptxData(writePptx(deck));
        QVERIFY(!nearRect(placeholderOf(moved.slides.first(), QStringLiteral("title"))->rect, QRectF(36, 18, 888, 72)));
        QString error;
        const Presentation after = withResetSlide(moved, moved.slides.first().id, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const Slide &slide = after.slides.first();
        const Shape *reset = placeholderOf(slide, QStringLiteral("title"));
        QVERIFY(nearRect(reset->rect, QRectF(36, 18, 888, 72)));
        QCOMPARE(reset->displayText(), QStringLiteral("Inherited title")); // the text stays
        QCOMPARE(reset->paragraphs.first().runs.first().color, QColor(Qt::black)); // the master's tx1
        // The body placeholder is back, empty, where the layout has it.
        const Shape *body = placeholderOf(slide, QStringLiteral("obj"), 1);
        QVERIFY(body != nullptr && body->displayText().isEmpty());
        QVERIFY(nearRect(body->rect, QRectF(72, 144, 432, 288)));
        QVERIFY(savesCleanly(after));
    }

    void duplicateSlideOwnsItsParts()
    {
        Presentation deck = layoutsDeck();
        deck.slides[3].notes = QStringLiteral("Chart notes");
        const QString chartSlide = deck.slides.at(3).id;
        QString newId;
        QString error;
        const Presentation after = withDuplicatedSlide(deck, chartSlide, &newId, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(after.slides.size(), deck.slides.size() + 1);
        QCOMPARE(after.slides.at(4).id, newId);
        QCOMPARE(after.slides.at(4).layoutId, after.slides.at(3).layoutId);
        QCOMPARE(after.slides.at(4).notes, QStringLiteral("Chart notes"));
        QCOMPARE(after.slides.at(4).shapes.size(), after.slides.at(3).shapes.size());
        QByteArray zip;
        QVERIFY(savesCleanly(after, &zip));
        const auto package = packageOf(zip);
        const QString chartType = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart");
        const auto a = package->firstOfType(QStringLiteral("ppt/slides/slide4.xml"), chartType);
        const auto b = package->firstOfType(QStringLiteral("ppt/slides/slide5.xml"), chartType);
        QVERIFY(a && b);
        QVERIFY2(package->targetPart(QStringLiteral("ppt/slides/slide4.xml"), *a)
                     != package->targetPart(QStringLiteral("ppt/slides/slide5.xml"), *b),
                 "the duplicate shares its chart");
        QVERIFY(after.slides.at(4).origin->creationId != after.slides.at(3).origin->creationId);
    }

    void reuseSlidesKeepOrTakeTheFormatting()
    {
        const Presentation deck = layoutsDeck();
        const auto source = packageOf(PptxFixture::M82::templatePackage());
        QVERIFY(source != nullptr);
        // The template has no slides: reuse from the base deck instead, and
        // from a deck on the template's design.
        QString error;
        const Presentation designed = withTemplateDesign(readPptxData(PptxFixture::build()), source, {}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const auto other = packageOf(writePptx(designed));
        QStringList ids;
        ReuseOptions options;
        options.slides = {0, 2}; // a title and content slide, a picture
        options.index = 1;
        options.keepSourceFormatting = false;
        Presentation adopted = withReusedSlides(deck, other, options, &ids, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(adopted.slides.size(), deck.slides.size() + 2);
        QCOMPARE(ids.size(), 2);
        QCOMPARE(adopted.masters.size(), deck.masters.size()); // this deck's design
        QCOMPARE(adopted.slides.at(1).layoutId, layoutIdNamed(deck, QStringLiteral("Title and Content")));
        QCOMPARE(placeholderOf(adopted.slides.at(1), QStringLiteral("title"))->displayText(), QStringLiteral("Inherited title"));
        QVERIFY(nearRect(placeholderOf(adopted.slides.at(1), QStringLiteral("title"))->rect, QRectF(36, 18, 888, 72)));
        QCOMPARE(adopted.slides.at(2).shapes.first().kind, ShapeKind::Image);
        QCOMPARE(adopted.slides.at(2).shapes.first().imagePng, designed.slides.at(2).shapes.first().imagePng);
        QVERIFY(savesCleanly(adopted));

        options.keepSourceFormatting = true;
        const Presentation kept = withReusedSlides(deck, other, options, &ids, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(kept.masters.size(), deck.masters.size() + 1); // the template's master came along
        QCOMPARE(kept.masters.last().name, QStringLiteral("Template Master"));
        const Shape *title = placeholderOf(kept.slides.at(1), QStringLiteral("title"));
        QVERIFY(nearRect(title->rect, QRectF(60, 30, 840, 80))); // the template master's title
        QCOMPARE(title->paragraphs.first().runs.first().fontFamily, QStringLiteral("Georgia"));
        QByteArray zip;
        QVERIFY(savesCleanly(kept, &zip));
        // The deck's own design parts are untouched.
        const QByteArray before = writePptx(deck);
        QCOMPARE(part(zip, QStringLiteral("ppt/theme/theme1.xml")), part(before, QStringLiteral("ppt/theme/theme1.xml")));
    }

    void templateDesignForAllOrSelectedSlides()
    {
        const Presentation deck = layoutsDeck();
        const auto source = packageOf(PptxFixture::M82::templatePackage());
        QVERIFY(packageHasMasters(*source));
        QString error;
        const Presentation all = withTemplateDesign(deck, source, {}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(all.masters.size(), 1); // the deck's master is gone, as in PowerPoint
        QCOMPARE(all.masters.first().name, QStringLiteral("Template Master"));
        QCOMPARE(all.masters.first().theme.fonts.major, QStringLiteral("Georgia"));
        // Title and Content slides take the template's layout of that name.
        const Slide &first = all.slides.first();
        QCOMPARE(all.findLayout(first.layoutId)->name, QStringLiteral("Title and Content"));
        QVERIFY(nearRect(placeholderOf(first, QStringLiteral("title"))->rect, QRectF(60, 30, 840, 80)));
        QVERIFY(nearRect(placeholderOf(first, QStringLiteral("obj"), 1)->rect, QRectF(90, 140, 780, 330)));
        QCOMPARE(placeholderOf(first, QStringLiteral("title"))->displayText(), QStringLiteral("Inherited title"));
        QVERIFY(savesCleanly(all));

        const QString second = deck.slides.at(1).id;
        const Presentation selected = withTemplateDesign(deck, source, {second}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(selected.masters.size(), 2);
        QCOMPARE(selected.slides.at(1).master, selected.masters.last().id);
        QCOMPARE(selected.slides.at(0).master, selected.masters.first().id);
        QVERIFY(savesCleanly(selected));
    }

    void libreOfficeRendersLayoutAndReusedSlides()
    {
        if (!PptxFixture::sofficeAvailable()) {
            QSKIP("LibreOffice (soffice) is not available");
        }
        const Presentation deck = layoutsDeck();
        QString newId;
        Presentation made = withNewSlide(deck, layoutIdNamed(deck, QStringLiteral("Two Content")), 1, &newId);
        Slide *fresh = made.findSlide(newId);
        QVERIFY(fresh != nullptr);
        for (Shape &shape : fresh->shapes) {
            if (shape.placeholder.type == u"title") {
                TextRun run;
                run.text = QStringLiteral("Made on a layout");
                shape.paragraphs = {TextParagraph{.runs = {run}}};
            }
        }
        ReuseOptions options;
        options.slides = {2, 3};
        options.keepSourceFormatting = true;
        QStringList ids;
        made = withReusedSlides(made, packageOf(PptxFixture::build()), options, &ids);
        QCOMPARE(made.slides.size(), deck.slides.size() + 3);
        QTemporaryDir dir;
        QString error;
        const QByteArray pdf = PptxFixture::convertWithSoffice(dir, QStringLiteral("made.pptx"), writePptx(made),
                                                               QStringLiteral("pdf"), &error);
        QVERIFY2(!pdf.isEmpty(), qPrintable(error));
        // One page a slide shown (the fixture's slide 5 is hidden).
        const int shown = int(std::count_if(made.slides.cbegin(), made.slides.cend(), [](const Slide &s) { return !s.hidden; }));
        QCOMPARE(PptxFixture::pdfPages(pdf), shown);
    }
};

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    PptxSlidesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pptx_slides.moc"
