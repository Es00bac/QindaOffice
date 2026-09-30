// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_notes_edits.h"

#include <QRegularExpression>

namespace QindaOffice::Ai::LiveNotesEdits {

namespace {

// Top-level sections that stay at the end, below the topic sections, in
// this order. NEW (at end) and a missing TIMELINE are inserted above them.
const QStringList kTrailing = {QStringLiteral("timeline"), QStringLiteral("definitions"),
                               QStringLiteral("key terms"), QStringLiteral("open questions"),
                               QStringLiteral("review questions"), QStringLiteral("references"),
                               QStringLiteral("action items")};

struct Heading {
    int line = 0;
    int level = 0;
    QString norm;
};

int headingLevel(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("^(#{1,6})\\s+\\S"));
    const QRegularExpressionMatch m = re.match(line);
    return m.hasMatch() ? int(m.capturedLength(1)) : 0;
}

class Doc {
public:
    explicit Doc(const QString &text) : lines(text.split(QLatin1Char('\n'))) { index(); }

    QStringList lines;
    QList<Heading> headings;

    void index()
    {
        headings.clear();
        bool fence = false;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).trimmed().startsWith(QLatin1String("```"))) {
                fence = !fence;
                continue;
            }
            const int level = fence ? 0 : headingLevel(lines.at(i));
            if (level > 0) {
                headings.append({i, level, normalizeHeading(lines.at(i))});
            }
        }
    }
    // Index into `headings`, or -1.
    int find(const QString &target, int minLevel = 2) const
    {
        const QString want = normalizeHeading(target);
        if (want.isEmpty()) {
            return -1;
        }
        for (int i = 0; i < headings.size(); ++i) {
            if (headings.at(i).level >= minLevel && headings.at(i).norm == want) {
                return i;
            }
        }
        int match = -1;
        for (int i = 0; i < headings.size(); ++i) {
            const QString &have = headings.at(i).norm;
            if (headings.at(i).level >= minLevel && qMin(have.size(), want.size()) >= 4
                && (have.startsWith(want) || want.startsWith(have))) {
                // A shortened heading must not select the first of two
                // different topics. Missing/ambiguous targets keep content.
                if (match >= 0) return -1;
                match = i;
            }
        }
        return match;
    }
    int sectionEnd(int h) const
    {
        for (int i = h + 1; i < headings.size(); ++i) {
            if (headings.at(i).level <= headings.at(h).level) {
                return headings.at(i).line;
            }
        }
        return int(lines.size());
    }
    int ownEnd(int h) const
    {
        return h + 1 < headings.size() ? qMin(headings.at(h + 1).line, sectionEnd(h))
                                       : int(lines.size());
    }
    // Where a new top-level topic section goes: above the trailing ones.
    int topicsEnd() const
    {
        int at = int(lines.size());
        for (int i = int(headings.size()) - 1; i >= 0; --i) {
            if (headings.at(i).level == 1) {
                break;
            }
            if (headings.at(i).level == 2) {
                if (!kTrailing.contains(headings.at(i).norm)) {
                    break;
                }
                at = headings.at(i).line;
            }
        }
        return at;
    }
    void insert(int at, const QStringList &block)
    {
        // Trim blank lines at the seam so spacing is decided once, later.
        while (at > 0 && at <= lines.size() && lines.at(at - 1).trimmed().isEmpty()) {
            lines.removeAt(--at);
        }
        QStringList padded = block;
        padded.prepend(QString());
        padded.append(QString());
        for (int i = 0; i < padded.size(); ++i) {
            lines.insert(at + i, padded.at(i));
        }
        index();
    }
    QStringList take(int from, int to)
    {
        const QStringList out = lines.mid(from, to - from);
        lines.remove(from, to - from);
        index();
        return out;
    }
};

QStringList bodyLines(const QString &body)
{
    QStringList out = body.split(QLatin1Char('\n'));
    while (!out.isEmpty() && out.first().trimmed().isEmpty()) {
        out.removeFirst();
    }
    while (!out.isEmpty() && out.last().trimmed().isEmpty()) {
        out.removeLast();
    }
    return out;
}

int firstHeadingLevel(const QStringList &block)
{
    for (const QString &line : block) {
        if (const int level = headingLevel(line); level > 0) {
            return level;
        }
        if (!line.trimmed().isEmpty()) {
            return 0; // text before any heading: not a section
        }
    }
    return 0;
}

// Re-levels every heading in `block` so its first heading is `level`.
QStringList relevel(const QStringList &block, int level)
{
    const int first = firstHeadingLevel(block);
    if (first == 0 || first == level) {
        return block;
    }
    QStringList out;
    for (const QString &line : block) {
        const int l = headingLevel(line);
        if (l == 0) {
            out.append(line);
            continue;
        }
        const int to = qBound(2, l + level - first, 6);
        out.append(QString(to, QLatin1Char('#')) + line.mid(l));
    }
    return out;
}

// Drops a heading line the model repeated at the top of a body that is
// meant to be the section's content only (SUMMARY, ACTIONS, TIMELINE).
QStringList withoutHeading(QStringList block)
{
    if (!block.isEmpty() && headingLevel(block.first()) > 0) {
        block.removeFirst();
    }
    return bodyLines(block.join(QLatin1Char('\n')));
}

void replaceBody(Doc &doc, int h, const QStringList &body)
{
    const int from = doc.headings.at(h).line + 1;
    const int to = doc.ownEnd(h);
    doc.lines.remove(from, to - from);
    doc.index();
    doc.insert(from, body);
}

// A section named `name` at level 2, created above the trailing sections
// (or at the very end for Action items) when missing.
int ensureSection(Doc &doc, const QString &name, bool atEnd)
{
    int h = doc.find(name);
    if (h < 0) {
        doc.insert(atEnd ? int(doc.lines.size()) : doc.topicsEnd(),
                   {QStringLiteral("## ") + name});
        h = doc.find(name);
    }
    return h;
}

QString tidy(const QStringList &lines)
{
    QStringList out;
    for (const QString &line : lines) {
        const bool blank = line.trimmed().isEmpty();
        if (blank && (out.isEmpty() || out.last().trimmed().isEmpty())) {
            continue; // no leading or doubled blank lines
        }
        if (headingLevel(line) > 0 && !out.isEmpty() && !out.last().trimmed().isEmpty()) {
            out.append(QString()); // a heading always starts a paragraph
        }
        out.append(line);
    }
    while (!out.isEmpty() && out.last().trimmed().isEmpty()) {
        out.removeLast();
    }
    return out.join(QLatin1Char('\n'));
}

} // namespace

QString normalizeHeading(const QString &heading)
{
    QString t = heading.trimmed();
    while (t.startsWith(QLatin1Char('#'))) {
        t.remove(0, 1);
    }
    static const QRegularExpression stamp(QStringLiteral("\\s*\\[(?:\\d{1,2}:)?\\d{1,2}:\\d{2}\\]\\s*$"));
    t.remove(stamp);
    t.remove(QStringLiteral("**"));
    t.remove(QStringLiteral("=="));
    return t.trimmed().toCaseFolded();
}

QList<Op> parse(const QString &reply, bool *script)
{
    static const QRegularExpression opLine(
        QStringLiteral("^===\\s*(TITLE|SUMMARY|REPLACE|APPEND|NEW|MOVE|DELETE|TIMELINE|ACTIONS|END)"
                       "\\b(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression moveSpec(QStringLiteral("^(.*?)\\s+TO\\s+(UNDER|AFTER)\\s+(.*)$"),
                                             QRegularExpression::CaseInsensitiveOption);
    QList<Op> ops;
    QStringList body;
    bool open = false;
    bool sawEnd = false;
    const auto close = [&] {
        if (open) {
            ops.last().body = bodyLines(body.join(QLatin1Char('\n'))).join(QLatin1Char('\n'));
        }
        body.clear();
        open = false;
    };
    for (const QString &line : reply.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = opLine.match(line.trimmed());
        if (!m.hasMatch()) {
            if (open) {
                body.append(line);
            }
            continue;
        }
        close();
        const QString word = m.captured(1).toUpper();
        const QString rest = m.captured(2).trimmed();
        if (word == QLatin1String("END")) {
            sawEnd = true;
            continue;
        }
        Op op;
        if (word == QLatin1String("TITLE")) {
            op.kind = Op::Title;
        } else if (word == QLatin1String("SUMMARY")) {
            op.kind = Op::Summary;
        } else if (word == QLatin1String("REPLACE")) {
            op.kind = Op::Replace;
        } else if (word == QLatin1String("APPEND")) {
            op.kind = Op::Append;
        } else if (word == QLatin1String("DELETE")) {
            op.kind = Op::Delete;
        } else if (word == QLatin1String("TIMELINE")) {
            op.kind = Op::Timeline;
        } else if (word == QLatin1String("ACTIONS")) {
            op.kind = Op::Actions;
        } else if (word == QLatin1String("MOVE")) {
            const QRegularExpressionMatch mv = moveSpec.match(rest);
            if (!mv.hasMatch()) {
                continue;
            }
            op.kind = Op::Move;
            op.target = mv.captured(1).trimmed();
            op.moveUnder = mv.captured(2).compare(QLatin1String("UNDER"), Qt::CaseInsensitive) == 0;
            op.destination = mv.captured(3).trimmed();
        } else { // NEW [UNDER|AFTER ## X] [AT END]
            const QString upper = rest.toUpper();
            if (upper.startsWith(QLatin1String("UNDER"))) {
                op.kind = Op::NewUnder;
                op.target = rest.mid(5).trimmed();
            } else if (upper.startsWith(QLatin1String("AFTER"))) {
                op.kind = Op::NewAfter;
                op.target = rest.mid(5).trimmed();
            } else {
                op.kind = Op::NewAtEnd;
            }
        }
        if (op.kind == Op::Replace || op.kind == Op::Append || op.kind == Op::Delete) {
            op.target = rest;
        }
        ops.append(op);
        open = true;
    }
    close();
    if (script != nullptr) {
        *script = sawEnd || !ops.isEmpty();
    }
    return ops;
}

Result apply(const QString &notes, const QList<Op> &ops)
{
    Doc doc(notes);
    Result result;
    const auto remark = [&result](const QString &text) { result.remarks.append(text); };
    for (const Op &op : ops) {
        const QStringList body = bodyLines(op.body);
        switch (op.kind) {
        case Op::Title: {
            const QString title = body.isEmpty() ? QString() : body.first().trimmed();
            if (!title.startsWith(QLatin1String("# "))) {
                remark(QStringLiteral("TITLE without a # line"));
                continue;
            }
            const int h = doc.headings.isEmpty() || doc.headings.first().level != 1 ? -1 : 0;
            if (h == 0) {
                doc.lines[doc.headings.first().line] = title;
                doc.index();
            } else {
                doc.insert(0, {title});
            }
            break;
        }
        case Op::Summary: {
            int h = doc.find(QStringLiteral("Summary"));
            if (h < 0) {
                // Right under the title (before the first section).
                int at = 0;
                for (const Heading &heading : std::as_const(doc.headings)) {
                    if (heading.level >= 2) {
                        at = heading.line;
                        break;
                    }
                    at = int(doc.lines.size());
                }
                doc.insert(at, {QStringLiteral("## Summary")});
                h = doc.find(QStringLiteral("Summary"));
            }
            replaceBody(doc, h, withoutHeading(body));
            break;
        }
        case Op::Replace:
        case Op::Append: {
            const int h = doc.find(op.target);
            if (h < 0) {
                remark(QStringLiteral("%1 target missing, added as a new section: %2")
                           .arg(op.kind == Op::Replace ? QStringLiteral("REPLACE")
                                                       : QStringLiteral("APPEND"),
                                op.target));
                QStringList block = body;
                if (firstHeadingLevel(block) == 0) {
                    QString name = op.target.trimmed();
                    while (name.startsWith(QLatin1Char('#'))) {
                        name.remove(0, 1);
                    }
                    block.prepend(QStringLiteral("## ") + name.trimmed());
                }
                doc.insert(doc.topicsEnd(), relevel(block, 2));
            } else if (op.kind == Op::Append) {
                doc.insert(doc.ownEnd(h), withoutHeading(body));
            } else if (firstHeadingLevel(body) == 0) {
                replaceBody(doc, h, body); // keeps the old heading
            } else {
                const int level = doc.headings.at(h).level;
                const int from = doc.headings.at(h).line;
                doc.take(from, doc.sectionEnd(h));
                doc.insert(from, relevel(body, level));
            }
            break;
        }
        case Op::NewUnder:
        case Op::NewAfter:
        case Op::NewAtEnd: {
            if (firstHeadingLevel(body) == 0) {
                const int h = op.kind == Op::NewAtEnd ? -1 : doc.find(op.target);
                if (h >= 0) {
                    doc.insert(doc.ownEnd(h), body); // text, not a section: append it
                } else {
                    remark(QStringLiteral("NEW without a heading was dropped"));
                    continue;
                }
                break;
            }
            const int h = op.kind == Op::NewAtEnd ? -1 : doc.find(op.target);
            if (h < 0) {
                if (op.kind != Op::NewAtEnd) {
                    remark(QStringLiteral("NEW target missing, added at the end: %1").arg(op.target));
                }
                doc.insert(doc.topicsEnd(), relevel(body, 2));
                break;
            }
            const int level = doc.headings.at(h).level + (op.kind == Op::NewUnder ? 1 : 0);
            doc.insert(doc.sectionEnd(h), relevel(body, qMax(2, level)));
            break;
        }
        case Op::Move: {
            const int from = doc.find(op.target);
            if (from < 0 || doc.find(op.destination) < 0) {
                remark(QStringLiteral("MOVE target missing: %1").arg(op.target));
                continue;
            }
            const int start = doc.headings.at(from).line;
            const int end = doc.sectionEnd(from);
            const int destLine = doc.headings.at(doc.find(op.destination)).line;
            if (destLine >= start && destLine < end) {
                remark(QStringLiteral("MOVE into itself ignored: %1").arg(op.target));
                continue;
            }
            const QStringList block = doc.take(start, end);
            const int dest = doc.find(op.destination);
            const int level = doc.headings.at(dest).level + (op.moveUnder ? 1 : 0);
            doc.insert(doc.sectionEnd(dest), relevel(block, qMax(2, level)));
            break;
        }
        case Op::Delete: {
            const int h = doc.find(op.target);
            if (h < 0) {
                remark(QStringLiteral("DELETE target missing: %1").arg(op.target));
                continue;
            }
            doc.take(doc.headings.at(h).line, doc.sectionEnd(h));
            break;
        }
        case Op::Timeline: {
            const int h = ensureSection(doc, QStringLiteral("Timeline"), false);
            doc.insert(doc.ownEnd(h), withoutHeading(body));
            break;
        }
        case Op::Actions: {
            const QStringList items = withoutHeading(body);
            int h = doc.find(QStringLiteral("Action items"));
            if (h < 0 && items.isEmpty()) {
                break;
            }
            h = ensureSection(doc, QStringLiteral("Action items"), true);
            replaceBody(doc, h, items);
            break;
        }
        }
        ++result.applied;
    }
    result.notes = tidy(doc.lines);
    return result;
}

QString protocolText()
{
    return QStringLiteral(
        "Reply with an EDIT SCRIPT, not the whole notes. Each edit starts with a line "
        "beginning `=== `, followed by its content on the next lines:\n"
        "=== TITLE — a better `# Title` line\n"
        "=== SUMMARY — the new summary bullets (the whole summary)\n"
        "=== APPEND ## Heading — bullets to ADD to that existing section (most common)\n"
        "=== REPLACE ## Heading — the whole revised section, heading included (to correct, "
        "merge or reorganise it)\n"
        "=== NEW UNDER ## Heading — a new `###` subsection inside that section\n"
        "=== NEW AFTER ## Heading — a new `##` section after that one\n"
        "=== NEW — a new `##` section after the other topic sections\n"
        "=== MOVE ## A TO UNDER ## B — re-file a section (or TO AFTER ## B)\n"
        "=== DELETE ## Heading — remove a section that is wrong or merged elsewhere\n"
        "=== TIMELINE — lines to add to the chronological `## Timeline`\n"
        "=== ACTIONS — the whole action-item checklist\n"
        "Name headings exactly as they appear in the notes. File each new point where it "
        "BELONGS, however long ago that section was opened; open a new section only for a "
        "genuinely new subject. Emit only the edits needed; if nothing new is worth noting, "
        "reply `=== END`.\n"
        "Example:\n"
        "=== APPEND ## Photosynthesis [05:00]\n"
        "- Light reactions need =={term}chlorophyll==\n"
        "=== NEW UNDER ## Cell structure [01:10]\n"
        "### Mitochondria\n"
        "- make **ATP**\n"
        "=== SUMMARY\n"
        "- Cells turn light (plants) and food (all) into ATP");
}

} // namespace QindaOffice::Ai::LiveNotesEdits
