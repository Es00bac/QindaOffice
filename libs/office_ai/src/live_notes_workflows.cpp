// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_notes_briefing.h"
#include <QObject>
namespace QindaOffice::Ai::LiveNotesBriefing {
QList<Scenario> workflowScenarios()
{
    return {
        {QStringLiteral("technical"), QObject::tr("Technical discussion or design review"),
            QObject::tr("Constraints, alternatives, tradeoffs, decisions and unanswered questions.")},
        {QStringLiteral("planning"), QObject::tr("Project planning or status"),
            QObject::tr("Goals, scope, milestones, dependencies, owners and blockers.")},
        {QStringLiteral("troubleshooting"), QObject::tr("Troubleshooting or incident"),
            QObject::tr("Symptoms, environment, attempted fixes, observed results and verification.")},
        {QStringLiteral("retrospective"), QObject::tr("Retrospective"),
            QObject::tr("What happened, what helped, problems and agreed improvements.")},
        {QStringLiteral("one_on_one"), QObject::tr("One-to-one or coaching"),
            QObject::tr("Goals, feedback, obstacles, commitments and follow-up questions.")},
        {QStringLiteral("appointment"), QObject::tr("Personal appointment"),
            QObject::tr("What was discussed, explicit instructions, questions and next steps.")},
        {QStringLiteral("workshop"), QObject::tr("Workshop or conference"),
            QObject::tr("Ideas, demonstrations, resources and applications to try.")},
        {QStringLiteral("community"), QObject::tr("Community or volunteer planning"),
            QObject::tr("Proposals, decisions, responsibilities, logistics and unresolved needs.")}
    };
}
QString workflowBrief(const QString &id)
{
    if (id == QLatin1String("technical")) return QStringLiteral(
        "TECHNICAL DISCUSSION / DESIGN REVIEW: capture the problem, goals and constraints; "
        "compare alternatives with their evidence and tradeoffs. Separate proposed designs "
        "from agreed decisions. Keep compatibility assumptions, versions and exact names. "
        "Record rationale and rejected alternatives only when discussed. Mark missing "
        "evidence or unresolved questions explicitly.");
    if (id == QLatin1String("planning")) return QStringLiteral(
        "PROJECT PLANNING / STATUS: record goals, scope and exclusions, milestones, dependencies, "
        "risks, blockers and changed estimates. Distinguish committed work from suggestions. "
        "An explicit promise such as 'I will finish X' is a commitment even without a deadline. "
        "Name owners and dates only when stated; otherwise say unassigned / no date agreed. "
        "Keep completed work separate from next steps and do not treat a status report as a new task.");
    if (id == QLatin1String("troubleshooting")) return QStringLiteral(
        "TROUBLESHOOTING / INCIDENT: preserve symptoms, environment and versions, reproduction, "
        "expected versus observed behavior. For each attempted fix record what was tried and "
        "the observed result; distinguish hypotheses, failures, workarounds and verified fixes. "
        "Keep a chronological investigation trail even when the notes are organised by topic. "
        "Never invent commands or label the cause confirmed without evidence. Record verification "
        "and rollback plans when mentioned.");
    if (id == QLatin1String("retrospective")) return QStringLiteral(
        "RETROSPECTIVE: distinguish observations from interpretations; capture what worked, "
        "what did not, contributing factors and agreed experiments. Use neutral language, "
        "without assigning blame or motives. Keep owners and follow-up measures only when agreed.");
    if (id == QLatin1String("one_on_one")) return QStringLiteral(
        "ONE-TO-ONE / COACHING: retain the person's goals, progress, feedback, obstacles and "
        "explicit commitments. Separate advice or options from a decision to act. Do not infer "
        "feelings, motives, performance ratings or unstated commitments. Keep questions to revisit.");
    if (id == QLatin1String("appointment")) return QStringLiteral(
        "PERSONAL APPOINTMENT: record the person's questions, what the other person actually "
        "said, explicit instructions and agreed next steps. Keep exact amounts, dates and names "
        "when stated; flag unclear wording. Do not add diagnoses, legal interpretations or "
        "new instructions, and do not infer sensitive details. These are notes from a conversation.");
    if (id == QLatin1String("workshop")) return QStringLiteral(
        "WORKSHOP / CONFERENCE: keep key ideas, demonstrations, prerequisites, named resources "
        "and concrete uses. Distinguish demonstrated results from a speaker's claims. Retain "
        "questions from the audience and unresolved limitations; never reconstruct missing links.");
    if (id == QLatin1String("community")) return QStringLiteral(
        "COMMUNITY / VOLUNTEER PLANNING: distinguish proposals, voted/agreed decisions and "
        "remaining disagreements. Record responsibilities, logistics, dates, resource needs and "
        "accessibility needs when stated. Never assume consent or assign volunteers to tasks.");
    return {};
}
NotesBriefing suggestedBriefing(const QString &id)
{
    NotesBriefing b;
    b.scenario = id;
    b.openQuestions = true;
    b.style = QStringLiteral("plain");
    if (id == QLatin1String("technical")) {
        b.layout = QStringLiteral("custom");
        b.sections = QStringLiteral("Goals and constraints\nOptions and tradeoffs\nDecisions and rationale\nEvidence to verify");
    } else if (id == QLatin1String("planning") || id == QLatin1String("community")) {
        b.layout = QStringLiteral("custom");
        b.sections = QStringLiteral("Goals and scope\nProgress\nMilestones and dependencies\nRisks and blockers\nDecisions");
    } else if (id == QLatin1String("troubleshooting")) {
        b.layout = QStringLiteral("custom");
        b.sections = QStringLiteral("Symptoms and environment\nReproduction\nHypotheses\nAttempts and results\nVerified fixes\nVerification and rollback");
        b.timeline = true;
    } else if (id == QLatin1String("retrospective")) {
        b.layout = QStringLiteral("custom");
        b.sections = QStringLiteral("What happened\nWhat worked\nWhat did not\nContributing factors\nAgreed experiments");
    } else if (id == QLatin1String("one_on_one") || id == QLatin1String("appointment")) {
        b.layout = QStringLiteral("custom");
        b.sections = QStringLiteral("Questions and goals\nWhat was discussed\nInstructions and commitments\nQuestions for next time");
        b.quotes = false;
    } else if (id == QLatin1String("lecture") || id == QLatin1String("workshop")) {
        b.layout = QStringLiteral("study");
        b.definitions = true;
    } else if (id == QLatin1String("meeting")) b.layout = QStringLiteral("meeting");
    else if (id == QLatin1String("interview")) b.layout = QStringLiteral("qa");
    else if (id == QLatin1String("tutorial")) b.layout = QStringLiteral("timeline");
    return b;
}
}
