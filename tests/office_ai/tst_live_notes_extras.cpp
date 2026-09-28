// SPDX-License-Identifier: GPL-3.0-or-later
// M56 (D-061) in office_ai: the one-shot request/parse pairs (key terms,
// action items → iCalendar), the pure note surgery (split by topic, Anki
// cards, unclear spots, section times), the prompt's new marks (‼ flags,
// named speakers, AI jargon notes, prep material, earlier sessions), and
// the engine's flag, speaker names, screen images and usage.

#include "live_notes_engine.h"
#include "live_notes_extras.h"
#include "live_notes_prompt.h"

#include <QJsonArray>
#include <QtTest>

using namespace QindaOffice::Ai;
namespace Extras = LiveNotesExtras;

namespace {

class Run final : public ChatRun {
    Q_OBJECT
public:
    Run(QObject *parent, QString reply) : ChatRun(parent)
    {
        QTimer::singleShot(0, this, [this, reply] { emitFinished({reply, {120, 30}}); });
    }
    void cancel() override {}
};

class Provider final : public ChatProvider {
    Q_OBJECT
public:
    QStringList replies;
    QList<ChatRequest> requests;
    ChatRun *start(const ChatRequest &request) override
    {
        requests.append(request);
        return new Run(this, replies.isEmpty() ? QString() : replies.takeFirst());
    }
};

const QString kNotes = QStringLiteral(
    "# Biology\n\n## Summary\n- cells\n\n## Mitochondria [01:10]\n- make ATP *(unclear at 01:42: "
    "cristae?)*\n\n## Photosynthesis [05:00]\n- light reactions *(unclear: grana)*\n\n"
    "## Definitions\n- **ATP** — energy\n\n## Action items\n- [ ] read ch. 4");

} // namespace

class LiveNotesExtrasTest : public QObject {
    Q_OBJECT
private slots:
    void termsAndActionItemsParse();
    void actionItemsBecomeIcs();
    void splitByTopicKeepsTheIndex();
    void ankiCardsFromTables();
    void unclearSpotsAndSectionTimes();
    void promptCarriesFlagsNamesJargonAndContext();
    void engineFlagsNamesScreensAndUsage();
};

void LiveNotesExtrasTest::termsAndActionItemsParse()
{
    QCOMPARE(Extras::parseTerms(QStringLiteral("Here you go:\n```json\n[\"Krebs cycle\", \"ATP\", \"atp\", \"\"]\n```")),
             (QStringList{QStringLiteral("Krebs cycle"), QStringLiteral("ATP")}));
    QVERIFY(Extras::termsRequest(QStringLiteral("x")).systemPrompt.contains(QStringLiteral("JSON array")));
    const auto items = Extras::parseActionItems(QStringLiteral(
        R"([{"title":"Read chapter 4","owner":"","due":"2026-10-02","notes":"for the quiz"},
            {"title":"Send slides","owner":"Maria","due":"","notes":""},{"title":""}])"));
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.first().due, QDate(2026, 10, 2));
    QCOMPARE(items.at(1).owner, QStringLiteral("Maria"));
    const ChatRequest request = Extras::actionItemsRequest(kNotes, QDate(2026, 9, 27));
    QVERIFY(request.systemPrompt.contains(QStringLiteral("2026-09-27 (Sunday)")));
}

void LiveNotesExtrasTest::actionItemsBecomeIcs()
{
    const QByteArray ics = Extras::actionItemsIcs(
        {{QStringLiteral("Read, carefully; chapter 4"), QString(), QDate(2026, 10, 2), QStringLiteral("quiz")},
         {QStringLiteral("Send slides"), QStringLiteral("Maria"), QDate(), QString()}},
        QStringLiteral("Biology 101"));
    QCOMPARE(ics.count("BEGIN:VTODO"), 2);
    QCOMPARE(ics.count("BEGIN:VEVENT"), 1); // only the dated one
    QVERIFY(ics.contains("SUMMARY:Read\\, carefully\\; chapter 4"));
    QVERIFY(ics.contains("DUE;VALUE=DATE:20261002"));
    QVERIFY(ics.contains("DTEND;VALUE=DATE:20261003"));
    QVERIFY(ics.contains("SUMMARY:Maria: Send slides"));
    QVERIFY(ics.contains("From: Biology 101"));
}

void LiveNotesExtrasTest::splitByTopicKeepsTheIndex()
{
    const Extras::SplitNotes split = Extras::splitByTopic(kNotes, QStringLiteral("Biology"));
    QCOMPARE(split.topics.size(), 2);
    QCOMPARE(split.topics.first().title, QStringLiteral("Mitochondria"));
    QVERIFY(split.topics.first().markdown.startsWith(QStringLiteral("# Mitochondria [01:10]\n\n- make ATP")));
    QVERIFY(split.index.startsWith(QStringLiteral("# Biology\n\n## Summary")));
    QVERIFY(split.index.contains(QStringLiteral("## Topics\n1. Mitochondria")));
    QVERIFY(split.index.contains(QStringLiteral("## Definitions")));   // stays on the index
    QVERIFY(split.index.contains(QStringLiteral("## Action items")));
    QVERIFY(!split.index.contains(QStringLiteral("light reactions")));
}

void LiveNotesExtrasTest::ankiCardsFromTables()
{
    const QString page = QStringLiteral(
        "# Study\n\n## Flashcards\n| Front | Back |\n|---|---|\n| What makes ATP? | Mitochondria |\n"
        "| ATP\tstands for | adenosine triphosphate |\n\n| Other | Table |\n|---|---|\n| x | y |\n");
    const QString tsv = Extras::ankiTsv(page);
    QVERIFY(tsv.startsWith(QStringLiteral("#separator:tab\n")));
    QVERIFY(tsv.contains(QStringLiteral("What makes ATP?\tMitochondria\n")));
    QVERIFY(tsv.contains(QStringLiteral("ATP stands for\tadenosine triphosphate")));
    QVERIFY(!tsv.contains(QStringLiteral("x\ty"))); // not a card table
    QVERIFY(Extras::ankiTsv(kNotes).isEmpty());
}

void LiveNotesExtrasTest::unclearSpotsAndSectionTimes()
{
    const auto spots = Extras::unclearSpots(kNotes);
    QCOMPARE(spots.size(), 2);
    QCOMPARE(spots.first().ms, 102000);
    QCOMPARE(spots.first().guess, QStringLiteral("cristae?"));
    QCOMPARE(spots.first().text, QStringLiteral("*(unclear at 01:42: cristae?)*"));
    QCOMPARE(spots.at(1).ms, -1);
    const auto times = Extras::sectionTimes(kNotes);
    QCOMPARE(times.size(), 2);
    QCOMPARE(times.at(1).heading, QStringLiteral("Photosynthesis"));
    QCOMPARE(times.at(1).ms, 300000);
    QCOMPARE(Extras::parseTimestamp(QStringLiteral("1:02:03")), 3723000);
    QCOMPARE(Extras::parseTimestamp(QStringLiteral("x")), -1);
}

void LiveNotesExtrasTest::promptCarriesFlagsNamesJargonAndContext()
{
    TranscriptLine line{65000, QStringLiteral("we ship Friday")};
    line.speaker = QStringLiteral("Speaker 2");
    line.flagged = true;
    QCOMPARE(LiveNotesPrompt::formatLines({line}, {{QStringLiteral("Speaker 2"), QStringLiteral("Maria")}}),
             QStringLiteral("[01:05] ‼ Maria (Speaker 2): we ship Friday"));
    NotesBriefing briefing;
    briefing.explainJargon = true;
    SessionContext context;
    context.reference = QStringLiteral("Slide 1: Krebs cycle");
    context.previousNotes = QStringLiteral("# Lecture 3");
    const QString prompt = LiveNotesPrompt::systemPrompt(2, briefing, context);
    for (const char *text : {"‼ marks a moment the USER flagged", "[[ai:", "REFERENCE MATERIAL",
                             "Slide 1: Krebs cycle", "EARLIER SESSIONS", "# Lecture 3", "unclear at mm:ss"}) {
        QVERIFY2(prompt.contains(QString::fromUtf8(text)), text);
    }
    QVERIFY(!LiveNotesPrompt::systemPrompt().contains(QStringLiteral("[[ai:")));
    QCOMPARE(NotesBriefing::fromMap(briefing.toMap()).explainJargon, true);
}

void LiveNotesExtrasTest::engineFlagsNamesScreensAndUsage()
{
    Provider provider;
    provider.replies = {QStringLiteral("# A"), QStringLiteral("# A with names")};
    QString notes;
    LiveNotesEngine engine;
    LiveNotesEngine::Options options;
    options.minWords = 1;
    options.minIntervalMs = 0;
    engine.setOptions(options);
    engine.setProvider(&provider);
    engine.setNotesAccess([&notes] { return notes; }, [&notes](const QString &n) { notes = n; return true; });
    engine.start();
    engine.addSegment(QStringLiteral("first words"), 0, QStringLiteral("Speaker 1"));
    engine.flagMoment();
    engine.addSegment(QStringLiteral("the important part"), 3000, QStringLiteral("Speaker 1"));
    QVERIFY(engine.transcript().at(0).flagged);   // being heard when flagged
    QVERIFY(engine.transcript().at(1).flagged);   // and the next line
    QImage screen(64, 36, QImage::Format_RGB32);
    screen.fill(Qt::white);
    engine.addScreen(screen);
    engine.addScreen(screen);
    engine.addScreen(screen); // only the newest two ride along
    engine.tick();
    QCOMPARE(provider.requests.first().messages.first().images.size(), 2);
    QVERIFY(provider.requests.first().messages.first().text.contains(QStringLiteral("‼")));
    QTRY_COMPARE(notes, QStringLiteral("# A"));
    QCOMPARE(engine.usage().requests, 1);
    QCOMPARE(engine.usage().inputTokens, 120);
    QCOMPARE(engine.usage().screens, 2);
    // Naming a speaker rewrites the notes with no new speech.
    engine.setSpeakerNames({{QStringLiteral("Speaker 1"), QStringLiteral("Dr. Lee")}});
    engine.tick();
    QCOMPARE(provider.requests.size(), 2);
    QVERIFY(provider.requests.last().messages.first().text.contains(QStringLiteral("Speaker 1 = Dr. Lee")));
    QTRY_COMPARE(notes, QStringLiteral("# A with names"));
    engine.addGlossary({QStringLiteral("Krebs"), QStringLiteral("krebs")});
    QCOMPARE(engine.options().glossary, QStringList{QStringLiteral("Krebs")});
}

QTEST_GUILESS_MAIN(LiveNotesExtrasTest)

#include "tst_live_notes_extras.moc"
