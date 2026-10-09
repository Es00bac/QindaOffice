// SPDX-License-Identifier: GPL-3.0-or-later
// M75 (D-084) sections and comments gates over the hand-built PowerPoint
// package: p14:sectionLst read, edited (add, rename, remove, move) and
// written with the slide ids written, slides keeping their sections when
// slides come and go; PowerPoint's modern comments (authors.xml,
// modernComment_*.xml: a shape-anchored thread with a reply, a resolved
// slide comment) and legacy ones (commentAuthors.xml, commentN.xml,
// p15:threadingInfo replies) read, carried byte for byte while unchanged,
// written in the deck's own kind when edited (a resolved one makes them
// modern), every author keeping its id; both through .fodp.

#include "fodp_reader.h"
#include "fodp_writer.h"
#include "pptx_fixture_m75.h"
#include "pptx_origin.h"
#include "pptx_reader.h"
#include "pptx_rules.h"
#include "pptx_writer.h"
#include "sections.h"

#include "support/ooxml_schema.h"

#include <QGuiApplication>
#include <QtTest>

using namespace QindaOffice::Show;
namespace Ooxml = QindaOffice::Ooxml;

class PptxReviewTest : public QObject {
    Q_OBJECT
private slots:
    void sectionsRead();
    void sectionOperations();
    void sectionsFollowTheSlides();
    void sectionsWritten();
    void modernCommentsRead();
    void unchangedCommentsByteForByte();
    void editedCommentsWrittenModern();
    void legacyCommentsRoundTrip();
    void resolvingMakesThemModern();
    void reviewThroughFodp();
};

namespace {

QByteArray part(const QByteArray &zip, const QString &name)
{
    const auto package = Ooxml::Package::fromData(zip);
    return package ? package->data(name) : QByteArray();
}

QStringList partsLike(const QByteArray &zip, const QString &prefix)
{
    QStringList out;
    for (const QString &name : Ooxml::Package::fromData(zip)->partNames()) {
        if (name.startsWith(prefix)) {
            out.append(name);
        }
    }
    return out;
}

Presentation withSections()
{
    return readPptxData(PptxFixture::edited([](Ooxml::PackageWriter &w) { PptxFixture::presentationExtension(w, PptxFixture::kSections); }));
}

Presentation withComments()
{
    return readPptxData(PptxFixture::edited([](Ooxml::PackageWriter &w) {
        PptxFixture::addModernComments(w);
    }));
}

Presentation withLegacyComments()
{
    return readPptxData(PptxFixture::edited([](Ooxml::PackageWriter &w) { PptxFixture::addLegacyComments(w); }));
}

QStringList names(const Presentation &deck)
{
    QStringList out;
    for (const SlideSection &section : deck.sections) {
        out.append(QStringLiteral("%1:%2").arg(section.name).arg(section.slides.size()));
    }
    return out;
}

void checkRules(const QByteArray &zip)
{
    const QStringList rules = PptxFixture::ruleViolations(zip);
    QVERIFY2(rules.isEmpty(), qPrintable(rules.join(QLatin1Char('\n'))));
    if (QindaOffice::Test::schemaValidationAvailable()) {
        const QStringList errors = QindaOffice::Test::validatePackage(*Ooxml::Package::fromData(zip));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
    }
}

} // namespace

void PptxReviewTest::sectionsRead()
{
    const Presentation deck = withSections();
    QCOMPARE(names(deck), (QStringList{QStringLiteral("Intro:2"), QStringLiteral("Data:3")}));
    QCOMPARE(deck.sections.first().id, QStringLiteral("{8A6B1E41-6C2F-4A0B-9C1D-2E3F40516273}"));
    QCOMPARE(deck.sections.first().slides, (QStringList{QStringLiteral("s256"), QStringLiteral("s257")}));
    QCOMPARE(sectionOf(deck, QStringLiteral("s259")), 1);
    QVERIFY(readPptxData(PptxFixture::build()).sections.isEmpty());
}

void PptxReviewTest::sectionOperations()
{
    Presentation deck = readPptxData(PptxFixture::build());
    // Add Section at slide 3 of a deck without sections: PowerPoint's
    // "Default Section" holds slides 1-2.
    QCOMPARE(addSection(deck, QStringLiteral("s258"), QStringLiteral("Middle")), 1);
    QCOMPARE(names(deck), (QStringList{QStringLiteral("Default Section:2"), QStringLiteral("Middle:3")}));
    QCOMPARE(addSection(deck, QStringLiteral("s260"), QStringLiteral("End")), 2);
    QCOMPARE(names(deck), (QStringList{QStringLiteral("Default Section:2"), QStringLiteral("Middle:2"), QStringLiteral("End:1")}));
    QVERIFY(renameSection(deck, 0, QStringLiteral("Start")));
    QVERIFY(!renameSection(deck, 0, QStringLiteral("  ")));
    // Move "End" up: its slide moves before Middle's.
    QVERIFY(moveSection(deck, 2, -1));
    QCOMPARE(names(deck), (QStringList{QStringLiteral("Start:2"), QStringLiteral("End:1"), QStringLiteral("Middle:2")}));
    QCOMPARE(deck.slides.at(2).id, QStringLiteral("s260"));
    QVERIFY(!moveSection(deck, 0, -1));
    // Remove "End": its slide joins Start.
    QVERIFY(removeSection(deck, 1));
    QCOMPARE(names(deck), (QStringList{QStringLiteral("Start:3"), QStringLiteral("Middle:2")}));
    removeAllSections(deck);
    QVERIFY(deck.sections.isEmpty());
}

void PptxReviewTest::sectionsFollowTheSlides()
{
    Presentation deck = withSections();
    // A new slide after slide 2 joins Intro; a deleted one leaves Data.
    Slide added;
    added.id = QStringLiteral("new");
    deck.slides.insert(2, added);
    deck.slides.removeAt(5); // s260
    normalizeSections(deck);
    QCOMPARE(names(deck), (QStringList{QStringLiteral("Intro:3"), QStringLiteral("Data:2")}));
    QCOMPARE(deck.sections.first().slides.last(), QStringLiteral("new"));
    // Slide 4 (s259) dragged to the top joins Intro.
    moveSlideInSections(deck, QStringLiteral("s259"));
    deck.slides.move(4, 0);
    normalizeSections(deck);
    QCOMPARE(deck.sections.first().slides.first(), QStringLiteral("s259"));
    QCOMPARE(names(deck), (QStringList{QStringLiteral("Intro:4"), QStringLiteral("Data:1")}));
}

void PptxReviewTest::sectionsWritten()
{
    Presentation deck = withSections();
    // Unchanged: the sections as read.
    QByteArray zip = writePptx(deck);
    QByteArray presentation = part(zip, QStringLiteral("ppt/presentation.xml"));
    QVERIFY(presentation.contains(R"(<p14:section name="Intro" id="{8A6B1E41-6C2F-4A0B-9C1D-2E3F40516273}"><p14:sldIdLst><p14:sldId id="256"/><p14:sldId id="257"/></p14:sldIdLst></p14:section>)"));
    checkRules(zip);
    // A slide added in the middle (a fresh slide id) and a renamed section.
    Slide added;
    added.id = QStringLiteral("fresh");
    deck.slides.insert(1, added);
    normalizeSections(deck);
    QVERIFY(renameSection(deck, 1, QStringLiteral("Numbers")));
    zip = writePptx(deck);
    presentation = part(zip, QStringLiteral("ppt/presentation.xml"));
    QVERIFY(presentation.contains(R"(<p14:sldId id="256"/><p14:sldId id="261"/><p14:sldId id="257"/>)"));
    QVERIFY(presentation.contains(R"(name="Numbers")"));
    checkRules(zip);
    const Presentation back = readPptxData(zip);
    QCOMPARE(names(back), (QStringList{QStringLiteral("Intro:3"), QStringLiteral("Numbers:3")}));
    // No sections: no list.
    deck.sections.clear();
    QVERIFY(!part(writePptx(deck), QStringLiteral("ppt/presentation.xml")).contains("sectionLst"));
}

void PptxReviewTest::modernCommentsRead()
{
    const Presentation deck = withComments();
    QVERIFY(!deck.legacyComments);
    const QList<SlideComment> &comments = deck.slides.at(0).comments;
    QCOMPARE(comments.size(), 3);
    const SlideComment &thread = comments.at(0);
    QCOMPARE(thread.id, QStringLiteral("{C0000000-0000-4000-8000-000000000001}"));
    QCOMPARE(thread.author, QStringLiteral("Ada Lovelace"));
    QCOMPARE(thread.initials, QStringLiteral("AL"));
    QCOMPARE(thread.text, QStringLiteral("Is this accent dark enough?"));
    QCOMPARE(thread.shapeId, deck.slides.at(0).shapes.at(2).id); // the text box (id 4)
    QCOMPARE(thread.created, QDateTime(QDate(2026, 9, 1), QTime(10, 0), QTimeZone::UTC));
    QVERIFY(!thread.resolved);
    const SlideComment &reply = comments.at(1);
    QCOMPARE(reply.parentId, thread.id);
    QCOMPARE(reply.author, QStringLiteral("Alan Turing"));
    QCOMPARE(reply.text, QStringLiteral("Agreed, darker."));
    const SlideComment &resolved = comments.at(2);
    QVERIFY(resolved.resolved);
    QVERIFY(resolved.shapeId.isEmpty()); // on the slide
    QCOMPARE(deck.slides.at(0).origin->creationId, 1234567u);
    QVERIFY(deck.slides.at(1).comments.isEmpty());
}

void PptxReviewTest::unchangedCommentsByteForByte()
{
    const QByteArray source = PptxFixture::edited([](Ooxml::PackageWriter &w) { PptxFixture::addModernComments(w); });
    const Presentation deck = readPptxData(source);
    Presentation edited = deck;
    edited.slides[1].shapes[0].rect.translate(5, 5); // an edit elsewhere
    const QByteArray zip = writePptx(edited);
    QCOMPARE(part(zip, QStringLiteral("ppt/comments/modernComment_100_12D687.xml")),
             part(source, QStringLiteral("ppt/comments/modernComment_100_12D687.xml")));
    QCOMPARE(part(zip, QStringLiteral("ppt/authors.xml")), part(source, QStringLiteral("ppt/authors.xml")));
    QVERIFY(part(zip, QStringLiteral("ppt/slides/slide1.xml")).contains("p188:commentRel"));
    checkRules(zip);
    QCOMPARE(readPptxData(zip).slides.at(0).comments, deck.slides.at(0).comments);
}

void PptxReviewTest::editedCommentsWrittenModern()
{
    Presentation deck = withComments();
    // A new thread on slide 2's table by a new author, a reply in the old
    // thread, the old thread resolved.
    SlideComment fresh;
    fresh.id = QStringLiteral("{C0000000-0000-4000-8000-0000000000A1}");
    fresh.author = QStringLiteral("Barbara Liskov");
    fresh.initials = QStringLiteral("BL");
    fresh.created = QDateTime(QDate(2026, 10, 1), QTime(12, 0), QTimeZone::UTC);
    fresh.text = QStringLiteral("Line one\nline two");
    fresh.shapeId = deck.slides.at(1).shapes.at(0).id;
    deck.slides[1].comments.append(fresh);
    SlideComment answer = fresh;
    answer.id = QStringLiteral("{C0000000-0000-4000-8000-0000000000A2}");
    answer.parentId = deck.slides.at(0).comments.at(0).id;
    answer.author = QStringLiteral("Ada Lovelace");
    answer.authorId = QStringLiteral("{AAAA0000-1111-2222-3333-444455556666}");
    answer.text = QStringLiteral("Fixed now");
    answer.shapeId = deck.slides.at(0).comments.at(0).shapeId;
    deck.slides[0].comments.insert(2, answer);
    deck.slides[0].comments[0].resolved = true;
    deck.slides[0].comments[1].resolved = true;
    deck.slides[0].comments[2].resolved = true;
    const QByteArray zip = writePptx(deck);
    checkRules(zip);
    // The authors: the two from the file with their ids, then the new one.
    const QByteArray authors = part(zip, QStringLiteral("ppt/authors.xml"));
    QVERIFY(authors.indexOf("{AAAA0000-1111-2222-3333-444455556666}") < authors.indexOf("Barbara Liskov"));
    QVERIFY(authors.contains(R"(name="Alan Turing")"));
    const QStringList parts = partsLike(zip, QStringLiteral("ppt/comments/modernComment_"));
    QCOMPARE(parts.size(), 2);
    const QByteArray table = part(zip, QStringLiteral("ppt/comments/modernComment_101_%1.xml")
                                           .arg(QString::number(deck.slides.at(1).origin->creationId == 0
                                                                    ? (qHash(deck.slides.at(1).id) & 0x7FFFFFFFu) | 1u
                                                                    : deck.slides.at(1).origin->creationId,
                                                                16)
                                                    .toUpper()));
    QVERIFY2(table.contains("<ac:graphicFrameMk id=\"2\""), table.constData());
    QVERIFY(part(zip, QStringLiteral("ppt/slides/slide2.xml")).contains("p14:creationId"));
    const Presentation back = readPptxData(zip);
    QCOMPARE(back.slides.at(0).comments.size(), 4);
    QCOMPARE(back.slides.at(0).comments.at(2).text, QStringLiteral("Fixed now"));
    QVERIFY(back.slides.at(0).comments.at(0).resolved);
    QCOMPARE(back.slides.at(1).comments.size(), 1);
    QCOMPARE(back.slides.at(1).comments.first().text, QStringLiteral("Line one\nline two"));
    QCOMPARE(back.slides.at(1).comments.first().author, QStringLiteral("Barbara Liskov"));
    QCOMPARE(back.slides.at(1).comments.first().shapeId, back.slides.at(1).shapes.at(0).id);
    QCOMPARE(back.slides.at(1).comments.first().created, fresh.created);
}

void PptxReviewTest::legacyCommentsRoundTrip()
{
    const QByteArray source = PptxFixture::edited([](Ooxml::PackageWriter &w) { PptxFixture::addLegacyComments(w); });
    Presentation deck = readPptxData(source);
    QVERIFY(deck.legacyComments);
    const QList<SlideComment> &comments = deck.slides.at(1).comments;
    QCOMPARE(comments.size(), 2);
    QCOMPARE(comments.at(0).id, QStringLiteral("0.2"));
    QCOMPARE(comments.at(0).author, QStringLiteral("Grace Hopper"));
    QCOMPARE(comments.at(0).position, QPointF(100, 50)); // 800 / 8, 400 / 8
    QCOMPARE(comments.at(1).parentId, QStringLiteral("0.2"));
    QCOMPARE(comments.at(1).text, QStringLiteral("Done"));
    // Unchanged: byte for byte.
    QByteArray zip = writePptx(deck);
    QCOMPARE(part(zip, QStringLiteral("ppt/comments/comment1.xml")), part(source, QStringLiteral("ppt/comments/comment1.xml")));
    QCOMPARE(part(zip, QStringLiteral("ppt/commentAuthors.xml")), part(source, QStringLiteral("ppt/commentAuthors.xml")));
    checkRules(zip);
    // A reply: still legacy; the next index of its author, threaded.
    SlideComment reply;
    reply.author = QStringLiteral("Grace Hopper");
    reply.authorId = QStringLiteral("0");
    reply.parentId = QStringLiteral("0.2");
    reply.text = QStringLiteral("Thanks");
    reply.created = QDateTime(QDate(2026, 10, 2), QTime(8, 30), QTimeZone::UTC);
    deck.slides[1].comments.append(reply);
    zip = writePptx(deck);
    checkRules(zip); // legacy comment parts are in the ECMA-376 schemas
    const QStringList parts = partsLike(zip, QStringLiteral("ppt/comments/"));
    QCOMPARE(parts.size(), 1);
    const QByteArray written = part(zip, parts.first());
    QVERIFY(written.contains(R"(<p:cm authorId="0" dt="2026-10-02T08:30:00.000" idx="3">)"));
    QVERIFY(written.contains(R"(<p15:parentCm authorId="0" idx="2"/>)"));
    QVERIFY(part(zip, QStringLiteral("ppt/commentAuthors.xml")).contains(R"(name="Grace Hopper" initials="GH" lastIdx="3")"));
    QVERIFY(!part(zip, QStringLiteral("ppt/authors.xml")).size());
    const Presentation back = readPptxData(zip);
    QVERIFY(back.legacyComments);
    QCOMPARE(back.slides.at(1).comments.size(), 3);
    QCOMPARE(back.slides.at(1).comments.at(1).text, QStringLiteral("Done"));
    QCOMPARE(back.slides.at(1).comments.at(2).text, QStringLiteral("Thanks"));
    QCOMPARE(back.slides.at(1).comments.at(2).parentId, QStringLiteral("0.2"));
}

void PptxReviewTest::resolvingMakesThemModern()
{
    Presentation deck = withLegacyComments();
    deck.slides[1].comments[0].resolved = true;
    const QByteArray zip = writePptx(deck);
    checkRules(zip);
    QVERIFY(part(zip, QStringLiteral("ppt/commentAuthors.xml")).isEmpty());
    QVERIFY(part(zip, QStringLiteral("ppt/authors.xml")).contains("Grace Hopper"));
    const Presentation back = readPptxData(zip);
    QVERIFY(!back.legacyComments);
    QCOMPARE(back.slides.at(1).comments.size(), 2);
    QVERIFY(back.slides.at(1).comments.first().resolved);
    QCOMPARE(back.slides.at(1).comments.at(1).parentId, back.slides.at(1).comments.first().id);
}

void PptxReviewTest::reviewThroughFodp()
{
    Presentation deck = withComments();
    QCOMPARE(addSection(deck, deck.slides.at(2).id, QStringLiteral("Later")), 1);
    const QByteArray fodp = writeFodp(deck);
    const Presentation back = readFodp(fodp);
    QCOMPARE(back.sections, deck.sections);
    QCOMPARE(back.slides.at(0).comments, deck.slides.at(0).comments);
    QCOMPARE(writeFodp(back), fodp);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    PptxReviewTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pptx_review.moc"
