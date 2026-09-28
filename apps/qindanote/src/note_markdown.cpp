// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_markdown.h"

#include "note_blocks.h"

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
const QColor kHighlight(0xff, 0xee, 0x6e);
// Private-use sentinels that survive the Markdown parser.
const QChar kMarkOpen(0xE000);
const QChar kMarkClose(0xE001);
constexpr int kCacheLimit = 24;

void applyHighlights(QTextDocument *document)
{
    QTextCursor open = document->find(QString(kMarkOpen));
    while (!open.isNull()) {
        const QTextCursor close = document->find(QString(kMarkClose), open.position());
        if (close.isNull()) {
            break;
        }
        QTextCursor span(document);
        span.setPosition(open.position());
        span.setPosition(close.position() - 1, QTextCursor::KeepAnchor);
        QTextCharFormat marker;
        marker.setBackground(kHighlight);
        span.mergeCharFormat(marker);
        // Remove the sentinels, closing one first so positions hold.
        QTextCursor removeClose(document);
        removeClose.setPosition(close.position() - 1);
        removeClose.setPosition(close.position(), QTextCursor::KeepAnchor);
        removeClose.removeSelectedText();
        QTextCursor removeOpen(document);
        removeOpen.setPosition(open.position() - 1);
        removeOpen.setPosition(open.position(), QTextCursor::KeepAnchor);
        removeOpen.removeSelectedText();
        open = document->find(QString(kMarkOpen));
    }
}

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
    static const QRegularExpression highlight(QStringLiteral("==([^=\\n](?:[^\\n]*?[^=\\n])?)=="));
    QString source = markdown;
    source.replace(highlight, QString(kMarkOpen) + QStringLiteral("\\1") + QString(kMarkClose));

    auto document = std::make_shared<QTextDocument>();
    document->setDefaultFont(pageFont());
    document->setDocumentMargin(0);
    document->setMarkdown(source, QTextDocument::MarkdownDialectGitHub);
    applyHighlights(document.get());
    applyPaperStyle(document.get());
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

QString markdownPlainText(const QString &markdown)
{
    return build(markdown, 600)->toPlainText();
}

} // namespace QindaOffice::Apps::Notes
