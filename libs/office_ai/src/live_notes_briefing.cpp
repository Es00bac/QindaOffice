// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_notes_briefing.h"

#include <QObject>

namespace QindaOffice::Ai {

QVariantMap NotesBriefing::toMap() const
{
    return {{QStringLiteral("scenario"), scenario},
            {QStringLiteral("focus"), focus},
            {QStringLiteral("skip"), skip},
            {QStringLiteral("custom"), custom},
            {QStringLiteral("timestamps"), timestamps},
            {QStringLiteral("actionItems"), actionItems},
            {QStringLiteral("quotes"), quotes},
            {QStringLiteral("openQuestions"), openQuestions},
            {QStringLiteral("definitions"), definitions},
            {QStringLiteral("explainJargon"), explainJargon},
            {QStringLiteral("layout"), layout},
            {QStringLiteral("sections"), sections},
            {QStringLiteral("style"), style},
            {QStringLiteral("timeline"), timeline}};
}

NotesBriefing NotesBriefing::fromMap(const QVariantMap &map)
{
    NotesBriefing b;
    const auto text = [&map](const char *key, const QString &fallback) {
        return map.value(QLatin1String(key), fallback).toString().trimmed();
    };
    const auto flag = [&map](const char *key, bool fallback) {
        return map.value(QLatin1String(key), fallback).toBool();
    };
    b.scenario = text("scenario", b.scenario);
    b.focus = text("focus", QString());
    b.skip = text("skip", QString());
    b.custom = text("custom", QString());
    b.timestamps = flag("timestamps", b.timestamps);
    b.actionItems = flag("actionItems", b.actionItems);
    b.quotes = flag("quotes", b.quotes);
    b.openQuestions = flag("openQuestions", b.openQuestions);
    b.definitions = flag("definitions", b.definitions);
    b.explainJargon = flag("explainJargon", b.explainJargon);
    b.layout = text("layout", b.layout);
    b.sections = map.value(QStringLiteral("sections")).toString().trimmed();
    b.style = text("style", b.style);
    b.timeline = flag("timeline", b.timeline);
    return b;
}

namespace LiveNotesBriefing {

QList<Scenario> scenarios()
{
    QList<Scenario> out{
        {QStringLiteral("general"), QObject::tr("General"),
         QObject::tr("Anything: organised by topic, nothing assumed.")},
        {QStringLiteral("lecture"), QObject::tr("Lecture or class"),
         QObject::tr("Definitions, formulas, examples, and what the teacher says will be tested.")},
        {QStringLiteral("meeting"), QObject::tr("Meeting"),
         QObject::tr("Decisions, who owns what, deadlines and open issues.")},
        {QStringLiteral("interview"), QObject::tr("Interview"),
         QObject::tr("Question-and-answer pairs, the speaker's own words, facts about them.")},
        {QStringLiteral("tutorial"), QObject::tr("Tutorial or how-to"),
         QObject::tr("Numbered steps, exact commands, settings, values and gotchas.")},
        {QStringLiteral("talk"), QObject::tr("Talk, podcast or video"),
         QObject::tr("The thesis, the arguments and evidence, references and takeaways.")},
    };
    const auto workflows = workflowScenarios();
    for (qsizetype i = 0; i < workflows.size(); ++i) out.insert(i + 1, workflows.at(i));
    return out;
}

QString scenarioBrief(const QString &id)
{
    if (id == QLatin1String("lecture")) {
        return QStringLiteral(
            "This is a LECTURE or class. Use the lecture's own terms and structure where the "
            "organisation allows. Capture every "
            "definition (as **Term** — meaning), formula, theorem, date and worked example "
            "exactly. When the lecturer signals importance (\"this will be on the exam\", "
            "\"remember\", repetition), ==highlight== it. Note assignments and readings as action "
            "items.");
    }
    if (id == QLatin1String("meeting")) {
        return QStringLiteral(
            "This is a MEETING. Lead with the decisions made. For each topic record what was "
            "decided, the reasons, and disagreements that remain. Every task becomes an action "
            "item with its owner and due date when said (- [ ] **Name** — task — by date). "
            "Attribute proposals and objections to people by name when names are used.");
    }
    if (id == QLatin1String("interview")) {
        return QStringLiteral(
            "This is an INTERVIEW. Structure the notes as the questions asked (### Q: …) with "
            "the answers condensed beneath. Keep the interviewee's most telling phrases as "
            "exact quotes. Collect facts about the person (background, roles, dates) in a "
            "`## About` section after the summary.");
    }
    if (id == QLatin1String("tutorial")) {
        return QStringLiteral(
            "This is a TUTORIAL or how-to. Turn the procedure into numbered steps in order. Put "
            "commands, code, file names, menu paths, settings and values in `code`, exactly as "
            "said. Note prerequisites first and warnings/gotchas where they apply.");
    }
    if (id == QLatin1String("talk")) {
        return QStringLiteral(
            "This is a TALK, podcast or video. Capture the central thesis in the summary, then "
            "each argument with its evidence. List books, papers, people, products and links "
            "mentioned in a `## References` section. Separate the speakers' claims from "
            "established facts when the difference matters.");
    }
    return workflowBrief(id);
}

QString render(const NotesBriefing &briefing)
{
    QStringList parts;
    const QString scenario = scenarioBrief(briefing.scenario);
    if (!scenario.isEmpty()) {
        parts.append(scenario);
    }
    if (!briefing.focus.isEmpty()) {
        parts.append(QStringLiteral("FOCUS ON: %1. Give these the most space and detail; "
                                    "anything else only as far as it supports them.")
                         .arg(briefing.focus));
    }
    if (!briefing.skip.isEmpty()) {
        parts.append(QStringLiteral("LEAVE OUT: %1. Do not note these at all, even if they "
                                    "take up much of the session.")
                         .arg(briefing.skip));
    }
    if (!briefing.custom.isEmpty()) {
        parts.append(QStringLiteral("THE USER'S OWN INSTRUCTIONS: %1").arg(briefing.custom));
    }
    if (parts.isEmpty()) {
        return {};
    }
    return QStringLiteral("SESSION BRIEFING (from the user; where it conflicts with the rules "
                          "above, it wins):\n")
        + parts.join(QStringLiteral("\n"));
}

QString renderContext(const SessionContext &context)
{
    QString out;
    if (!context.reference.trimmed().isEmpty()) {
        out += QStringLiteral(
                   "\n\nREFERENCE MATERIAL (the user attached it before the session — slides, "
                   "a syllabus, an agenda. Follow its structure and use its spellings; note what "
                   "it says only when the session covers it):\n<<<\n%1\n>>>")
                   .arg(context.reference.left(SessionContext::kReferenceBudget));
    }
    if (!context.previousNotes.trimmed().isEmpty()) {
        out += QStringLiteral(
                   "\n\nEARLIER SESSIONS in this series (their notes, for continuity: refer back "
                   "— \"(see session 3)\" — instead of repeating them):\n<<<\n%1\n>>>")
                   .arg(context.previousNotes.right(SessionContext::kPreviousBudget));
    }
    return out;
}

} // namespace LiveNotesBriefing

} // namespace QindaOffice::Ai
