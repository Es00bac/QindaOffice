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

QString tailOf(const QList<TranscriptLine> &lines, int chars)
{
    QString out;
    for (qsizetype i = lines.size() - 1; i >= 0 && out.size() < chars; --i) {
        out.prepend(formatLines({lines.at(i)}) + QLatin1Char('\n'));
    }
    return out.trimmed();
}

} // namespace

QString systemPrompt()
{
    return QStringLiteral(
        "You are an expert live note-taker. Audio from a live source — a lecture, a meeting, "
        "a video, a conversation — is being transcribed as it happens, and you keep the notes "
        "for it. Each turn you get the notes written so far and the newest transcript; you "
        "return the revised notes.\n\n"
        "Write GitHub-flavoured Markdown:\n"
        "- One `#` title naming the session's subject (improve it as the subject becomes clear).\n"
        "- `## Summary` right under the title: 2-5 bullets covering the whole session so far.\n"
        "- Then one `##` section per topic, in the order covered, with `###` subsections when a "
        "topic has parts. End each `##` heading with the start time in brackets, e.g. "
        "`## Photosynthesis [12:40]`.\n"
        "- Concise bullets in your own words; nest sub-points. Keep specifics exact: names, "
        "numbers, dates, formulas, units, examples, references.\n"
        "- **Bold** each key term where it is introduced; definitions as `**Term** — meaning`.\n"
        "- ==Highlight== the few takeaways that matter most (sparingly: about one per section).\n"
        "- Memorable or exact wording as a quote: `> \"…\"`.\n"
        "- Your own annotations in italics in parentheses: *(connects to the earlier point on "
        "X)*, *(speaker corrects the earlier figure)*. Mark doubtful recognition as "
        "*(unclear: …?)*.\n"
        "- Tasks, assignments and deadlines as `- [ ] …` in a final `## Action items` section "
        "(omit the section when there are none).\n\n"
        "Revise, do not just append: when new material corrects, extends or clarifies earlier "
        "notes, fix those notes; merge duplicates; move points to the section they belong in. "
        "Never drop substantive content that is still correct. Lines in the notes that do not "
        "come from the transcript were written by the user: keep them and their wording.\n"
        "Transcript lines marked ★ were flagged as important; ⤷ marks where a new topic "
        "probably starts; (filler) lines are probably noise — use them only if they matter.\n"
        "Ignore greetings, small talk, sponsor reads and recognition noise. Never invent "
        "anything the transcript does not say.\n\n"
        "Reply with the notes only: no preamble, no code fence, no closing remarks.");
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

QString formatLines(const QList<TranscriptLine> &lines)
{
    QStringList out;
    for (const TranscriptLine &line : lines) {
        QString marks;
        if (line.newTopic) {
            marks += QStringLiteral("⤷ ");
        }
        if (line.key) {
            marks += QStringLiteral("★ ");
        }
        if (line.filler) {
            marks += QStringLiteral("(filler) ");
        }
        out.append(QStringLiteral("[%1] %2%3").arg(formatTime(line.ms), marks, line.text));
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
    const bool tail = !request.fullRevision && useTailMode(request.notes);
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
    if (request.fullRevision) {
        out += QStringLiteral("FULL TRANSCRIPT:\n%1\n\n").arg(formatLines(request.fresh));
        out += QStringLiteral("Rewrite the notes from the full transcript: reorganise, correct "
                              "and complete them, keeping the user's own additions.");
        return out;
    }
    const QString context = tailOf(request.context, kContextChars);
    if (!context.isEmpty()) {
        out += QStringLiteral("EARLIER TRANSCRIPT (already in the notes; context only):\n%1\n\n")
                   .arg(context);
    }
    out += QStringLiteral("NEW TRANSCRIPT (not yet in the notes):\n%1\n\n")
               .arg(formatLines(request.fresh));
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
