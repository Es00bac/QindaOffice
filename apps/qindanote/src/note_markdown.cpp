// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_markdown.h"

#include "note_blocks.h"
#include "note_markdown_style.h"

#include <QAbstractTextDocumentLayout>
#include <QHash>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <memory>

namespace QindaOffice::Apps::Notes {

namespace {

// Paper colours (D-004).
const QColor kInk(0x20, 0x20, 0x28);
const QColor kHeading(0x1f, 0x3b, 0x73);
const QColor kQuote(0x4a, 0x4a, 0x58);
constexpr int kCacheLimit = 24;

void applyPaperStyle(QTextDocument *document)
{
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const int level = block.blockFormat().headingLevel();
        const bool quote = block.blockFormat().hasProperty(QTextFormat::BlockQuoteLevel)
            && block.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) > 0;
        if (level == 0 && !quote) {
            continue;
        }
        QTextCursor cursor(block);
        cursor.select(QTextCursor::BlockUnderCursor);
        QTextCharFormat format;
        if (level > 0) {
            format.setForeground(kHeading);
            format.setFontWeight(QFont::Bold);
        } else {
            format.setForeground(kQuote);
            format.setFontItalic(true);
        }
        cursor.mergeCharFormat(format);
        if (level > 0) {
            QTextBlockFormat spacing;
            spacing.setTopMargin(level <= 2 ? 10 : 6);
            spacing.setBottomMargin(3);
            cursor.mergeBlockFormat(spacing);
        }
    }
}

std::shared_ptr<QTextDocument> build(const QString &markdown, qreal width)
{
    // One layout per (text, width): painting runs every frame the page is
    // exposed, and a long set of notes is expensive to parse.
    static QHash<QString, std::shared_ptr<QTextDocument>> cache;
    const QString key = QString::number(qRound(width)) + QLatin1Char('\n') + markdown;
    if (const auto it = cache.constFind(key); it != cache.constEnd()) {
        return it.value();
    }
    auto document = std::make_shared<QTextDocument>();
    document->setDefaultFont(pageFont());
    document->setDocumentMargin(0);
    // M57: callouts become frames, the inline vocabulary becomes formats
    // (note_markdown_style.h); plain GitHub Markdown renders as before.
    QTextCursor cursor(document.get());
    MarkdownStyle::insertWithCallouts(cursor, MarkdownStyle::markInline(markdown));
    // Headings first, so a span inside one (a grey [mm:ss], a badge) wins.
    applyPaperStyle(document.get());
    MarkdownStyle::applyInline(document.get());
    MarkdownStyle::styleTables(document.get());
    MarkdownStyle::tightenFrames(document.get());
    document->setTextWidth(qMax<qreal>(20, width - 2 * kTextPadding));
    if (cache.size() >= kCacheLimit) {
        cache.clear();
    }
    cache.insert(key, document);
    return document;
}

} // namespace

qreal markdownBlockHeight(const QString &markdown, qreal width)
{
    if (markdown.trimmed().isEmpty()) {
        return kTextBlockMinHeight;
    }
    const auto document = build(markdown, width);
    return qMax<qreal>(kTextBlockMinHeight, std::ceil(document->size().height()) + 2 * kTextPadding);
}

void paintMarkdown(QPainter *painter, const QRectF &box, const QString &markdown)
{
    const auto document = build(markdown, box.width());
    painter->save();
    painter->translate(box.topLeft() + QPointF(kTextPadding, kTextPadding));
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, kInk);
    context.clip = QRectF(0, 0, box.width(), box.height());
    document->documentLayout()->draw(painter, context);
    painter->restore();
}

qint64 markdownTimestampAt(const QString &markdown, qreal width, const QPointF &point)
{
    const auto document = build(markdown, width);
    const int position = document->documentLayout()->hitTest(point - QPointF(kTextPadding, kTextPadding),
                                                            Qt::FuzzyHit);
    if (position < 0) {
        return -1;
    }
    const QTextBlock block = document->findBlock(position);
    const QString text = block.text();
    const int offset = position - block.position();
    static const QRegularExpression stamp(QStringLiteral("\\[((?:\\d{1,2}:)?\\d{1,2}:\\d{2})\\]"));
    for (auto it = stamp.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        if (offset >= m.capturedStart() - 1 && offset <= m.capturedEnd() + 1) {
            const QStringList parts = m.captured(1).split(QLatin1Char(':'));
            qint64 total = 0;
            for (const QString &part : parts) {
                total = total * 60 + part.toInt();
            }
            return total * 1000;
        }
    }
    return -1;
}

QString markdownHtml(const QString &markdown)
{
    return build(markdown, 700)->toHtml();
}

QString markdownPlainText(const QString &markdown)
{
    return build(markdown, 600)->toPlainText();
}

} // namespace QindaOffice::Apps::Notes
