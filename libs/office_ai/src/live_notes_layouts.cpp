// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_notes_layouts.h"

#include <QObject>

namespace QindaOffice::Ai::LiveNotesLayouts {

QList<Layout> layouts()
{
    return {
        {QStringLiteral("topics"), QObject::tr("By subject"),
         QObject::tr("One section per subject; later points are filed where they belong.")},
        {QStringLiteral("timeline"), QObject::tr("Chronological"),
         QObject::tr("Sections in the order things came up.")},
        {QStringLiteral("objectives"), QObject::tr("Learning objectives"),
         QObject::tr("\"Be able to…\" goals, each with its explanation and a self-check.")},
        {QStringLiteral("study"), QObject::tr("Study and retention"),
         QObject::tr("Cornell cues, memory aids and review questions.")},
        {QStringLiteral("meeting"), QObject::tr("Meeting minutes"),
         QObject::tr("Decisions first, then topics, risks and open questions.")},
        {QStringLiteral("argument"), QObject::tr("Argument map"),
         QObject::tr("The thesis, each claim with its evidence and counterpoints.")},
        {QStringLiteral("qa"), QObject::tr("Questions and answers"),
         QObject::tr("Each section a question the session answers.")},
        {QStringLiteral("story"), QObject::tr("Story guide"),
         QObject::tr("A show, film or book: characters, plot, lore, themes, predictions.")},
        {QStringLiteral("custom"), QObject::tr("My own sections"),
         QObject::tr("The sections you list below, in your order.")},
    };
}

QList<Layout> styles()
{
    return {
        {QStringLiteral("plain"), QObject::tr("Plain"),
         QObject::tr("Headings, lists, bold and one highlight colour.")},
        {QStringLiteral("styled"), QObject::tr("Styled"),
         QObject::tr("Highlights by meaning, a few callout boxes, underline, formulas.")},
        {QStringLiteral("rich"), QObject::tr("Rich"),
         QObject::tr("Everything: callouts, tables, badges and a big-idea banner.")},
    };
}

QString layoutBrief(const QString &id, const QString &sections, bool timestamps)
{
    const QString stamp = timestamps
        ? QStringLiteral(" End each `##` heading with the time the subject first came up, in "
                         "brackets: `## Photosynthesis [12:40]`.")
        : QStringLiteral(" No timestamps in headings.");
    const QString own = sections.trimmed();
    QString brief;
    if (id == QLatin1String("timeline")) {
        brief = QStringLiteral(
                    "ORGANISATION — CHRONOLOGICAL: one `##` section per topic in the order covered, "
                    "`###` for its parts.")
            + stamp;
    } else if (id == QLatin1String("objectives")) {
        brief = QStringLiteral(
                    "ORGANISATION — LEARNING OBJECTIVES: each `##` heading is an objective phrased "
                    "as what the listener should be able to do or know (\"Explain why …\", "
                    "\"Calculate …\", \"Tell … from …\"). Under it: the explanation, the key facts, "
                    "a worked example when one is given, and a `> [!question] Check yourself` "
                    "callout with one or two self-test questions. Infer the objectives from the "
                    "content; merge, split and rename them as the session makes them clearer.")
            + stamp;
    } else if (id == QLatin1String("study")) {
        brief = QStringLiteral(
                    "ORGANISATION — FOR RETENTION (Cornell style): one `##` per topic, ordered so "
                    "prerequisites come first. In each, a table `| Cue | Notes |` whose cues are "
                    "questions or keywords the notes answer, then a one-line **Takeaway**. Add a "
                    "`> [!mnemonic]` callout wherever a list or sequence must be memorised. Keep "
                    "`## Review questions` near the end: 5–10 questions for spaced review, easiest "
                    "first.")
            + stamp;
    } else if (id == QLatin1String("meeting")) {
        brief = QStringLiteral(
            "ORGANISATION — MEETING MINUTES: `## Decisions` first (each decision, who made it, "
            "why — even when it was made at the end), then one `##` per discussion topic "
            "(positions, arguments, who said what), `## Risks and concerns`, `## Open "
            "questions`, and the action items.");
    } else if (id == QLatin1String("argument")) {
        brief = QStringLiteral(
            "ORGANISATION — ARGUMENT MAP: `## Thesis` (the central claim in one or two "
            "sentences), then one `##` per supporting claim with its evidence as sub-bullets "
            "and counterpoints marked =={warn}…==, `## Objections and responses`, and "
            "`## Assessment` (what is well supported, what is only asserted).");
    } else if (id == QLatin1String("qa")) {
        brief = QStringLiteral(
                    "ORGANISATION — QUESTIONS AND ANSWERS: each `###` heading is a question the "
                    "session answers (asked outright or implied), the answer condensed beneath "
                    "it; group related questions under `##` themes.")
            + stamp;
    } else if (id == QLatin1String("story")) {
        brief = QStringLiteral(
            "ORGANISATION — STORY GUIDE (a show, film, book, game or a story being told): "
            "`## Characters` (a `###` per character: who they are, relationships, what they "
            "want, how they change), `## Plot so far` (numbered beats with [mm:ss]), `## World "
            "and lore` (places, rules, factions, terms), `## Themes and motifs`, `## Memorable "
            "lines` (quotes, with who said them), `## Predictions and mysteries` (open threads). "
            "Update characters and lore in place as they are revealed.");
    } else if (id == QLatin1String("custom") && !own.isEmpty()) {
        return QStringLiteral(
                   "ORGANISATION — THE USER'S OWN SECTIONS, in this order (each a `##` section, "
                   "created when it first has content; anything that fits none goes in "
                   "`## Other`):\n")
            + own;
    } else {
        brief = QStringLiteral(
                    "ORGANISATION — BY SUBJECT, not by time: one `##` section per subject, `###` "
                    "for its sub-topics. When the speaker comes back to a subject, add to THAT "
                    "section — never open a second one for it. Order the sections logically "
                    "(overview first, prerequisites before what builds on them), not by when they "
                    "came up; re-file and merge as the picture becomes clear.")
            + stamp;
    }
    if (!own.isEmpty()) {
        brief += QStringLiteral("\nAlso keep these sections (the user asked for them):\n") + own;
    }
    return brief;
}

QString timelineBrief()
{
    return QStringLiteral(
        "- Keep a `## Timeline` section below the others: the chronological layer under the "
        "organised notes, one terse line per minute or two of the session, `- [mm:ss] what "
        "happened` — appended as the session goes, never reorganised.");
}

QString styleGuide(const QString &style)
{
    if (style == QLatin1String("plain")) {
        return QStringLiteral(
            "TYPESETTING — PLAIN: headings, bullets, numbered steps, **bold** key terms where "
            "introduced (definitions as `**Term** — meaning`), *italics* for titles and "
            "emphasis, and ==highlight== for the one or two takeaways per section that matter "
            "most. Nothing else.");
    }
    QString guide = QStringLiteral(
        "TYPESETTING — the page renders this markup; use it to make the notes readable and "
        "memorable, consistently (the same kind of thing always looks the same):\n"
        "- **bold** a key term where it is introduced; definitions as `**Term** — meaning`.\n"
        "- *italics* for titles, foreign words and light emphasis; ++underline++ for the one "
        "phrase in a section that must not be missed.\n"
        "- Highlights carry MEANING by colour: ==key takeaway== (yellow), =={term}a term being "
        "defined== (blue), =={example}an example== (green), =={warn}a pitfall, caveat or "
        "correction== (red), =={question}an open question== (violet), =={date}a date, deadline "
        "or figure that matters== (orange).\n"
        "- !!stand-out!! for a surprising or critical fact (bold, larger, red ink) — rare.\n"
        "- Formulas and units exactly: x^2^ (superscript), H~2~O (subscript), `code` for "
        "commands, names of files and settings.\n"
        "- Callout boxes: a quote block whose first line is `> [!kind] Optional title`, the "
        "content on the following `> ` lines. Kinds: def (definition), key (key idea), "
        "example, warn (caution), tip, question, mnemonic (memory aid), summary, quote "
        "(memorable words, with who said them), big (THE central idea in one sentence, shown "
        "large). A callout is a block of its own at the start of a line, with a blank line "
        "before it — never inside a bullet.\n"
        "- Tables (`| A | B |`) whenever two or more things are compared.\n"
        "- [#tag] badges for cross-cutting themes (e.g. [#exam], [#decision], [#todo]).\n");
    guide += style == QLatin1String("rich")
        ? QStringLiteral(
              "Intensity RICH: make the notes visually rich — open with a `> [!big]` callout "
              "stating the central idea, give most sections a callout, use a table wherever "
              "things line up, and colour-code consistently. Still notes, never decoration for "
              "its own sake.")
        : QStringLiteral(
              "Intensity STYLED: tasteful — about one callout per section at most, highlights "
              "sparingly (a few per section), tables only for real comparisons.");
    return guide;
}

} // namespace QindaOffice::Ai::LiveNotesLayouts
