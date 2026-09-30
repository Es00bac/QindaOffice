// SPDX-License-Identifier: GPL-3.0-or-later
// M57 (D-063): the section-edit script live notes revise with — parsing the
// "=== OP" lines and applying them so points land in the section they
// belong to, headings re-levelled, trailing sections kept last, and a
// missing target degrading to "keep the content" rather than a loss.

#include "live_notes_edits.h"
#include "live_notes_engine.h"
#include "live_notes_layouts.h"
#include "live_notes_prompt.h"

#include <QtTest>

using namespace QindaOffice::Ai;
namespace Edits = LiveNotesEdits;

namespace {

class Run final : public ChatRun {
    Q_OBJECT
public:
    Run(QObject *parent, QString reply) : ChatRun(parent)
    {
        QTimer::singleShot(0, this, [this, reply] { emitFinished({reply, {}}); });
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
    "# Biology\n\n## Summary\n- cells\n\n## Cell structure [01:10]\n- membranes\n\n"
    "### Nucleus\n- DNA\n\n## Photosynthesis [05:00]\n- light\n\n## Definitions\n- **ATP** — energy\n\n"
    "## Action items\n- [ ] read ch. 4");

QString apply(const QString &script, QStringList *remarks = nullptr)
{
    const Edits::Result result = Edits::apply(kNotes, Edits::parse(script));
    if (remarks != nullptr) {
        *remarks = result.remarks;
    }
    return result.notes;
}

} // namespace

class LiveNotesEditsTest : public QObject {
    Q_OBJECT
private slots:
    void parsesOps();
    void appendLandsInTheOldSection();
    void newSectionsAreFiledAndLevelled();
    void replaceAndDelete();
    void moveRefilesWithLevels();
    void summaryTitleTimelineActions();
    void missingTargetsKeepTheContent();
    void layoutsShapeThePrompt();
    void engineFilesEditsIntoTheNotes();
    void finalPassTidiesWithAnEditScript();
    void ambiguousTargetsAndNoChange();
};

void LiveNotesEditsTest::ambiguousTargetsAndNoChange()
{
    bool script = false;
    QVERIFY(Edits::parse(QStringLiteral("=== END"), &script).isEmpty());
    QVERIFY(script);
    const QString notes = QStringLiteral("# Investigation\n\n## Network client\n- client fact\n\n## Network server\n- server fact");
    const auto result = Edits::apply(notes, Edits::parse(QStringLiteral("=== DELETE ## Network")));
    QCOMPARE(result.applied, 0);
    QVERIFY(result.notes.contains(QStringLiteral("client fact")));
    QVERIFY(result.notes.contains(QStringLiteral("server fact")));
    Provider provider;
    provider.replies = {QStringLiteral("=== END")};
    QString current = notes;
    LiveNotesEngine engine;
    engine.setProvider(&provider);
    engine.setNotesAccess([&current] { return current; }, [&current](const QString &text) { current = text; return true; });
    QSignalSpy done(&engine, &LiveNotesEngine::finalized), failed(&engine, &LiveNotesEngine::failed);
    engine.start();
    engine.finalize();
    engine.tick();
    QTRY_COMPARE(done.size(), 1);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(provider.requests.size(), 1);
}

void LiveNotesEditsTest::parsesOps()
{
    bool script = true;
    QVERIFY(Edits::parse(QStringLiteral("# Notes\n\n## A\n- x"), &script).isEmpty());
    QVERIFY(!script);
    const auto ops = Edits::parse(QStringLiteral(
        "=== APPEND ## Photosynthesis [05:00]\n- a\n\n=== new under ## Cell structure\n### Ribosomes\n"
        "- b\n=== MOVE ## Nucleus TO AFTER ## Photosynthesis\n=== NEW\n## Genetics\n=== END"), &script);
    QVERIFY(script);
    QCOMPARE(ops.size(), 4);
    QCOMPARE(ops.at(0).kind, Edits::Op::Append);
    QCOMPARE(ops.at(0).target, QStringLiteral("## Photosynthesis [05:00]"));
    QCOMPARE(ops.at(0).body, QStringLiteral("- a"));
    QCOMPARE(ops.at(1).kind, Edits::Op::NewUnder);
    QCOMPARE(ops.at(1).target, QStringLiteral("## Cell structure"));
    QCOMPARE(ops.at(2).kind, Edits::Op::Move);
    QCOMPARE(ops.at(2).target, QStringLiteral("## Nucleus"));
    QVERIFY(!ops.at(2).moveUnder);
    QCOMPARE(ops.at(2).destination, QStringLiteral("## Photosynthesis"));
    QCOMPARE(ops.at(3).kind, Edits::Op::NewAtEnd);
    QCOMPARE(Edits::normalizeHeading(QStringLiteral("## **Cell** structure [1:02:03]")),
             QStringLiteral("cell structure"));
}

void LiveNotesEditsTest::appendLandsInTheOldSection()
{
    // Said at 40:00, filed under the section opened at 01:10, above its
    // subsection; the heading is matched without its timestamp.
    const QString notes = apply(QStringLiteral("=== APPEND ## cell structure\n- walls in plants"));
    QVERIFY2(notes.contains(QStringLiteral("- membranes\n\n- walls in plants\n\n### Nucleus"))
                 || notes.contains(QStringLiteral("- membranes\n- walls in plants\n\n### Nucleus")),
             qPrintable(notes));
}

void LiveNotesEditsTest::newSectionsAreFiledAndLevelled()
{
    const QString notes = apply(QStringLiteral(
        "=== NEW UNDER ## Cell structure\n## Mitochondria\n- ATP\n=== NEW\n# Respiration\n- glucose"));
    // Under: re-levelled to ###, after the existing subsection.
    QVERIFY(notes.indexOf(QStringLiteral("### Mitochondria")) > notes.indexOf(QStringLiteral("### Nucleus")));
    QVERIFY(notes.indexOf(QStringLiteral("### Mitochondria")) < notes.indexOf(QStringLiteral("## Photosynthesis")));
    // At end: a ## section above Definitions and Action items.
    QVERIFY(notes.contains(QStringLiteral("## Respiration")));
    QVERIFY(notes.indexOf(QStringLiteral("## Respiration")) > notes.indexOf(QStringLiteral("## Photosynthesis")));
    QVERIFY(notes.indexOf(QStringLiteral("## Respiration")) < notes.indexOf(QStringLiteral("## Definitions")));
    QVERIFY(!notes.contains(QStringLiteral("\n\n\n")));
}

void LiveNotesEditsTest::replaceAndDelete()
{
    QString notes = apply(QStringLiteral("=== REPLACE ## Photosynthesis\n### Photosynthesis [05:00]\n"
                                         "- light and dark reactions"));
    QVERIFY(notes.contains(QStringLiteral("## Photosynthesis [05:00]\n\n- light and dark reactions"))
                || notes.contains(QStringLiteral("## Photosynthesis [05:00]\n- light and dark reactions")));
    QVERIFY(!notes.contains(QStringLiteral("### Photosynthesis")));
    QVERIFY(!notes.contains(QStringLiteral("- light\n")));
    // A body without a heading keeps the old heading.
    notes = apply(QStringLiteral("=== REPLACE ## Nucleus\n- DNA and RNA"));
    QVERIFY(notes.contains(QStringLiteral("### Nucleus")));
    QVERIFY(notes.contains(QStringLiteral("- DNA and RNA")));
    // Deleting a section takes its subsections with it.
    notes = apply(QStringLiteral("=== DELETE ## Cell structure"));
    QVERIFY(!notes.contains(QStringLiteral("Nucleus")));
    QVERIFY(notes.contains(QStringLiteral("## Photosynthesis")));
}

void LiveNotesEditsTest::moveRefilesWithLevels()
{
    QString notes = apply(QStringLiteral("=== MOVE ## Nucleus TO AFTER ## Photosynthesis"));
    QVERIFY(notes.contains(QStringLiteral("## Nucleus")));
    QVERIFY(!notes.contains(QStringLiteral("### Nucleus")));
    QVERIFY(notes.indexOf(QStringLiteral("## Nucleus")) > notes.indexOf(QStringLiteral("## Photosynthesis")));
    notes = apply(QStringLiteral("=== MOVE ## Photosynthesis TO UNDER ## Cell structure"));
    QVERIFY(notes.contains(QStringLiteral("### Photosynthesis [05:00]")));
    QStringList remarks;
    notes = apply(QStringLiteral("=== MOVE ## Cell structure TO UNDER ## Nucleus"), &remarks);
    QCOMPARE(notes, Edits::apply(kNotes, {}).notes);
    QCOMPARE(remarks.size(), 1);
}

void LiveNotesEditsTest::summaryTitleTimelineActions()
{
    const QString notes = apply(QStringLiteral(
        "=== TITLE\n# Biology 101: cells\n=== SUMMARY\n## Summary\n- cells and energy\n"
        "=== TIMELINE\n- [05:00] photosynthesis begins\n=== ACTIONS\n- [ ] read ch. 5"));
    QVERIFY(notes.startsWith(QStringLiteral("# Biology 101: cells\n\n## Summary\n")));
    QVERIFY(notes.contains(QStringLiteral("- cells and energy")));
    QVERIFY(!notes.contains(QStringLiteral("- cells\n")));
    QCOMPARE(notes.count(QStringLiteral("## Summary")), 1);
    // The timeline is created above the trailing reference sections.
    QVERIFY(notes.indexOf(QStringLiteral("## Timeline")) > notes.indexOf(QStringLiteral("## Photosynthesis")));
    QVERIFY(notes.indexOf(QStringLiteral("## Timeline")) < notes.indexOf(QStringLiteral("## Definitions")));
    QVERIFY(notes.endsWith(QStringLiteral("## Action items\n\n- [ ] read ch. 5"))
                || notes.endsWith(QStringLiteral("## Action items\n- [ ] read ch. 5")));
    // Empty notes grow from edits alone.
    const QString fresh = Edits::apply(QString(), Edits::parse(QStringLiteral(
        "=== TITLE\n# Talk\n=== SUMMARY\n- a\n=== NEW\n## Topic\n- b"))).notes;
    QCOMPARE(fresh, QStringLiteral("# Talk\n\n## Summary\n\n- a\n\n## Topic\n- b"));
}

void LiveNotesEditsTest::missingTargetsKeepTheContent()
{
    QStringList remarks;
    const QString notes = apply(QStringLiteral("=== APPEND ## Genetics\n- genes\n=== DELETE ## Nope"),
                                &remarks);
    QVERIFY(notes.contains(QStringLiteral("## Genetics")));
    QVERIFY(notes.contains(QStringLiteral("- genes")));
    QVERIFY(notes.indexOf(QStringLiteral("## Genetics")) < notes.indexOf(QStringLiteral("## Definitions")));
    QCOMPARE(remarks.size(), 2);
}

void LiveNotesEditsTest::layoutsShapeThePrompt()
{
    // Every layout has a brief of its own; ids are stable (persisted).
    QStringList ids;
    QSet<QString> briefs;
    for (const auto &layout : LiveNotesLayouts::layouts()) {
        ids.append(layout.id);
        briefs.insert(LiveNotesLayouts::layoutBrief(layout.id, QStringLiteral("Recipes — food"), true));
    }
    QCOMPARE(ids, (QStringList{QStringLiteral("topics"), QStringLiteral("timeline"),
                               QStringLiteral("objectives"), QStringLiteral("study"),
                               QStringLiteral("meeting"), QStringLiteral("argument"),
                               QStringLiteral("qa"), QStringLiteral("story"), QStringLiteral("custom")}));
    QCOMPARE(briefs.size(), ids.size());
    QVERIFY(LiveNotesLayouts::layoutBrief(QStringLiteral("custom"), QStringLiteral("Recipes — food"), true)
                .contains(QStringLiteral("USER'S OWN SECTIONS")));
    QVERIFY(LiveNotesLayouts::layoutBrief(QStringLiteral("story"), QString(), true)
                .contains(QStringLiteral("## Characters")));
    NotesBriefing briefing;
    briefing.layout = QStringLiteral("study");
    briefing.style = QStringLiteral("rich");
    briefing.timeline = true;
    const QString prompt = LiveNotesPrompt::systemPrompt(2, briefing);
    for (const char *text : {"Cornell", "[!mnemonic]", "Intensity RICH", "## Timeline", "=== APPEND"}) {
        QVERIFY2(prompt.contains(QString::fromUtf8(text)), text);
    }
    const NotesBriefing back = NotesBriefing::fromMap(briefing.toMap());
    QCOMPARE(back.layout, QStringLiteral("study"));
    QCOMPARE(back.style, QStringLiteral("rich"));
    QVERIFY(back.timeline);
    QCOMPARE(NotesBriefing::fromMap({}).layout, QStringLiteral("topics"));
}

void LiveNotesEditsTest::engineFilesEditsIntoTheNotes()
{
    Provider provider;
    provider.replies = {QStringLiteral("=== APPEND ## Cell structure\n- walls in plants"),
                        QStringLiteral("=== END"), QStringLiteral("I cannot help with that.")};
    QString notes = kNotes;
    LiveNotesEngine engine;
    LiveNotesEngine::Options options;
    options.minWords = 1;
    options.minIntervalMs = 0;
    options.fullRevisionEvery = 0;
    engine.setOptions(options);
    engine.setProvider(&provider);
    engine.setNotesAccess([&notes] { return notes; }, [&notes](const QString &n) { notes = n; return true; });
    engine.start();
    engine.addSegment(QStringLiteral("plant cells also have walls"), 2400000);
    engine.tick();
    const QString message = provider.requests.first().messages.first().text;
    QVERIFY(message.contains(QStringLiteral("CURRENT NOTES:\n# Biology")));
    QVERIFY(message.contains(QStringLiteral("EDIT SCRIPT")));
    QTRY_VERIFY(notes.contains(QStringLiteral("- walls in plants")));
    QVERIFY(notes.indexOf(QStringLiteral("- walls in plants")) < notes.indexOf(QStringLiteral("### Nucleus")));
    QCOMPARE(engine.updateCount(), 1);
    // "=== END": nothing written, the lines count as covered.
    const QString before = notes;
    engine.addSegment(QStringLiteral("um so anyway"), 2410000);
    engine.tick();
    QTRY_COMPARE(provider.requests.size(), 2);
    QTRY_VERIFY(!engine.busy());
    QCOMPARE(notes, before);
    // Prose that is neither a script nor notes is refused.
    QSignalSpy failed(&engine, &LiveNotesEngine::failed);
    engine.addSegment(QStringLiteral("more"), 2420000);
    engine.tick();
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(notes, before);
}

void LiveNotesEditsTest::finalPassTidiesWithAnEditScript()
{
    Provider provider;
    provider.replies = {QStringLiteral("=== DELETE ## Photosynthesis\n=== SUMMARY\n- cells, tidied")};
    QString notes = kNotes;
    LiveNotesEngine engine;
    LiveNotesEngine::Options options;
    options.minWords = 1000; // nothing is due before the stop
    engine.setOptions(options);
    engine.setProvider(&provider);
    engine.setNotesAccess([&notes] { return notes; }, [&notes](const QString &n) { notes = n; return true; });
    QSignalSpy finalized(&engine, &LiveNotesEngine::finalized);
    engine.start();
    engine.addSegment(QStringLiteral("one last remark"), 60000);
    engine.finalize();
    QTRY_COMPARE(finalized.size(), 1);
    QCOMPARE(provider.requests.size(), 1); // one pass, not a full rewrite
    const QString message = provider.requests.first().messages.first().text;
    QVERIFY(message.contains(QStringLiteral("FINAL pass")));
    QVERIFY(message.contains(QStringLiteral("one last remark")));
    QVERIFY(!message.contains(QStringLiteral("FULL TRANSCRIPT")));
    QVERIFY(!notes.contains(QStringLiteral("Photosynthesis")));
    QVERIFY(notes.contains(QStringLiteral("- cells, tidied")));
    // "=== END" on the final pass finishes too.
    provider.replies = {QStringLiteral("=== END")};
    LiveNotesEngine second;
    second.setOptions(options);
    second.setProvider(&provider);
    second.setNotesAccess([&notes] { return notes; }, [&notes](const QString &n) { notes = n; return true; });
    QSignalSpy done(&second, &LiveNotesEngine::finalized);
    second.start();
    second.finalize();
    QTRY_COMPARE(done.size(), 1);
}

QTEST_GUILESS_MAIN(LiveNotesEditsTest)

#include "tst_live_notes_edits.moc"
