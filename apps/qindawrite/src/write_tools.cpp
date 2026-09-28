// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_tools.h"

#include "page_text_control.h"
#include "write_document.h"

#include <QJsonObject>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

namespace {

const auto kDialect = QTextDocument::MarkdownDialectGitHub;

QString plain(const QString &text)
{
    QString out = text;
    out.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return out;
}

// The block range of the section headed `heading`: from that heading to
// the next heading of the same or a higher level (or the end).
bool sectionCursor(QTextDocument *document, const QString &heading, QTextCursor *out)
{
    const QString wanted = heading.trimmed().remove(QLatin1Char('#')).trimmed();
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const int level = block.blockFormat().headingLevel();
        if (level <= 0 || block.text().trimmed().compare(wanted, Qt::CaseInsensitive) != 0) {
            continue;
        }
        QTextBlock end = block.next();
        while (end.isValid()) {
            const int next = end.blockFormat().headingLevel();
            if (next > 0 && next <= level) {
                break;
            }
            end = end.next();
        }
        QTextCursor cursor(document);
        cursor.setPosition(block.position());
        cursor.setPosition(end.isValid() ? end.position() - 1
                                         : document->characterCount() - 1,
                           QTextCursor::KeepAnchor);
        *out = cursor;
        return true;
    }
    return false;
}

// QTextCursor::insertMarkdown merges the first inserted block INTO the
// block at the cursor, which keeps its own paragraph format: a leading
// "## Heading" came out as a bold plain paragraph. When the insertion
// starts a block, give that block the format the Markdown asked for.
void insertMarkdown(QTextCursor &cursor, const QString &markdown)
{
    const int start = cursor.selectionStart();
    QTextCursor probe(cursor.document());
    probe.setPosition(start);
    const bool atBlockStart = probe.atBlockStart();
    cursor.insertMarkdown(markdown, kDialect);
    if (!atBlockStart) {
        return;
    }
    QTextDocument parsed;
    parsed.setMarkdown(markdown, kDialect);
    if (parsed.begin().blockFormat().headingLevel() > 0) {
        // A fresh cursor: one at the insertion point moved with the text.
        QTextCursor first(cursor.document());
        first.setPosition(start);
        first.setBlockFormat(parsed.begin().blockFormat());
    }
}

} // namespace

QString WriteTools::snapshot(int budget)
{
    QTextDocument *document = m_document->textDocument();
    QString markdown = document->toMarkdown(kDialect);
    QString out;
    if (markdown.size() > budget) {
        out += QStringLiteral("(The document is long: only its first %1 characters are shown. "
                              "Prefer replace_section and replace_text over replace_document.)\n")
                   .arg(budget);
        markdown.truncate(budget);
    }
    out += QStringLiteral("<<<DOCUMENT (Markdown)\n%1\nDOCUMENT>>>\n").arg(markdown.trimmed());
    const QTextCursor cursor = m_control->textCursor();
    if (cursor.hasSelection()) {
        out += QStringLiteral("\nSelected text:\n«%1»\n").arg(plain(cursor.selectedText()).left(4000));
    } else {
        out += QStringLiteral("\nThe cursor is in the paragraph: «%1»\n")
                   .arg(cursor.block().text().left(400));
    }
    return out;
}

QString WriteTools::operationGuide() const
{
    return QStringLiteral(
        "Content is GitHub Markdown (# headings, lists, **bold**, *italic*, tables, > quotes).\n"
        "- {\"op\":\"replace_document\",\"markdown\":\"…\"} — the whole new document. Use for "
        "rewriting or restructuring everything.\n"
        "- {\"op\":\"replace_section\",\"heading\":\"Results\",\"markdown\":\"## Results\\n…\"} "
        "— replace one section: from that heading up to the next heading of the same or higher "
        "level. Include the heading in the new markdown if it should stay.\n"
        "- {\"op\":\"replace_text\",\"find\":\"teh\",\"replace\":\"the\",\"all\":true,"
        "\"case_sensitive\":false} — plain-text find and replace, keeping formatting.\n"
        "- {\"op\":\"insert\",\"markdown\":\"…\",\"at\":\"end\"} — at \"start\", \"end\" or "
        "\"cursor\".\n"
        "- {\"op\":\"replace_selection\",\"markdown\":\"…\"} — only when text is selected.");
}

Ai::EditOutcome WriteTools::apply(const QJsonArray &edits)
{
    Ai::EditOutcome outcome;
    QTextDocument *document = m_document->textDocument();
    const QTextCursor live = m_control->textCursor();
    // AGENT-GUARD (write_host_text.cpp's replaceAll): the edit block's cursor
    // is never null, or the document's edit-block counter sticks and every
    // later undo step merges into one.
    QTextCursor block(document);
    block.beginEditBlock();
    int caret = live.position();
    for (const QJsonValue &value : edits) {
        const QJsonObject edit = value.toObject();
        const QString op = edit.value(QStringLiteral("op")).toString();
        const QString markdown = edit.value(QStringLiteral("markdown")).toString();
        if (op == QLatin1String("replace_document")) {
            QTextCursor all(document);
            all.select(QTextCursor::Document);
            insertMarkdown(all, markdown);
            caret = 0;
            outcome.changes.append(QObject::tr("rewrote the whole document"));
        } else if (op == QLatin1String("replace_section")) {
            const QString heading = edit.value(QStringLiteral("heading")).toString();
            QTextCursor section;
            if (!sectionCursor(document, heading, &section)) {
                outcome.errors.append(QObject::tr("no heading “%1”").arg(heading));
                continue;
            }
            caret = section.selectionStart();
            insertMarkdown(section, markdown);
            outcome.changes.append(QObject::tr("rewrote the section “%1”").arg(heading));
        } else if (op == QLatin1String("replace_text")) {
            const QString find = edit.value(QStringLiteral("find")).toString();
            if (find.isEmpty()) {
                outcome.errors.append(QObject::tr("replace_text without \"find\""));
                continue;
            }
            const bool all = edit.value(QStringLiteral("all")).toBool(true);
            const QTextDocument::FindFlags flags =
                edit.value(QStringLiteral("case_sensitive")).toBool() ? QTextDocument::FindCaseSensitively
                                                                      : QTextDocument::FindFlags();
            const QString replacement = edit.value(QStringLiteral("replace")).toString();
            int count = 0;
            QTextCursor hit = document->find(find, QTextCursor(document), flags);
            while (!hit.isNull()) {
                hit.insertText(replacement);
                ++count;
                if (!all) {
                    break;
                }
                hit = document->find(find, hit, flags);
            }
            if (count == 0) {
                outcome.errors.append(QObject::tr("“%1” is not in the document").arg(find));
                continue;
            }
            outcome.changes.append(QObject::tr("replaced “%1” (%n time(s))", "", count).arg(find));
        } else if (op == QLatin1String("insert")) {
            const QString at = edit.value(QStringLiteral("at")).toString(QStringLiteral("end"));
            QTextCursor where(document);
            if (at == QLatin1String("cursor")) {
                where.setPosition(qMin(caret, document->characterCount() - 1));
            } else if (at == QLatin1String("start")) {
                where.movePosition(QTextCursor::Start);
            } else {
                where.movePosition(QTextCursor::End);
            }
            if (!where.atBlockStart()) {
                where.insertBlock();
            }
            insertMarkdown(where, markdown);
            outcome.changes.append(QObject::tr("inserted text at the %1").arg(at));
        } else if (op == QLatin1String("replace_selection")) {
            QTextCursor selection = live;
            if (!selection.hasSelection()) {
                outcome.errors.append(QObject::tr("nothing is selected"));
                continue;
            }
            insertMarkdown(selection, markdown);
            outcome.changes.append(QObject::tr("replaced the selection"));
        } else {
            outcome.errors.append(QObject::tr("unknown operation “%1”").arg(op));
        }
    }
    block.endEditBlock();
    outcome.applied = int(outcome.changes.size());
    if (outcome.applied > 0) {
        QTextCursor cursor(document);
        cursor.setPosition(qBound(0, caret, document->characterCount() - 1));
        m_control->setTextCursor(cursor);
        m_document->setDirty(true);
    }
    return outcome;
}

void WriteTools::undo()
{
    m_control->undo();
}

} // namespace QindaOffice::Apps::Write
