// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_markdown_style.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include <functional>

namespace QindaOffice::Apps::Notes::MarkdownStyle {

namespace {

// Paper colours (D-004).
const QColor kAiNote(0x0f, 0x6e, 0x7a);
const QColor kStandOut(0xb3, 0x26, 0x1e);
const QColor kStamp(0x7a, 0x7f, 0x8c);
const QColor kTagInk(0x34, 0x46, 0x7a);
const QColor kTagBack(0xe6, 0xe9, 0xf2);
const QColor kRule(0xb8, 0xbe, 0xc9);
const QColor kHeaderBack(0xe9, 0xee, 0xf6);

// One inline style: its sentinel pair (private use, survives the parser).
enum Span { Ai, Key, Term, Example, Warn, Question, Date, Underline, StandOut, Sup, Sub, Tag,
            Stamp, SpanCount };
QChar openOf(Span span) { return QChar(0xE010 + 2 * int(span)); }
QChar closeOf(Span span) { return QChar(0xE011 + 2 * int(span)); }

QString wrap(Span span, const QString &capture)
{
    return QString(openOf(span)) + capture + QString(closeOf(span));
}

struct Kind {
    const char *id;
    const char *label; // shown on the box; empty for "big"
    QColor border;
    QColor back;
};

const Kind &kindOf(const QString &id)
{
    static const Kind kinds[] = {
        {"def", "≡  DEFINITION", QColor(0x5b, 0x8d, 0xef), QColor(0xee, 0xf4, 0xff)},
        {"key", "★  KEY IDEA", QColor(0xe0, 0xa8, 0x00), QColor(0xff, 0xf8, 0xdc)},
        {"example", "✎  EXAMPLE", QColor(0x43, 0xa0, 0x47), QColor(0xee, 0xf8, 0xee)},
        {"warn", "⚠  CAUTION", QColor(0xd9, 0x53, 0x4f), QColor(0xfd, 0xee, 0xee)},
        {"tip", "➜  TIP", QColor(0x1f, 0x9e, 0x8f), QColor(0xe9, 0xf7, 0xf5)},
        {"question", "?  QUESTION", QColor(0x7e, 0x57, 0xc2), QColor(0xf3, 0xee, 0xfc)},
        {"mnemonic", "✱  MEMORY AID", QColor(0xc2, 0x18, 0x5b), QColor(0xfc, 0xeb, 0xf2)},
        {"summary", "∑  SUMMARY", QColor(0x5c, 0x6b, 0x7a), QColor(0xf1, 0xf3, 0xf5)},
        {"quote", "❝", QColor(0x9a, 0xa0, 0xa6), QColor(0xfa, 0xfa, 0xfa)},
        {"big", "", QColor(0x1f, 0x3b, 0x73), QColor(0xee, 0xf2, 0xfa)},
        {"note", "✦  NOTE", QColor(0x8a, 0x93, 0x9e), QColor(0xf5, 0xf6, 0xf8)},
    };
    const QString want = id.toLower();
    // Model spellings for the same box.
    const QString alias = want == QLatin1String("definition") ? QStringLiteral("def")
        : want == QLatin1String("warning") || want == QLatin1String("caution") ? QStringLiteral("warn")
        : want == QLatin1String("important") ? QStringLiteral("key")
        : want;
    for (const Kind &kind : kinds) {
        if (alias == QLatin1String(kind.id)) {
            return kind;
        }
    }
    return kinds[std::size(kinds) - 1];
}

// Applies `transform` to the prose of `line`, never inside `code` spans.
QString outsideCode(const QString &line, const std::function<QString(QString)> &transform)
{
    const QStringList parts = line.split(QLatin1Char('`'));
    QStringList out;
    for (qsizetype i = 0; i < parts.size(); ++i) {
        out.append(i % 2 == 0 ? transform(parts.at(i)) : parts.at(i));
    }
    return out.join(QLatin1Char('`'));
}

QString markProse(QString text)
{
    static const QRegularExpression ai(QStringLiteral("\\[\\[ai:\\s*(.*?)\\]\\]"));
    static const QRegularExpression kinded(
        QStringLiteral("==\\{(key|term|def|example|warn|question|date)\\}(.+?)=="));
    static const QRegularExpression key(QStringLiteral("==([^=\\n](?:[^\\n]*?[^=\\n])?)=="));
    static const QRegularExpression underline(QStringLiteral("\\+\\+(?=\\S)([^+\\n]*?\\S)\\+\\+"));
    static const QRegularExpression standOut(QStringLiteral("!!(?=\\S)([^!\\n]*?\\S)!!"));
    static const QRegularExpression sup(QStringLiteral("\\^([^\\^\\s]+)\\^"));
    static const QRegularExpression sub(QStringLiteral("(?<!~)~([^~\\s]+)~(?!~)"));
    static const QRegularExpression tag(QStringLiteral("\\[#([\\w-]+)\\]"));
    static const QRegularExpression stamp(QStringLiteral("(\\[(?:\\d{1,2}:)?\\d{1,2}:\\d{2}\\])"));
    text.replace(ai, wrap(Ai, QStringLiteral("ⓘ \\1")));
    // Kinded highlights one by one (the kind picks the sentinel).
    QRegularExpressionMatch m;
    while ((m = kinded.match(text)).hasMatch()) {
        const QString k = m.captured(1);
        const Span span = k == QLatin1String("term") || k == QLatin1String("def") ? Term
            : k == QLatin1String("example")                                     ? Example
            : k == QLatin1String("warn")                                        ? Warn
            : k == QLatin1String("question")                                    ? Question
            : k == QLatin1String("date")                                        ? Date
                                                                                : Key;
        text.replace(m.capturedStart(), m.capturedLength(), wrap(span, m.captured(2)));
    }
    text.replace(key, wrap(Key, QStringLiteral("\\1")));
    text.replace(underline, wrap(Underline, QStringLiteral("\\1")));
    text.replace(standOut, wrap(StandOut, QStringLiteral("\\1")));
    text.replace(sup, wrap(Sup, QStringLiteral("\\1")));
    text.replace(sub, wrap(Sub, QStringLiteral("\\1")));
    text.replace(tag, wrap(Tag, QStringLiteral(" #\\1 ")));
    text.replace(stamp, wrap(Stamp, QStringLiteral("\\1")));
    return text;
}

// Formats the text between each open/close sentinel pair and removes the
// sentinels (close first, so the open's position holds).
void applySpans(QTextDocument *document, QChar openChar, QChar closeChar, const QTextCharFormat &format)
{
    QTextCursor open = document->find(QString(openChar));
    while (!open.isNull()) {
        const QTextCursor close = document->find(QString(closeChar), open.position());
        if (close.isNull()) {
            break;
        }
        QTextCursor span(document);
        span.setPosition(open.position());
        span.setPosition(close.position() - 1, QTextCursor::KeepAnchor);
        span.mergeCharFormat(format);
        QTextCursor removeClose(document);
        removeClose.setPosition(close.position() - 1);
        removeClose.setPosition(close.position(), QTextCursor::KeepAnchor);
        removeClose.removeSelectedText();
        QTextCursor removeOpen(document);
        removeOpen.setPosition(open.position() - 1);
        removeOpen.setPosition(open.position(), QTextCursor::KeepAnchor);
        removeOpen.removeSelectedText();
        open = document->find(QString(openChar));
    }
}

QTextCharFormat spanFormat(Span span, qreal base)
{
    QTextCharFormat f;
    switch (span) {
    case Ai:
        f.setForeground(kAiNote);
        f.setFontItalic(true);
        f.setFontPointSize(base * 0.9);
        break;
    case Key: f.setBackground(QColor(0xff, 0xee, 0x6e)); break;
    case Term: f.setBackground(QColor(0xcf, 0xe3, 0xff)); break;
    case Example: f.setBackground(QColor(0xd4, 0xf5, 0xd0)); break;
    case Warn: f.setBackground(QColor(0xff, 0xd0, 0xd0)); break;
    case Question: f.setBackground(QColor(0xe6, 0xd8, 0xff)); break;
    case Date: f.setBackground(QColor(0xff, 0xdc, 0xb0)); break;
    case Underline: f.setFontUnderline(true); break;
    case StandOut:
        f.setForeground(kStandOut);
        f.setFontWeight(QFont::Bold);
        f.setFontPointSize(base * 1.15);
        break;
    case Sup: f.setVerticalAlignment(QTextCharFormat::AlignSuperScript); break;
    case Sub: f.setVerticalAlignment(QTextCharFormat::AlignSubScript); break;
    case Tag:
        f.setForeground(kTagInk);
        f.setBackground(kTagBack);
        f.setFontWeight(QFont::DemiBold);
        f.setFontPointSize(base * 0.82);
        break;
    case Stamp:
        f.setForeground(kStamp);
        f.setFontWeight(QFont::Normal);
        f.setFontPointSize(base * 0.8);
        break;
    case SpanCount: break;
    }
    return f;
}

void insertCallout(QTextCursor &cursor, const Kind &kind, const QString &title, const QString &body)
{
    QTextDocument *document = cursor.document();
    const qreal base = document->defaultFont().pointSizeF();
    const bool big = QLatin1String(kind.id) == QLatin1String("big");
    QTextFrameFormat frame;
    frame.setBorder(1);
    frame.setBorderBrush(kind.border);
    frame.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    frame.setBackground(kind.back);
    frame.setPadding(big ? 10 : 6);
    frame.setTopMargin(4);
    frame.setBottomMargin(6);
    frame.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    QTextFrame *box = cursor.insertFrame(frame);
    const int start = cursor.position();
    const bool quote = QLatin1String(kind.id) == QLatin1String("quote");
    const QString label = QString::fromUtf8(kind.label)
        + (title.isEmpty() ? QString() : (big || quote ? QStringLiteral("  ") : QStringLiteral("  ·  ")) + title);
    if (!label.trimmed().isEmpty()) {
        QTextCharFormat labelFormat;
        labelFormat.setForeground(kind.border.darker(130));
        labelFormat.setFontWeight(QFont::Bold);
        labelFormat.setFontPointSize(base * 0.85);
        labelFormat.setFontCapitalization(QFont::SmallCaps);
        cursor.insertText(label, labelFormat);
        cursor.insertBlock();
        cursor.setCharFormat(QTextCharFormat());
    }
    if (!body.trimmed().isEmpty()) {
        cursor.insertMarkdown(body, QTextDocument::MarkdownDialectGitHub);
    }
    if (big || quote) {
        QTextCursor all(document);
        all.setPosition(start);
        all.setPosition(box->lastPosition(), QTextCursor::KeepAnchor);
        QTextCharFormat f;
        if (big) {
            f.setFontPointSize(base * 1.45);
            f.setFontWeight(QFont::Bold);
            f.setForeground(kind.border);
            QTextBlockFormat centred;
            centred.setAlignment(Qt::AlignHCenter);
            all.mergeBlockFormat(centred);
        } else {
            f.setFontItalic(true);
        }
        all.mergeCharFormat(f);
    }
    cursor = document->rootFrame()->lastCursorPosition();
}

} // namespace

QString markInline(const QString &markdown)
{
    QStringList lines = markdown.split(QLatin1Char('\n'));
    bool fence = false;
    for (QString &line : lines) {
        if (line.trimmed().startsWith(QLatin1String("```"))) {
            fence = !fence;
            continue;
        }
        if (!fence) {
            line = outsideCode(line, markProse);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void applyInline(QTextDocument *document)
{
    const qreal base = document->defaultFont().pointSizeF();
    for (int span = 0; span < SpanCount; ++span) {
        applySpans(document, openOf(Span(span)), closeOf(Span(span)), spanFormat(Span(span), base));
    }
}

void insertWithCallouts(QTextCursor &cursor, const QString &markdown)
{
    // AGENT-NOTE: small local models write a callout inside a bullet, often
    // bolded ("- **> [!warn] Caution**") with its content on the indented
    // lines below; that is read as the same callout so it still renders as
    // a box (seen with Qwen3.5-4B, docs/live-notes.md).
    static const QRegularExpression calloutStart(QStringLiteral(
        "^(\\s*)(?:[-*+]\\s+)?(?:\\*\\*)?>\\s*\\[!(\\w+)\\][-+]?\\s*(.*?)(?:\\*\\*)?\\s*$"));
    static const QRegularExpression quoted(QStringLiteral("^\\s*>\\s?"));
    const auto indentOf = [](const QString &line) {
        qsizetype n = 0;
        while (n < line.size() && line.at(n).isSpace()) {
            ++n;
        }
        return n;
    };
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    QStringList plain;
    const auto flush = [&] {
        const QString text = plain.join(QLatin1Char('\n'));
        plain.clear();
        if (!text.trimmed().isEmpty()) {
            cursor.insertMarkdown(text, QTextDocument::MarkdownDialectGitHub);
            cursor = cursor.document()->rootFrame()->lastCursorPosition();
        }
    };
    bool fence = false;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (line.trimmed().startsWith(QLatin1String("```"))) {
            fence = !fence;
        }
        const QRegularExpressionMatch m = fence ? QRegularExpressionMatch() : calloutStart.match(line);
        if (!m.hasMatch()) {
            plain.append(line);
            continue;
        }
        flush();
        const qsizetype indent = m.capturedLength(1);
        QStringList body;
        while (i + 1 < lines.size() && !lines.at(i + 1).trimmed().isEmpty()
               && !calloutStart.match(lines.at(i + 1)).hasMatch()
               && (lines.at(i + 1).trimmed().startsWith(QLatin1Char('>'))
                   || indentOf(lines.at(i + 1)) > indent)) {
            body.append(lines.at(++i).trimmed().remove(quoted));
        }
        insertCallout(cursor, kindOf(m.captured(2)), m.captured(3).trimmed(), body.join(QLatin1Char('\n')));
    }
    flush();
}

void styleTables(QTextDocument *document)
{
    std::function<void(QTextFrame *)> visit = [&](QTextFrame *frame) {
        for (QTextFrame *child : frame->childFrames()) {
            if (auto *table = qobject_cast<QTextTable *>(child)) {
                QTextTableFormat format = table->format();
                format.setBorder(1);
                format.setBorderBrush(kRule);
                format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
                format.setBorderCollapse(true);
                format.setCellPadding(4);
                format.setCellSpacing(0);
                format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
                table->setFormat(format);
                for (int column = 0; column < table->columns(); ++column) {
                    QTextTableCell cell = table->cellAt(0, column);
                    QTextCharFormat header = cell.format();
                    header.setBackground(kHeaderBack);
                    header.setFontWeight(QFont::Bold);
                    cell.setFormat(header);
                    QTextCursor text = cell.firstCursorPosition();
                    text.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
                    QTextCharFormat bold;
                    bold.setFontWeight(QFont::Bold);
                    text.mergeCharFormat(bold);
                }
            }
            visit(child);
        }
    };
    visit(document->rootFrame());
}

void tightenFrames(QTextDocument *document)
{
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        if (!block.text().isEmpty()) {
            continue;
        }
        const QTextFrame *here = QTextCursor(block).currentFrame();
        const bool atSeam = (block.next().isValid() && QTextCursor(block.next()).currentFrame() != here)
            || (block.previous().isValid() && QTextCursor(block.previous()).currentFrame() != here);
        if (!atSeam) {
            continue;
        }
        QTextCursor cursor(block);
        QTextBlockFormat format = block.blockFormat();
        format.setLineHeight(1, QTextBlockFormat::FixedHeight);
        format.setTopMargin(0);
        format.setBottomMargin(0);
        cursor.setBlockFormat(format);
        QTextCharFormat tiny;
        tiny.setFontPointSize(1);
        cursor.setBlockCharFormat(tiny);
    }
}

} // namespace QindaOffice::Apps::Notes::MarkdownStyle
