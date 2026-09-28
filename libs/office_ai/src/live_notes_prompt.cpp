// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_notes_prompt.h"

#include <QObject>
#include <QRegularExpression>

namespace QindaOffice::Ai::LiveNotesPrompt {

namespace {

bool isSection(const QString &line, const char *name)
{
    return line.startsWith(QLatin1String("## "))
        && line.mid(3).trimmed().startsWith(QLatin1String(name), Qt::CaseInsensitive);
}

QString tailOf(const QList<TranscriptLine> &lines, int chars, const QHash<QString, QString> &names)
{
    QString out;
    for (qsizetype i = lines.size() - 1; i >= 0 && out.size() < chars; --i) {
        out.prepend(formatLines({lines.at(i)}, names) + QLatin1Char('\n'));
    }
    return out.trimmed();
}

} // namespace

int clampDetail(int detail)
{
    return qBound(int(KeyPoints), detail, int(Comprehensive));
}

QString detailBrief(int detail)
{
    switch (clampDetail(detail)) {
    case KeyPoints:
        return QStringLiteral(
            "DETAIL LEVEL 1 of 5 — KEY POINTS. Only what someone must remember: a 2-3 bullet "
            "summary and at most 3 bullets per topic. Fragments, not sentences (under 10 words "
            "each). No examples, no asides, no quotes unless they ARE the point. Merge topics "
            "freely. The whole set should fit on one screen.");
    case Concise:
        return QStringLiteral(
            "DETAIL LEVEL 2 of 5 — CONCISE (condensed study notes). Compress hard: every bullet "
            "a short fragment (about 12 words at most), one idea per bullet, related points "
            "merged. Keep definitions, numbers, names, conclusions and decisions; drop "
            "examples, anecdotes, repetition and hedging unless essential to understanding. "
            "Aim for roughly one line of notes per minute of speech. When in doubt, cut.");
    case Balanced:
        return QStringLiteral(
            "DETAIL LEVEL 3 of 5 — BALANCED. Clear notes of the main points with their key "
            "supporting details and the occasional illustrative example; bullets may be short "
            "sentences.");
    case Detailed:
        return QStringLiteral(
            "DETAIL LEVEL 4 of 5 — DETAILED. Capture supporting details, reasoning, examples "
            "and figures, with sub-bullets; someone who missed the session should be able to "
            "follow the argument from the notes.");
    default:
        return QStringLiteral(
            "DETAIL LEVEL 5 of 5 — COMPREHENSIVE. A near-complete structured record: every "
            "point, example, figure, caveat and question, organised and paraphrased (still "
            "notes, never a transcript).");
    }
}

QString systemPrompt(int detail, const NotesBriefing &briefing, const SessionContext &context)
{
    QStringList format;
    format << QStringLiteral("- One `#` title naming the session's subject (improve it as the "
                             "subject becomes clear).")
           << QStringLiteral("- `## Summary` right under the title: 2-5 bullets covering the "
                             "whole session so far.")
           << (briefing.timestamps
                   ? QStringLiteral("- Then one `##` section per topic, in the order covered, "
                                    "with `###` subsections when a topic has parts. End each "
                                    "`##` heading with the start time in brackets, e.g. "
                                    "`## Photosynthesis [12:40]`.")
                   : QStringLiteral("- Then one `##` section per topic, in the order covered, "
                                    "with `###` subsections when a topic has parts. No "
                                    "timestamps."))
           << QStringLiteral("- Bullets in your own words, as long as the detail level below "
                             "allows; nest sub-points. Keep specifics exact: names, numbers, "
                             "dates, formulas, units, references.")
           << QStringLiteral("- **Bold** each key term where it is introduced; definitions as "
                             "`**Term** — meaning`.")
           << QStringLiteral("- ==Highlight== the few takeaways that matter most (sparingly: "
                             "about one per section).")
           << (briefing.quotes ? QStringLiteral("- Memorable or exact wording as a quote: "
                                                "`> \"…\"`.")
                               : QStringLiteral("- No quotes: paraphrase everything."))
           << QStringLiteral("- Your own annotations in italics in parentheses: *(connects to "
                             "the earlier point on X)*, *(speaker corrects the earlier "
                             "figure)*. Mark doubtful recognition as *(unclear at mm:ss: …?)* with the "
                             "transcript time, so the user can replay it.");
    if (briefing.explainJargon) {
        format << QStringLiteral("- Right after a technical term or name the audience may not "
                                 "know, add a one-line plain-language explanation as "
                                 "[[ai: …]] (shown as an AI note, so keep it short and "
                                 "factual; only for terms that need it).");
    }
    if (briefing.definitions) {
        format << QStringLiteral("- A `## Definitions` section near the end: every key term "
                                 "with a one-line definition, alphabetical.");
    }
    if (briefing.openQuestions) {
        format << QStringLiteral("- A `## Open questions` section near the end: questions "
                                 "raised and not yet answered.");
    }
    format << (briefing.actionItems
                   ? QStringLiteral("- Tasks, assignments and deadlines as `- [ ] …` in a final "
                                    "`## Action items` section (omit the section when there are "
                                    "none).")
                   : QStringLiteral("- No `## Action items` section; mention tasks only where "
                                    "they matter to the content."));
    QString prompt = QStringLiteral(
        "You are an expert live note-taker. Audio from a live source — a lecture, a meeting, "
        "a video, a conversation — is being transcribed as it happens, and you keep the notes "
        "for it. Each turn you get the notes written so far and the newest transcript; you "
        "return the revised notes.\n\nWrite GitHub-flavoured Markdown:\n");
    prompt += format.join(QLatin1Char('\n'));
    prompt += QStringLiteral(
        "\n\nRevise, do not just append: when new material corrects, extends or clarifies "
        "earlier notes, fix those notes; merge duplicates; move points to the section they "
        "belong in. Never drop substantive content that is still correct. Lines in the notes "
        "that do not come from the transcript were written by the user: keep them and their "
        "wording.\n"
        "Transcript lines marked ★ were flagged as important; ⤷ marks where a new topic "
        "probably starts; (filler) lines are probably noise — use them only if they matter.\n"
        "‼ marks a moment the USER flagged as important while listening: it must be in the "
        "notes, ==highlighted==.\n"
        "Lines may start with a speaker label (Speaker 1, Speaker 2, …). Attribute decisions, "
        "claims, questions and quotes to whoever made them when it matters; once the "
        "transcript shows who a speaker is (\"thanks, Maria\"), write \"Maria (Speaker 2)\" "
        "the first time and the name alone after that. Never guess names.\n"
        "Ignore greetings, small talk, sponsor reads and recognition noise. Never invent "
        "anything the transcript does not say.\n\n"
        "Reply with the notes only: no preamble, no code fence, no closing remarks.\n\n");
    prompt += detailBrief(detail);
    prompt += QStringLiteral(" The detail level governs length above everything else: if the "
                             "current notes are longer or shorter than this level calls for, "
                             "rewrite them to fit it.");
    const QString session = LiveNotesBriefing::render(briefing);
    if (!session.isEmpty()) {
        prompt += QStringLiteral("\n\n") + session;
    }
    prompt += LiveNotesBriefing::renderContext(context);
    return prompt;
}

QString formatTime(qint64 ms)
{
    const qint64 seconds = qMax<qint64>(0, ms / 1000);
    if (seconds >= 3600) {
        return QStringLiteral("%1:%2:%3")
            .arg(seconds / 3600)
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString formatLines(const QList<TranscriptLine> &lines, const QHash<QString, QString> &names)
{
    QStringList out;
    for (const TranscriptLine &line : lines) {
        QString marks;
        if (line.newTopic) {
            marks += QStringLiteral("⤷ ");
        }
        if (line.flagged) {
            marks += QStringLiteral("‼ ");
        }
        if (line.key) {
            marks += QStringLiteral("★ ");
        }
        if (line.filler) {
            marks += QStringLiteral("(filler) ");
        }
        const QString name = names.value(line.speaker);
        const QString who = line.speaker.isEmpty() ? QString()
            : name.isEmpty() ? line.speaker + QStringLiteral(": ")
                             : QStringLiteral("%1 (%2): ").arg(name, line.speaker);
        out.append(QStringLiteral("[%1] %2%3%4").arg(formatTime(line.ms), marks, who, line.text));
    }
    return out.join(QLatin1Char('\n'));
}

bool useTailMode(const QString &notes)
{
    return notes.size() >= kTailModeChars && split(notes).sections.size() > kTailSections;
}

QString userMessage(const Request &request)
{
    QString out;
    out += QStringLiteral("Session time: %1").arg(formatTime(request.elapsedMs));
    if (!request.topic.trimmed().isEmpty()) {
        out += QStringLiteral(" · The user says this is about: %1").arg(request.topic.trimmed());
    }
    out += QStringLiteral("\n");
    if (!request.glossary.isEmpty()) {
        // Speech recognition mishears names ("Jeff" for "Jev"): the user's
        // key terms are the correct spellings.
        out += QStringLiteral("Correct spellings of names and terms (fix mishearings): %1\n")
                   .arg(request.glossary.join(QStringLiteral(", ")));
    }
    out += QLatin1Char('\n');
    // A restyle rewrites everything, so it always sees the whole notes.
    const bool tail = !request.fullRevision && !request.restyle && useTailMode(request.notes);
    if (tail) {
        const NotesParts parts = split(request.notes);
        const qsizetype frozen = parts.sections.size() - kTailSections;
        out += QStringLiteral("The notes are long, so this turn you revise only their END.\n\n"
                              "EARLIER SECTIONS (fixed; shown as headings only):\n");
        for (qsizetype i = 0; i < frozen; ++i) {
            out += parts.sections.at(i).section(QLatin1Char('\n'), 0, 0) + QLatin1Char('\n');
        }
        out += QStringLiteral("\nCURRENT SUMMARY:\n%1\n\nLAST SECTIONS (revise these):\n%2\n\n"
                              "CURRENT ACTION ITEMS:\n%3\n\n")
                   .arg(parts.summary.isEmpty() ? QStringLiteral("(none)") : parts.summary,
                        parts.sections.mid(frozen).join(QStringLiteral("\n\n")),
                        parts.actions.isEmpty() ? QStringLiteral("(none)") : parts.actions);
    } else {
        out += QStringLiteral("CURRENT NOTES:\n%1\n\n")
                   .arg(request.notes.trimmed().isEmpty() ? QStringLiteral("(none yet)")
                                                          : request.notes.trimmed());
    }
    if (request.restyle && request.fresh.isEmpty()) {
        out += request.restyleReason.isEmpty()
            ? QStringLiteral("The user changed the detail level. Rewrite the notes to fit it "
                             "exactly (condense or expand), keeping every fact the level "
                             "calls for and the user's own additions.")
            : request.restyleReason;
        return out;
    }
    if (request.fullRevision) {
        out += QStringLiteral("FULL TRANSCRIPT:\n%1\n\n").arg(formatLines(request.fresh, request.speakerNames));
        out += QStringLiteral("Rewrite the notes from the full transcript: reorganise, correct "
                              "and complete them, keeping the user's own additions.");
        return out;
    }
    const QString context = tailOf(request.context, kContextChars, request.speakerNames);
    if (!context.isEmpty()) {
        out += QStringLiteral("EARLIER TRANSCRIPT (already in the notes; context only):\n%1\n\n")
                   .arg(context);
    }
    out += QStringLiteral("NEW TRANSCRIPT (not yet in the notes):\n%1\n\n")
               .arg(formatLines(request.fresh, request.speakerNames));
    out += tail ? QStringLiteral("Reply with exactly: the `## Summary` section (covering the whole "
                                 "session), then the revised last sections and any new sections, "
                                 "then `## Action items` if there are any. Do not repeat the "
                                 "earlier sections or the title.")
                : QStringLiteral("Reply with the complete revised notes.");
    return out;
}

QString sanitizeReply(const QString &reply)
{
    QString text = reply;
    static const QRegularExpression think(QStringLiteral("<think>.*?</think>"),
                                          QRegularExpression::DotMatchesEverythingOption);
    text.remove(think);
    // An unterminated think block (a reasoning model cut off) is all noise.
    const qsizetype open = text.indexOf(QLatin1String("<think>"));
    if (open >= 0) {
        text.truncate(open);
    }
    text = text.trimmed();
    if (text.startsWith(QLatin1String("```"))) {
        const qsizetype firstNewline = text.indexOf(QLatin1Char('\n'));
        text = firstNewline >= 0 ? text.mid(firstNewline + 1) : QString();
        if (text.trimmed().endsWith(QLatin1String("```"))) {
            text = text.trimmed().chopped(3);
        }
        text = text.trimmed();
    }
    // "Here are the updated notes:" before the first heading.
    QStringList lines = text.split(QLatin1Char('\n'));
    qsizetype firstHeading = -1;
    for (qsizetype i = 0; i < lines.size() && i < 3; ++i) {
        if (lines.at(i).startsWith(QLatin1Char('#'))) {
            firstHeading = i;
            break;
        }
    }
    if (firstHeading > 0) {
        bool chatty = true;
        for (qsizetype i = 0; i < firstHeading; ++i) {
            const QString l = lines.at(i).trimmed();
            if (l.startsWith(QLatin1Char('-')) || l.startsWith(QLatin1Char('>'))
                || l.startsWith(QLatin1Char('*'))) {
                chatty = false;
            }
        }
        if (chatty) {
            lines = lines.mid(firstHeading);
        }
    }
    return lines.join(QLatin1Char('\n')).trimmed();
}

NotesParts split(const QString &notes)
{
    NotesParts parts;
    enum { Preamble, Summary, Section, Actions } where = Preamble;
    QString current;
    const auto close = [&] {
        const QString body = current.trimmed();
        current.clear();
        if (body.isEmpty()) {
            return;
        }
        switch (where) {
        case Preamble: parts.preamble = body; break;
        case Summary: parts.summary = body; break;
        case Section: parts.sections.append(body); break;
        case Actions:
            parts.actions = parts.actions.isEmpty() ? body : parts.actions + QLatin1Char('\n')
                    + body.section(QLatin1Char('\n'), 1);
            break;
        }
    };
    for (const QString &line : notes.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("## "))) {
            close();
            where = isSection(line, "Summary") ? Summary
                : isSection(line, "Action items") ? Actions : Section;
        }
        current += line + QLatin1Char('\n');
    }
    close();
    return parts;
}

QString join(const NotesParts &parts)
{
    QStringList pieces;
    for (const QString &piece : QStringList{parts.preamble, parts.summary}) {
        if (!piece.isEmpty()) {
            pieces.append(piece);
        }
    }
    pieces.append(parts.sections);
    if (!parts.actions.isEmpty()) {
        pieces.append(parts.actions);
    }
    return pieces.join(QStringLiteral("\n\n"));
}

QString mergeTail(const QString &notes, const QString &reply)
{
    const NotesParts old = split(notes);
    const NotesParts fresh = split(reply);
    if (fresh.sections.isEmpty() && fresh.summary.isEmpty()) {
        return {};
    }
    NotesParts merged;
    merged.preamble = old.preamble;
    merged.summary = fresh.summary.isEmpty() ? old.summary : fresh.summary;
    const qsizetype frozen = qMax<qsizetype>(0, old.sections.size() - kTailSections);
    merged.sections = old.sections.mid(0, frozen) + fresh.sections;
    // A reply that forgot the action items must not delete them.
    merged.actions = fresh.actions.isEmpty() ? old.actions : fresh.actions;
    return join(merged);
}

bool plausibleRevision(const QString &before, const QString &after, QString *why)
{
    if (after.trimmed().isEmpty()) {
        *why = QObject::tr("the model returned no notes");
        return false;
    }
    if (before.size() > 600 && after.size() < before.size() / 2) {
        *why = QObject::tr("the model's revision dropped more than half of the notes");
        return false;
    }
    return true;
}

} // namespace QindaOffice::Ai::LiveNotesPrompt
