// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_show_support.h"
using namespace PptxShowSupport;

class PptxShowTest : public QObject {
    Q_OBJECT
private slots:
    void actionsReadAsPowerPointWritesThem();
    void unchangedDeckKeepsWhatItRead();
    void editedActionsWrittenAndReadBack();
    void actionsMadeHereWrittenAsPowerPointWritesThem();
};

void PptxShowTest::actionsReadAsPowerPointWritesThem()
{
    const Presentation deck = fixture();
    QCOMPARE(deck.slides.size(), 5);
    const Slide &slide = deck.slides.at(1);
    const auto click = [&slide](quint32 id) { return byOfficeId(slide, id)->click; };
    const Shape *next = byOfficeId(slide, 10);
    QVERIFY(next != nullptr);
    QCOMPARE(next->geometry, QStringLiteral("actionButtonForwardNext"));
    QCOMPARE(parseAction(next->click).kind, ActionKind::NextSlide);
    QVERIFY(next->click.highlight);
    QCOMPARE(parseAction(next->hover).kind, ActionKind::LastSlide);
    QCOMPARE(next->hover.tooltip, QStringLiteral("To the end"));
    QCOMPARE(parseAction(click(11)).kind, ActionKind::PreviousSlide);
    QCOMPARE(parseAction(click(12)).kind, ActionKind::FirstSlide);
    QCOMPARE(parseAction(click(13)).kind, ActionKind::LastSlide);
    QCOMPARE(parseAction(click(14)).kind, ActionKind::LastViewed);
    QCOMPARE(parseAction(click(15)).kind, ActionKind::EndShow);
    const ActionTarget slideJump = parseAction(click(16));
    QCOMPARE(slideJump.kind, ActionKind::Slide);
    QCOMPARE(slideJump.slideId, deck.slides.at(3).id);
    const ActionTarget show = parseAction(click(17));
    QCOMPARE(show.kind, ActionKind::CustomShow);
    QCOMPARE(show.customShow, 1u);
    QVERIFY(show.returnAfter);
    QCOMPARE(click(18).link, QStringLiteral("https://example.org/m91"));
    QCOMPARE(parseAction(click(18)).kind, ActionKind::Url);
    QCOMPARE(click(18).tooltip, QStringLiteral("Example"));
    const ActionTarget file = parseAction(click(19));
    QCOMPARE(file.kind, ActionKind::File);
    QCOMPARE(file.address, QStringLiteral("notes/brief.docx"));
    // A program is read as a program (never as an address to open).
    const ActionTarget program = parseAction(click(20));
    QCOMPARE(program.kind, ActionKind::Program);
    QCOMPARE(click(20).link, QStringLiteral("ppaction://program"));
    QCOMPARE(program.address, QStringLiteral("file:///C:/Windows/notepad.exe"));
    const ActionTarget macro = parseAction(click(21));
    QCOMPARE(macro.kind, ActionKind::Macro);
    QCOMPARE(macro.address, QStringLiteral("Refresh"));
    const ShowAction sound = click(22);
    QCOMPARE(parseAction(sound).kind, ActionKind::None);
    QVERIFY(sound.link.isEmpty()); // ppaction://noaction is no link
    QVERIFY(sound.highlight);
    QVERIFY(sound.stopSound);
    QCOMPARE(sound.sound, PptxFixture::tinyWav(120));
    QCOMPARE(sound.soundName, QStringLiteral("applause.wav"));
    QVERIFY(click(23).isEmpty());
    QCOMPARE(parseAction(byOfficeId(slide, 23)->hover).kind, ActionKind::NextSlide);
    const QList<TextRun> runs = byOfficeId(slide, 24)->paragraphs.first().runs;
    QCOMPARE(runs.size(), 3);
    QCOMPARE(runs.at(0).hyperlink, slideLink(deck.slides.at(2).id));
    QVERIFY(runs.at(1).hyperlink.isEmpty());
    QCOMPARE(runs.at(2).hyperlink, QStringLiteral("https://example.org/m91"));
}

void PptxShowTest::unchangedDeckKeepsWhatItRead()
{
    const Presentation deck = fixture();
    QString error;
    const QByteArray zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    const QStringList found = problems(zip);
    QVERIFY2(found.isEmpty(), qPrintable(found.join(QLatin1Char('\n'))));
    const Presentation back = readPptxData(zip, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.customShows, deck.customShows);
    QCOMPARE(back.showSettings, deck.showSettings);
    for (qsizetype i = 0; i < deck.slides.size(); ++i) {
        QCOMPARE(back.slides.at(i), deck.slides.at(i));
    }
    QCOMPARE(back, deck);
    // presProps.xml as read; the actions' markup as PowerPoint spelled it.
    const QByteArray source = PptxFixture::M91::deck();
    QCOMPARE(part(zip, QStringLiteral("ppt/presProps.xml")), part(source, QStringLiteral("ppt/presProps.xml")));
    const QByteArray slide = part(zip, QStringLiteral("ppt/slides/slide2.xml"));
    QVERIFY(slide.contains("action=\"ppaction://program\""));
    QVERIFY(slide.contains("action=\"ppaction://customshow?id=1&amp;return=true\""));
    QVERIFY(slide.contains("<a:snd r:embed=\""));
    QVERIFY(slide.contains("advClick=\"0\" advTm=\"5000\""));
    QVERIFY(slide.contains("p14:prism"));
    // The program is still where it was; its relationship stays external.
    const auto package = Ooxml::Package::fromData(zip);
    bool program = false;
    for (const Ooxml::Relationship &rel : package->relationships(QStringLiteral("ppt/slides/slide2.xml"))) {
        program = program || (rel.target == u"file:///C:/Windows/notepad.exe" && rel.external);
    }
    QVERIFY(program);
}

void PptxShowTest::editedActionsWrittenAndReadBack()
{
    Presentation deck = fixture();
    Slide &slide = deck.slides[1];
    byOfficeId(slide, 11)->click.link = customShowLink(4, false);
    byOfficeId(slide, 12)->hover.link = QStringLiteral("https://example.org/hover");
    byOfficeId(slide, 13)->click.sound = builtInSound(QStringLiteral("Click"));
    byOfficeId(slide, 13)->click.soundName = QStringLiteral("Click");
    byOfficeId(slide, 20)->click.tooltip = QStringLiteral("Never run here");
    byOfficeId(slide, 22)->click = ShowAction();
    Shape *text = byOfficeId(slide, 24);
    text->paragraphs[0].runs[2].hyperlink = jumpLink(ActionKind::LastSlide);
    QString error;
    const QByteArray zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    const QStringList found = problems(zip);
    QVERIFY2(found.isEmpty(), qPrintable(found.join(QLatin1Char('\n'))));
    const Presentation back = readPptxData(zip, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    for (qsizetype i = 0; i < deck.slides.size(); ++i) {
        QCOMPARE(back.slides.at(i), deck.slides.at(i));
    }
    const Slide &read = back.slides.at(1);
    QCOMPARE(parseAction(byOfficeId(read, 11)->click).customShow, 4u);
    QVERIFY(!parseAction(byOfficeId(read, 11)->click).returnAfter);
    QCOMPARE(byOfficeId(read, 13)->click.sound, builtInSound(QStringLiteral("Click")));
    QCOMPARE(parseAction(byOfficeId(read, 20)->click).address, QStringLiteral("file:///C:/Windows/notepad.exe"));
    QVERIFY(byOfficeId(read, 22)->click.isEmpty());
    QCOMPARE(QByteArray(part(zip, QStringLiteral("ppt/slides/slide2.xml"))).count("<a:snd "), 1);
}

void PptxShowTest::actionsMadeHereWrittenAsPowerPointWritesThem()
{
    const Presentation deck = madeHere();
    QString error;
    const QByteArray zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    const QStringList found = problems(zip);
    QVERIFY2(found.isEmpty(), qPrintable(found.join(QLatin1Char('\n'))));
    const QByteArray slide = part(zip, QStringLiteral("ppt/slides/slide1.xml"));
    QVERIFY(slide.contains("<a:hlinkClick r:id=\"\" action=\"ppaction://hlinkshowjump?jump=nextslide\"/>"));
    QVERIFY(slide.contains("action=\"ppaction://hlinksldjump\""));
    QVERIFY(slide.contains("<a:hlinkClick r:id=\"\" action=\"ppaction://customshow?id=3&amp;return=true\"/>"));
    QVERIFY(slide.contains("action=\"ppaction://noaction\" tooltip=\"A chime\" highlightClick=\"1\""));
    QVERIFY(slide.contains("<a:hlinkHover "));
    QVERIFY(slide.contains("<p:transition spd=\"med\" advClick=\"0\" advTm=\"4200\"><p:fade/></p:transition>"));
    QVERIFY(part(zip, QStringLiteral("ppt/slides/slide2.xml")).contains("<p:transition advTm=\"0\"/>"));
    const QByteArray presentation = part(zip, QStringLiteral("ppt/presentation.xml"));
    QVERIFY(presentation.contains("<p:custShow name=\"Middle\" id=\"3\"><p:sldLst><p:sld r:id=\""));
    QVERIFY(presentation.indexOf("<p:custShowLst>") > presentation.indexOf("<p:notesSz"));
    const QByteArray props = part(zip, QStringLiteral("ppt/presProps.xml"));
    QVERIFY(props.contains("<p:showPr loop=\"1\" showNarration=\"1\" showAnimation=\"0\"><p:browse showScrollbar=\"0\"/><p:sldRg st=\"2\" end=\"3\"/>"));
    QVERIFY(props.contains("<p:penClr><a:srgbClr val=\"0070C0\"/></p:penClr>"));
    QVERIFY(props.contains("laserClr"));
    const Presentation back = readPptxData(zip, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.slides.size(), 3);
    const Slide &one = back.slides.at(0);
    QCOMPARE(one.shapes.size(), deck.slides.at(0).shapes.size());
    for (qsizetype i = 0; i < one.shapes.size(); ++i) {
        const Shape &was = deck.slides.at(0).shapes.at(i);
        const Shape &is = one.shapes.at(i);
        // Slide ids are the deck's own; links name the slide read back there.
        const auto same = [&](const ShowAction &a, const ShowAction &b) {
            ShowAction x = a;
            ShowAction y = b;
            for (qsizetype s = 0; s < 3; ++s) {
                if (x.link == slideLink(deck.slides.at(s).id)) {
                    x.link = slideLink(back.slides.at(s).id);
                }
            }
            return x == y;
        };
        QVERIFY2(same(was.click, is.click), qPrintable(was.id));
        QVERIFY2(same(was.hover, is.hover), qPrintable(was.id));
    }
    QCOMPARE(one.shapes.last().paragraphs.first().runs.first().hyperlink, slideLink(back.slides.at(1).id));
    QCOMPARE(one.advanceAfterMs, 4200);
    QVERIFY(!one.advanceOnClick);
    QCOMPARE(back.slides.at(1).advanceAfterMs, 0);
    QCOMPARE(back.slides.at(2).advanceAfterMs, -1);
    QCOMPARE(back.customShows.size(), 1);
    QCOMPARE(back.customShows.first().name, QStringLiteral("Middle"));
    QCOMPARE(back.customShows.first().slides,
             (QStringList{back.slides.at(1).id, back.slides.at(2).id, back.slides.at(1).id}));
    QCOMPARE(back.showSettings, deck.showSettings);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    PptxShowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pptx_show.moc"
