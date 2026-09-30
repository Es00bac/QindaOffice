// SPDX-License-Identifier: GPL-3.0-or-later
// Live notes in office_ai (D-057): the prompt and the long-notes merge,
// and the engine's cadence, gate and user-edits-win rules against a
// scripted provider and a local System One server. The provider plumbing
// borrowed from OpenCode is tst_ai_providers.cpp.

#include "live_notes_engine.h"
#include "live_notes_prompt.h"
#include "system_one_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace QindaOffice::Ai;

namespace {

// A provider whose runs answer with the next scripted reply.
class ScriptedRun final : public ChatRun {
    Q_OBJECT
public:
    ScriptedRun(QObject *parent, QString reply, bool fail)
        : ChatRun(parent)
    {
        QTimer::singleShot(0, this, [this, reply, fail] {
            if (fail) {
                emitFailed({ChatError::Unavailable, QStringLiteral("down"), true});
            } else {
                emitFinished({reply, {}});
            }
        });
    }
    void cancel() override { emitFailed({ChatError::Cancelled, QStringLiteral("cancelled"), false}); }
};

class ScriptedProvider final : public ChatProvider {
    Q_OBJECT
public:
    QStringList replies;
    QList<ChatRequest> requests;
    bool fail = false;
    ChatRun *start(const ChatRequest &request) override
    {
        requests.append(request);
        const QString reply = replies.isEmpty() ? QString() : replies.takeFirst();
        return new ScriptedRun(this, reply, fail);
    }
};

// Answers every POST with `body` (a System One reply) and records requests.
class JsonServer final : public QTcpServer {
    Q_OBJECT
public:
    QByteArray body;
    QList<QByteArray> requests;
    explicit JsonServer(QByteArray reply) : body(std::move(reply))
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *socket = nextPendingConnection();
            auto *buffer = new QByteArray;
            connect(socket, &QObject::destroyed, [buffer] { delete buffer; });
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                buffer->append(socket->readAll());
                const qsizetype end = buffer->indexOf("\r\n\r\n");
                const qsizetype at = buffer->toLower().indexOf("content-length:");
                if (end < 0 || at < 0) {
                    return;
                }
                const qsizetype length = buffer->mid(at + 15, buffer->indexOf("\r\n", at) - at - 15)
                                             .trimmed().toLongLong();
                if (buffer->size() - end - 4 < length) {
                    return;
                }
                requests.append(buffer->mid(end + 4));
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }
};

QByteArray gateReply(double substance, const char *topic, double importance)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("answers"),
         QJsonObject{{QStringLiteral("substance"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("noul")},
                                  {QStringLiteral("probabilities"),
                                   QJsonObject{{QStringLiteral("true"), substance},
                                               {QStringLiteral("false"), 1 - substance}}}}},
                     {QStringLiteral("topic"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("choice")},
                                  {QStringLiteral("choice"), QString::fromLatin1(topic)},
                                  {QStringLiteral("confidence"), 0.9}}},
                     {QStringLiteral("importance"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("score")},
                                  {QStringLiteral("score"), importance},
                                  {QStringLiteral("confidence"), 0.8}}}}}}).toJson();
}

QString words(int n, const QString &word = QStringLiteral("cell"))
{
    QStringList out;
    for (int i = 0; i < n; ++i) {
        out.append(word);
    }
    return out.join(QLatin1Char(' '));
}

struct Page {
    QString notes;
    bool refuse = false;
};

void wire(LiveNotesEngine &engine, Page &page)
{
    engine.setNotesAccess([&page] { return page.notes; },
                          [&page](const QString &notes) {
                              if (page.refuse) {
                                  return false;
                              }
                              page.notes = notes;
                              return true;
                          });
}

} // namespace

class LiveNotesTest : public QObject {
    Q_OBJECT
private slots:
    void promptCarriesNotesAndNewTranscript();
    void sanitizeStripsThinkingFencesAndPreamble();
    void tailModeSplicesTheEnd();
    void implausibleRevisionIsRefused();
    void engineWaitsForEnoughWordsThenUpdates();
    void engineKeepsTheUsersEdit();
    void engineRetriesWhileTheEditorHoldsTheBlock();
    void engineFinalRevisionUsesTheWholeTranscript();
    void detailLevelsShapeThePromptAndRestyle();
    void briefingShapesThePrompt();
    void gateSkipsFillerAndMarksKeyPoints();
    void gateFailureFailsOpen();
    void systemOneAnswersParse();
};

void LiveNotesTest::promptCarriesNotesAndNewTranscript()
{
    LiveNotesPrompt::Request request;
    request.notes = QStringLiteral("# Biology\n\n## Summary\n- cells");
    request.context = {{1000, QStringLiteral("old line")}};
    TranscriptLine fresh{65000, QStringLiteral("mitochondria make ATP")};
    fresh.key = true;
    fresh.newTopic = true;
    request.fresh = {fresh};
    request.topic = QStringLiteral("Biology 101");
    request.glossary = {QStringLiteral("Jev"), QStringLiteral("ElevenLabs")};
    const QString message = LiveNotesPrompt::userMessage(request);
    QVERIFY(message.contains(QStringLiteral("CURRENT NOTES:\n# Biology")));
    QVERIFY(message.contains(QStringLiteral("[00:01] old line")));
    QVERIFY(message.contains(QStringLiteral("[01:05] ⤷ ★ mitochondria make ATP")));
    QVERIFY(message.contains(QStringLiteral("Biology 101")));
    QVERIFY(message.contains(QStringLiteral("Correct spellings of names and terms (fix mishearings): Jev, ElevenLabs")));
    QVERIFY(message.endsWith(QStringLiteral("Reply with the complete revised notes.")));
    const QString system = LiveNotesPrompt::systemPrompt();
    for (const char *rule : {"## Summary", "==key takeaway==", "## Action items", "**bold**",
                             "> [!quote]", "BY SUBJECT", "EDIT SCRIPTS", "=={term}"}) {
        QVERIFY2(system.contains(QString::fromUtf8(rule)), rule);
    }
    NotesBriefing plain;
    plain.style = QStringLiteral("plain");
    QVERIFY(LiveNotesPrompt::systemPrompt(2, plain).contains(QStringLiteral("> \"")));
    QVERIFY(!LiveNotesPrompt::systemPrompt(2, plain).contains(QStringLiteral("[!big]")));
    QCOMPARE(LiveNotesPrompt::formatTime(3723000), QStringLiteral("1:02:03"));
    TranscriptLine spoken{5000, QStringLiteral("we ship Friday")};
    spoken.speaker = QStringLiteral("Speaker 2");
    QCOMPARE(LiveNotesPrompt::formatLines({spoken}), QStringLiteral("[00:05] Speaker 2: we ship Friday"));
    QVERIFY(system.contains(QStringLiteral("speaker label")));
}

void LiveNotesTest::sanitizeStripsThinkingFencesAndPreamble()
{
    QCOMPARE(LiveNotesPrompt::sanitizeReply(
                 QStringLiteral("<think>plan the notes</think>\n```markdown\n# T\n- a\n```")),
             QStringLiteral("# T\n- a"));
    QCOMPARE(LiveNotesPrompt::sanitizeReply(QStringLiteral("Here are the updated notes:\n\n# T\n- a")),
             QStringLiteral("# T\n- a"));
    // A cut-off think block is all noise; a bullet before the title is content.
    QCOMPARE(LiveNotesPrompt::sanitizeReply(QStringLiteral("# T\n<think>never closed")),
             QStringLiteral("# T"));
    QCOMPARE(LiveNotesPrompt::sanitizeReply(QStringLiteral("- first\n# T")),
             QStringLiteral("- first\n# T"));
}

void LiveNotesTest::tailModeSplicesTheEnd()
{
    QString notes = QStringLiteral("# Lecture\n\n## Summary\n- old summary");
    for (int i = 1; i <= 5; ++i) {
        notes += QStringLiteral("\n\n## Topic %1 [0%1:00]\n").arg(i) + words(300, QStringLiteral("detail"));
    }
    notes += QStringLiteral("\n\n## Action items\n- [ ] read chapter 2");
    QVERIFY(LiveNotesPrompt::useTailMode(notes));
    LiveNotesPrompt::Request request;
    request.notes = notes;
    request.fresh = {{360000, QStringLiteral("a sixth topic begins")}};
    const QString message = LiveNotesPrompt::userMessage(request);
    QVERIFY(message.contains(QStringLiteral("EARLIER SECTIONS (fixed; shown as headings only):\n## Topic 1")));
    QVERIFY(!message.contains(QStringLiteral("CURRENT NOTES")));

    const QString reply = QStringLiteral(
        "## Summary\n- new summary\n\n## Topic 4 [04:00]\n- revised four\n\n## Topic 5 [05:00]\n"
        "- revised five\n\n## Topic 6 [06:00]\n- brand new");
    const QString merged = LiveNotesPrompt::mergeTail(notes, reply);
    const NotesParts parts = LiveNotesPrompt::split(merged);
    QCOMPARE(parts.preamble, QStringLiteral("# Lecture"));
    QCOMPARE(parts.summary, QStringLiteral("## Summary\n- new summary"));
    QCOMPARE(parts.sections.size(), 6);
    QVERIFY(parts.sections.at(0).startsWith(QStringLiteral("## Topic 1"))); // untouched
    QCOMPARE(parts.sections.at(3), QStringLiteral("## Topic 4 [04:00]\n- revised four"));
    QCOMPARE(parts.sections.at(5), QStringLiteral("## Topic 6 [06:00]\n- brand new"));
    // The reply forgot the action items: they are kept.
    QCOMPARE(parts.actions, QStringLiteral("## Action items\n- [ ] read chapter 2"));
    QVERIFY(LiveNotesPrompt::mergeTail(notes, QStringLiteral("just prose")).isEmpty());
}

void LiveNotesTest::implausibleRevisionIsRefused()
{
    QString why;
    QVERIFY(!LiveNotesPrompt::plausibleRevision(words(200), QStringLiteral("# tiny"), &why));
    QVERIFY(!why.isEmpty());
    QVERIFY(!LiveNotesPrompt::plausibleRevision(QStringLiteral("a"), QStringLiteral("  "), &why));
    QVERIFY(LiveNotesPrompt::plausibleRevision(QStringLiteral("short"), QStringLiteral("# T"), &why));
}

void LiveNotesTest::engineWaitsForEnoughWordsThenUpdates()
{
    ScriptedProvider provider;
    provider.replies = {QStringLiteral("# Cells\n\n## Summary\n- cells are small")};
    Page page;
    LiveNotesEngine engine;
    LiveNotesEngine::Options options;
    options.minWords = 20;
    options.minIntervalMs = 0;
    engine.setOptions(options);
    engine.setProvider(&provider);
    wire(engine, page);
    QSignalSpy applied(&engine, &LiveNotesEngine::notesApplied);
    engine.start();
    engine.addSegment(words(10), 0);
    engine.tick();
    QCOMPARE(provider.requests.size(), 0); // 10 < 20 words
    engine.addSegment(words(15), 4000);
    QCOMPARE(engine.pendingWords(), 25);
    engine.tick();
    QCOMPARE(provider.requests.size(), 1);
    QVERIFY(engine.busy());
    engine.tick();
    QCOMPARE(provider.requests.size(), 1); // never two at once
    QTRY_COMPARE(applied.size(), 1);
    QCOMPARE(page.notes, QStringLiteral("# Cells\n\n## Summary\n- cells are small"));
    QCOMPARE(engine.pendingWords(), 0);
    const ChatRequest request = provider.requests.first();
    QVERIFY(request.systemPrompt.contains(QStringLiteral("live note-taker")));
    QVERIFY(request.messages.first().text.contains(QStringLiteral("(none yet)")));
}

void LiveNotesTest::engineKeepsTheUsersEdit()
{
    ScriptedProvider provider;
    provider.replies = {QStringLiteral("# Model notes"), QStringLiteral("# Model notes, with the edit")};
    Page page;
    LiveNotesEngine engine;
    LiveNotesEngine::Options options;
    options.minWords = 5;
    options.minIntervalMs = 0;
    engine.setOptions(options);
    engine.setProvider(&provider);
    wire(engine, page);
    QSignalSpy status(&engine, &LiveNotesEngine::status);
    engine.start();
    engine.addSegment(words(10), 0);
    engine.tick();
    // The user types while the model is thinking.
    page.notes = QStringLiteral("# My own heading");
    QTRY_VERIFY(!engine.busy());
    QCOMPARE(page.notes, QStringLiteral("# My own heading"));
    QCOMPARE(engine.updateCount(), 0);
    QCOMPARE(engine.pendingWords(), 10); // the same material goes again
    engine.tick();
    QTRY_COMPARE(engine.updateCount(), 1);
    QVERIFY(provider.requests.at(1).messages.first().text.contains(QStringLiteral("# My own heading")));
}

void LiveNotesTest::engineRetriesWhileTheEditorHoldsTheBlock()
{
    ScriptedProvider provider;
    provider.replies = {QStringLiteral("# Notes")};
    Page page;
    page.refuse = true;
    LiveNotesEngine engine;
    LiveNotesEngine::Options options;
    options.minWords = 1;
    options.minIntervalMs = 0;
    engine.setOptions(options);
    engine.setProvider(&provider);
    wire(engine, page);
    engine.start();
    engine.addSegment(words(3), 0);
    engine.tick();
    QTRY_VERIFY(!engine.busy());
    QCOMPARE(engine.updateCount(), 0);
    page.refuse = false; // the editor closed
    engine.tick();
    QCOMPARE(page.notes, QStringLiteral("# Notes"));
    QCOMPARE(engine.updateCount(), 1);
    QCOMPARE(provider.requests.size(), 1);
}

void LiveNotesTest::engineFinalRevisionUsesTheWholeTranscript()
{
    ScriptedProvider provider;
    provider.replies = {QStringLiteral("# First"), QStringLiteral("# Final, from everything")};
    Page page;
    LiveNotesEngine engine;
    LiveNotesEngine::Options options;
    options.minWords = 5;
    options.minIntervalMs = 0;
    options.editMode = false; // the M52 path; edit mode's final pass is in tst_live_notes_edits
    engine.setOptions(options);
    engine.setProvider(&provider);
    wire(engine, page);
    QSignalSpy finalized(&engine, &LiveNotesEngine::finalized);
    engine.start();
    engine.addSegment(QStringLiteral("the first thing said in the lecture"), 0);
    engine.tick();
    QTRY_COMPARE(engine.updateCount(), 1);
    engine.addSegment(QStringLiteral("and the last"), 90000);
    engine.finalize();
    QTRY_COMPARE(finalized.size(), 1);
    QCOMPARE(page.notes, QStringLiteral("# Final, from everything"));
    const QString last = provider.requests.last().messages.first().text;
    QVERIFY(last.contains(QStringLiteral("FULL TRANSCRIPT")));
    QVERIFY(last.contains(QStringLiteral("the first thing said")));
    QVERIFY(last.contains(QStringLiteral("[01:30] and the last")));
}

void LiveNotesTest::detailLevelsShapeThePromptAndRestyle()
{
    QVERIFY(LiveNotesPrompt::systemPrompt(1).contains(QStringLiteral("KEY POINTS")));
    QVERIFY(LiveNotesPrompt::systemPrompt().contains(QStringLiteral("CONCISE"))); // the default
    QVERIFY(LiveNotesPrompt::systemPrompt(5).contains(QStringLiteral("COMPREHENSIVE")));
    QCOMPARE(LiveNotesPrompt::clampDetail(9), 5);

    ScriptedProvider provider;
    const QString verbose = words(300, QStringLiteral("verbose"));
    provider.replies = {verbose, QStringLiteral("# Short")};
    Page page;
    LiveNotesEngine engine;
    LiveNotesEngine::Options options;
    options.minWords = 5;
    options.minIntervalMs = 0;
    options.detail = 4;
    engine.setOptions(options);
    engine.setProvider(&provider);
    wire(engine, page);
    engine.start();
    engine.addSegment(words(10), 0);
    engine.tick();
    QTRY_COMPARE(page.notes, verbose);
    QVERIFY(provider.requests.first().systemPrompt.contains(QStringLiteral("DETAILED")));
    // The slider moves to Key points: a rewrite runs with NO new speech,
    // and shrinking to a tenth is accepted (the guard allows a restyle).
    engine.setDetail(1);
    engine.tick();
    QCOMPARE(provider.requests.size(), 2);
    QVERIFY(provider.requests.last().systemPrompt.contains(QStringLiteral("KEY POINTS")));
    QVERIFY(provider.requests.last().messages.first().text.contains(QStringLiteral("changed the detail level")));
    QTRY_COMPARE(page.notes, QStringLiteral("# Short"));
}

void LiveNotesTest::briefingShapesThePrompt()
{
    const QString plain = LiveNotesPrompt::systemPrompt();
    QVERIFY(plain.contains(QStringLiteral("[12:40]")));             // timestamps on by default
    QVERIFY(plain.contains(QStringLiteral("## Action items")));
    QVERIFY(!plain.contains(QStringLiteral("SESSION BRIEFING")));    // nothing to brief

    NotesBriefing briefing;
    briefing.scenario = QStringLiteral("meeting");
    briefing.focus = QStringLiteral("the launch date");
    briefing.skip = QStringLiteral("small talk");
    briefing.custom = QStringLiteral("write in Spanish");
    briefing.timestamps = false;
    briefing.actionItems = false;
    briefing.quotes = false;
    briefing.openQuestions = true;
    briefing.definitions = true;
    const QString prompt = LiveNotesPrompt::systemPrompt(2, briefing);
    for (const char *text : {"No timestamps", "No quotes", "No `## Action items`", "## Open questions",
                             "## Definitions", "This is a MEETING", "FOCUS ON: the launch date",
                             "LEAVE OUT: small talk", "THE USER'S OWN INSTRUCTIONS: write in Spanish",
                             "CONCISE"}) {
        QVERIFY2(prompt.contains(QString::fromUtf8(text)), text);
    }
    QVERIFY(!prompt.contains(QStringLiteral("[12:40]")));
    // Persisted as a map, round trip.
    QCOMPARE(NotesBriefing::fromMap(briefing.toMap()), briefing);
    QCOMPARE(LiveNotesBriefing::scenarios().size(), 6);
    for (const auto &scenario : LiveNotesBriefing::scenarios()) {
        QVERIFY(scenario.id == QLatin1String("general") || !LiveNotesBriefing::scenarioBrief(scenario.id).isEmpty());
    }
    // The engine sends it.
    ScriptedProvider provider;
    provider.replies = {QStringLiteral("# N")};
    Page page;
    LiveNotesEngine engine;
    LiveNotesEngine::Options options;
    options.minWords = 1;
    options.minIntervalMs = 0;
    options.briefing = briefing;
    engine.setOptions(options);
    engine.setProvider(&provider);
    wire(engine, page);
    engine.start();
    engine.addSegment(words(3), 0);
    engine.tick();
    QVERIFY(provider.requests.first().systemPrompt.contains(QStringLiteral("FOCUS ON: the launch date")));
    QTRY_VERIFY(!engine.busy());
}

void LiveNotesTest::gateSkipsFillerAndMarksKeyPoints()
{
    JsonServer filler(gateReply(0.05, "continues", 0.2));
    QVERIFY(filler.listen(QHostAddress::LocalHost));
    SystemOneClient gate(QStringLiteral("http://127.0.0.1:%1/v1").arg(filler.serverPort()),
                         QStringLiteral("gk"), QStringLiteral("english"));
    QVERIFY(!gate.offMachine());
    ScriptedProvider provider;
    Page page;
    LiveNotesEngine engine;
    LiveNotesEngine::Options options;
    options.minWords = 10;
    options.minIntervalMs = 0;
    options.gateBatchWords = 10;
    engine.setOptions(options);
    engine.setProvider(&provider);
    engine.setGate(&gate);
    wire(engine, page);
    engine.start();
    engine.addSegment(QStringLiteral("this video is sponsored by a very generous company thanks"), 0);
    QTRY_COMPARE(filler.requests.size(), 1);
    QTRY_VERIFY(engine.transcript().first().filler);
    QCOMPARE(engine.pendingWords(), 0); // filler does not count
    engine.tick();
    QCOMPARE(provider.requests.size(), 0);
    const QJsonObject sent = QJsonDocument::fromJson(filler.requests.first()).object();
    QCOMPARE(sent.value(QStringLiteral("model")).toString(), QStringLiteral("english"));
    QVERIFY(sent.value(QStringLiteral("questions")).toObject().contains(QStringLiteral("substance")));

    // A new, important topic: counted, marked, and it triggers promptly.
    filler.body = gateReply(0.95, "new_topic", 3.0);
    provider.replies = {QStringLiteral("# Notes")};
    engine.addSegment(QStringLiteral("now the krebs cycle which is central to how cells breathe today"), 30000);
    QTRY_VERIFY(engine.transcript().last().key);
    QVERIFY(engine.transcript().last().newTopic);
    engine.tick();
    QCOMPARE(provider.requests.size(), 1);
    QVERIFY(provider.requests.first().messages.first().text.contains(QStringLiteral("⤷ ★ now the krebs cycle")));
    QVERIFY(provider.requests.first().messages.first().text.contains(QStringLiteral("(filler) this video")));
}

void LiveNotesTest::gateFailureFailsOpen()
{
    // Nothing listens on this port: the gate errors, and every word counts.
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost));
    const quint16 port = probe.serverPort();
    probe.close();
    SystemOneClient gate(QStringLiteral("http://127.0.0.1:%1").arg(port), QString(), QString());
    LiveNotesEngine engine;
    LiveNotesEngine::Options options;
    options.gateBatchWords = 3;
    engine.setOptions(options);
    engine.setGate(&gate);
    QSignalSpy status(&engine, &LiveNotesEngine::status);
    engine.start();
    engine.addSegment(words(5), 0);
    QTRY_VERIFY(!status.isEmpty());
    QVERIFY(status.last().first().toString().contains(QStringLiteral("gate")));
    QCOMPARE(engine.pendingWords(), 5);
}

void LiveNotesTest::systemOneAnswersParse()
{
    const auto answers = SystemOneClient::parseAnswers(
        QJsonDocument::fromJson(gateReply(0.8, "wrap_up", 2.0)).object());
    QCOMPARE(answers.value(QStringLiteral("substance")).probability, 0.8);
    QCOMPARE(answers.value(QStringLiteral("topic")).choice, QStringLiteral("wrap_up"));
    QCOMPARE(answers.value(QStringLiteral("importance")).score, 2.0);
    // Jev's noul shape: a value field.
    // Laya's noul shape: the probability in "noul".
    const auto laya = SystemOneClient::parseAnswers(QJsonDocument::fromJson(
        R"({"answers":{"q":{"type":"noul","noul":0.8106,"confidence":0.8}}})").object());
    QCOMPARE(laya.value(QStringLiteral("q")).probability, 0.8106);
    const auto jev = SystemOneClient::parseAnswers(QJsonDocument::fromJson(
        R"({"answers":{"q":{"type":"noul","value":0.25,"confidence":0.9}}})").object());
    QCOMPARE(jev.value(QStringLiteral("q")).probability, 0.25);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);
    LiveNotesTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_live_notes.moc"
