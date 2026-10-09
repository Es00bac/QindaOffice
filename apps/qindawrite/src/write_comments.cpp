// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_comments.h"

#include "docx_comments.h"
#include "docx_properties.h"
#include "page_text_control.h"
#include "write_document.h"

#include <QLocale>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

namespace QindaOffice::Apps::Write {

namespace {

constexpr QChar kMark(0x200B);
constexpr int kQuoteLength = 80;

// Adds (or removes) `ids` on every run in [start, end), keeping the other
// comments' ids and everything else those runs carry.
void retag(QTextDocument *document, int start, int end, const QStringList &ids, bool add)
{
    struct Run {
        int from;
        int to;
        QTextCharFormat format;
    };
    QList<Run> runs;
    for (QTextBlock block = document->findBlock(start); block.isValid() && block.position() < end;
         block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const int from = qMax(start, fragment.position());
            const int to = qMin(end, fragment.position() + fragment.length());
            if (from >= to) {
                continue;
            }
            QTextCharFormat format = fragment.charFormat();
            QStringList now = format.property(Docx::kCommentIds).toStringList();
            for (const QString &id : ids) {
                if (add && !now.contains(id)) {
                    now.append(id);
                } else if (!add) {
                    now.removeAll(id);
                }
            }
            if (now.isEmpty()) {
                format.clearProperty(Docx::kCommentIds);
            } else {
                format.setProperty(Docx::kCommentIds, now);
            }
            runs.append({from, to, format});
        }
    }
    for (const Run &run : std::as_const(runs)) {
        QTextCursor cursor(document);
        cursor.setPosition(run.from);
        cursor.setPosition(run.to, QTextCursor::KeepAnchor);
        cursor.setCharFormat(run.format);
    }
}

QString dateText(const QDateTime &when)
{
    return when.isValid() ? QLocale().toString(when.toLocalTime(), QLocale::ShortFormat) : QString();
}

} // namespace

WriteComments::WriteComments(WriteDocument *document, PageTextControl *control, std::function<QString()> author,
                             QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_control(control)
    , m_author(std::move(author))
{
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(250);
    connect(&m_refresh, &QTimer::timeout, this, &WriteComments::changed);
    connect(m_document, &WriteDocument::commentsChanged, this, &WriteComments::changed);
    connect(m_document->textDocument(), &QTextDocument::contentsChanged, &m_refresh, qOverload<>(&QTimer::start));
    connect(m_document, &WriteDocument::activeCommentChanged, this, &WriteComments::activeChanged);
    connect(m_control, &PageTextControl::cursorPositionChanged, this, &WriteComments::followCaret);
    connect(m_control, &PageTextControl::selectionChanged, this, [this] {
        const bool can = canAdd();
        if (can != m_canAdd) {
            m_canAdd = can;
            emit canAddChanged();
        }
    });
}

QString WriteComments::activeId() const
{
    return m_document->activeComment();
}

bool WriteComments::canAdd() const
{
    return m_control->document() == m_document->textDocument() && m_control->textCursor().hasSelection();
}

QString WriteComments::authorInitials(const QString &author) const
{
    QString initials;
    for (const QString &word : author.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        initials += word.at(0).toUpper();
    }
    return initials.left(3);
}

QStringList WriteComments::orderedTopLevel() const
{
    // Text order of the comments still marked in the text.
    QList<Docx::CommentSpan> spans = m_document->commentSpans();
    std::stable_sort(spans.begin(), spans.end(), [](const Docx::CommentSpan &a, const Docx::CommentSpan &b) {
        return a.start < b.start || (a.start == b.start && a.reference < b.reference);
    });
    QStringList ids;
    for (const Docx::CommentSpan &span : std::as_const(spans)) {
        if (span.reference < 0) {
            continue;
        }
        for (const Docx::Comment &c : m_document->comments()) {
            if (c.id == span.id && c.parentId.isEmpty()) {
                ids.append(c.id);
            }
        }
    }
    return ids;
}

QVariantList WriteComments::comments() const
{
    const QString text = m_document->textDocument()->toPlainText();
    QHash<QString, Docx::CommentSpan> spans;
    for (const Docx::CommentSpan &span : m_document->commentSpans()) {
        spans.insert(span.id, span);
    }
    auto entry = [&](const Docx::Comment &c) {
        const Docx::CommentSpan span = spans.value(c.id);
        QString quote = span.end > span.start ? text.mid(span.start, span.end - span.start) : QString();
        quote.replace(QChar::ParagraphSeparator, QLatin1Char(' ')).remove(kMark);
        if (quote.size() > kQuoteLength) {
            quote = quote.left(kQuoteLength - 1) + QChar(0x2026);
        }
        return QVariantMap{{QStringLiteral("id"), c.id},          {QStringLiteral("author"), c.author},
                           {QStringLiteral("initials"), c.initials}, {QStringLiteral("date"), dateText(c.date)},
                           {QStringLiteral("text"), c.text},      {QStringLiteral("done"), c.done},
                           {QStringLiteral("quote"), quote.simplified()}};
    };
    QVariantList out;
    for (const QString &id : orderedTopLevel()) {
        for (const Docx::Comment &c : m_document->comments()) {
            if (c.id != id) {
                continue;
            }
            QVariantMap top = entry(c);
            QVariantList replies;
            for (const Docx::Comment &r : m_document->comments()) {
                if (r.parentId == id && spans.value(r.id).reference >= 0) {
                    replies.append(entry(r));
                }
            }
            top.insert(QStringLiteral("replies"), replies);
            out.append(top);
        }
    }
    return out;
}

int WriteComments::count() const
{
    return int(orderedTopLevel().size());
}

QString WriteComments::add(const QString &text)
{
    QTextDocument *body = m_document->textDocument();
    if (m_control->document() != body || text.trimmed().isEmpty()) {
        return {};
    }
    QTextCursor selection = m_control->textCursor();
    if (!selection.hasSelection()) {
        selection.select(QTextCursor::WordUnderCursor);
    }
    const int start = selection.selectionStart();
    const int end = selection.selectionEnd();
    if (start >= end) {
        emit message(tr("Select the text the comment is about"));
        return {};
    }
    QList<Docx::Comment> list = m_document->comments();
    Docx::Comment comment;
    comment.id = Docx::nextCommentId(list);
    comment.author = m_author ? m_author() : QString();
    comment.initials = authorInitials(comment.author);
    comment.date = QDateTime::currentDateTimeUtc();
    comment.text = text.trimmed();
    comment.paraId = Docx::newParaId();
    QTextCursor edit(body);
    edit.beginEditBlock();
    retag(body, start, end, {comment.id}, true);
    QTextCursor mark(body);
    mark.setPosition(end);
    QTextCharFormat format = mark.charFormat();
    format.clearProperty(Docx::kCommentIds);
    format.setProperty(Docx::kCommentReference, comment.id);
    mark.insertText(QString(kMark), format);
    edit.endEditBlock();
    list.append(comment);
    m_document->setComments(std::move(list));
    m_document->setActiveComment(comment.id);
    return comment.id;
}

QString WriteComments::reply(const QString &id, const QString &text)
{
    QTextDocument *body = m_document->textDocument();
    Docx::CommentSpan parent;
    for (const Docx::CommentSpan &span : m_document->commentSpans()) {
        if (span.id == id) {
            parent = span;
        }
    }
    if (parent.reference < 0 || text.trimmed().isEmpty()) {
        return {};
    }
    QList<Docx::Comment> list = m_document->comments();
    Docx::Comment answer;
    answer.id = Docx::nextCommentId(list);
    answer.author = m_author ? m_author() : QString();
    answer.initials = authorInitials(answer.author);
    answer.date = QDateTime::currentDateTimeUtc();
    answer.text = text.trimmed();
    answer.parentId = id;
    answer.paraId = Docx::newParaId();
    // Word gives a reply the same range and its own mark after the parent's.
    QTextCursor edit(body);
    edit.beginEditBlock();
    if (parent.end > parent.start) {
        retag(body, parent.start, parent.end, {answer.id}, true);
    }
    QTextCursor mark(body);
    mark.setPosition(parent.reference);
    mark.setPosition(parent.reference + 1, QTextCursor::KeepAnchor);
    QTextCharFormat format = mark.charFormat();
    format.setProperty(Docx::kCommentReference, answer.id);
    mark.setPosition(parent.reference + 1);
    mark.insertText(QString(kMark), format);
    edit.endEditBlock();
    list.append(answer);
    m_document->setComments(std::move(list));
    return answer.id;
}

bool WriteComments::edit(const QString &id, const QString &text)
{
    QList<Docx::Comment> list = m_document->comments();
    for (Docx::Comment &c : list) {
        if (c.id == id && c.text != text.trimmed() && !text.trimmed().isEmpty()) {
            c.text = text.trimmed();
            m_document->setComments(std::move(list));
            return true;
        }
    }
    return false;
}

bool WriteComments::setDone(const QString &id, bool done)
{
    QList<Docx::Comment> list = m_document->comments();
    for (Docx::Comment &c : list) {
        if (c.id == id && c.done != done) {
            c.done = done;
            m_document->setComments(std::move(list));
            return true;
        }
    }
    return false;
}

bool WriteComments::remove(const QString &id)
{
    QStringList ids{id};
    for (qsizetype i = 0; i < ids.size(); ++i) { // replies, and replies to them
        for (const Docx::Comment &c : m_document->comments()) {
            if (c.parentId == ids.at(i) && !ids.contains(c.id)) {
                ids.append(c.id);
            }
        }
    }
    QTextDocument *body = m_document->textDocument();
    QList<int> marks;
    int start = -1;
    int end = -1;
    for (const Docx::CommentSpan &span : m_document->commentSpans()) {
        if (!ids.contains(span.id)) {
            continue;
        }
        if (span.reference >= 0) {
            marks.append(span.reference);
        }
        if (span.end > span.start) {
            start = start < 0 ? span.start : qMin(start, span.start);
            end = qMax(end, span.end);
        }
    }
    if (marks.isEmpty()) {
        return false;
    }
    QTextCursor edit(body);
    edit.beginEditBlock();
    if (start >= 0) {
        retag(body, start, end, ids, false);
    }
    std::sort(marks.begin(), marks.end(), std::greater<int>());
    for (const int at : std::as_const(marks)) {
        QTextCursor mark(body);
        mark.setPosition(at);
        mark.setPosition(at + 1, QTextCursor::KeepAnchor);
        mark.removeSelectedText();
    }
    edit.endEditBlock();
    QList<Docx::Comment> list = m_document->comments();
    list.removeIf([&](const Docx::Comment &c) { return ids.contains(c.id); });
    m_document->setComments(std::move(list));
    m_document->setActiveComment(QString());
    return true;
}

bool WriteComments::select(const QString &id)
{
    for (const Docx::CommentSpan &span : m_document->commentSpans()) {
        if (span.id != id || span.reference < 0 || m_control->document() != m_document->textDocument()) {
            continue;
        }
        QTextCursor cursor(m_document->textDocument());
        cursor.setPosition(span.end > span.start ? span.start : span.reference);
        cursor.setPosition(span.end > span.start ? span.end : span.reference + 1, QTextCursor::KeepAnchor);
        m_control->setTextCursor(cursor);
        m_document->setActiveComment(id);
        return true;
    }
    return false;
}

bool WriteComments::step(int direction)
{
    const QStringList ids = orderedTopLevel();
    if (ids.isEmpty()) {
        emit message(tr("No comments"));
        return false;
    }
    const qsizetype at = ids.indexOf(activeId());
    const qsizetype next = at < 0 ? (direction > 0 ? 0 : ids.size() - 1) : (at + direction + ids.size()) % ids.size();
    return select(ids.at(next));
}

bool WriteComments::next()
{
    return step(1);
}

bool WriteComments::previous()
{
    return step(-1);
}

void WriteComments::followCaret()
{
    if (m_control->document() != m_document->textDocument() || m_document->comments().isEmpty()) {
        return;
    }
    const int position = m_control->textCursor().position();
    QString inside;
    for (const Docx::CommentSpan &span : m_document->commentSpans()) {
        if (span.start <= position && position <= qMax(span.end, span.reference + 1)) {
            inside = span.id;
            for (const Docx::Comment &c : m_document->comments()) {
                if (c.id == span.id && !c.parentId.isEmpty()) {
                    inside = c.parentId; // the thread, not the reply
                }
            }
            break;
        }
    }
    if (!inside.isEmpty()) {
        m_document->setActiveComment(inside);
    }
}

} // namespace QindaOffice::Apps::Write
