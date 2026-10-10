// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_word_export.h"

#include "docx_properties.h"
#include "docx_text.h"
#include "docx_writer.h"
#include "ink_persistence.h"
#include "note_blocks.h"
#include "note_markdown.h"
#include "note_rich_export.h"
#include "notebook.h"

#include <QBuffer>
#include <QFont>
#include <QImage>
#include <QJsonObject>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>

#include <algorithm>

namespace QindaOffice::Apps::Notes {

namespace {

void insertPicture(QTextCursor &cursor, Docx::WordParts &parts, QTextDocument &doc, const QByteArray &bytes,
                   qreal maxWidth)
{
    const QImage image = QImage::fromData(bytes);
    const QString name = Docx::addPicture(&doc, &parts.media, bytes);
    if (image.isNull() || name.isEmpty()) {
        return;
    }
    QSizeF size(image.width(), image.height());
    if (size.width() > maxWidth) {
        size = QSizeF(maxWidth, size.height() * maxWidth / size.width());
    }
    QTextImageFormat format;
    format.setName(name);
    format.setWidth(size.width());
    format.setHeight(size.height());
    cursor.insertImage(format);
}

QByteArray pngOf(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// A Markdown block's rich text at the cursor. Its headings become Word's
// Heading 1–6 (Write style ids 2–7), so they show in Word's navigation pane.
void insertMarkdown(QTextCursor &cursor, const QString &markdown, qreal width)
{
    const auto rendered = markdownDocument(markdown, width);
    // AGENT-NOTE: insertFragment() merges the fragment's first block into
    // the current one without its block format (a leading heading would lose
    // its level and spacing), so that format is applied first.
    cursor.setBlockFormat(rendered->begin().blockFormat());
    const int start = cursor.position();
    cursor.insertFragment(QTextDocumentFragment(rendered.get()));
    QTextDocument *doc = cursor.document();
    for (QTextBlock block = doc->findBlock(start); block.isValid() && block.position() <= cursor.position();
         block = block.next()) {
        if (const int level = block.blockFormat().headingLevel(); level > 0) {
            QTextBlockFormat style;
            style.setProperty(Docx::kWriteStyleIdProperty, 1 + qMin(level, 6)); // StyleCatalog::Heading1…
            QTextCursor(block).mergeBlockFormat(style);
        }
    }
}

} // namespace

bool exportPageAsWord(const NotePage &page, qreal pageWidth, const QString &path, QString *error)
{
    QTextDocument doc;
    // Word's own body face at the page's size: the file is for Office users,
    // and Calibri is what OneNote's Word export writes too.
    QFont body(QStringLiteral("Calibri"));
    body.setPointSizeF(pageFont().pointSizeF());
    doc.setDefaultFont(body);
    Docx::WordParts parts; // US Letter, 1-inch margins
    parts.properties.title = page.title;
    parts.properties.creator = qEnvironmentVariable("USER");
    const qreal width = parts.layout.contentWidth();
    QTextCursor cursor(&doc);

    QTextBlockFormat title;
    title.setHeadingLevel(1);
    title.setProperty(Docx::kWriteStyleIdProperty, 1); // StyleCatalog::Title
    title.setBottomMargin(8);
    cursor.setBlockFormat(title);
    QTextCharFormat titleChars;
    titleChars.setFontPointSize(20);
    titleChars.setFontWeight(QFont::Bold);
    cursor.insertText(page.title.isEmpty() ? QObject::tr("Untitled page") : page.title, titleChars);

    QList<PageBlock> blocks = layoutBlocks(page.envelope.blocksJson, pageWidth);
    std::stable_sort(blocks.begin(), blocks.end(), [](const PageBlock &a, const PageBlock &b) {
        return qAbs(a.rect.top() - b.rect.top()) > 4 ? a.rect.top() < b.rect.top() : a.rect.left() < b.rect.left();
    });
    const QTextCharFormat plain; // the document's default font
    for (const PageBlock &block : std::as_const(blocks)) {
        cursor.insertBlock(QTextBlockFormat(), plain);
        switch (block.kind) {
        case PageBlock::Kind::Markdown:
            RichExport::insertWordTagIcons(cursor, block.tags, doc, parts);
            insertMarkdown(cursor, block.text, width);
            break;
        case PageBlock::Kind::Text:
            if (block.rich.isEmpty() && block.tags.isEmpty()) {
                cursor.insertText(block.text, plain);
                break;
            }
            // M84 (D-093): formatted text as Word runs, styles, lists and
            // tables; its tags as boxes and icons (note_rich_export.h).
            RichExport::insertWord(cursor,
                                   QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                               {QStringLiteral("text"), block.text},
                                               {QStringLiteral("rich"), block.rich},
                                               {QStringLiteral("tags"), block.tags}},
                                   doc, parts, width);
            break;
        case PageBlock::Kind::Check: {
            QTextBlockFormat item;
            item.setLeftMargin(block.level * 24.0);
            cursor.setBlockFormat(item);
            cursor.insertText(block.checked ? QStringLiteral("☑ ") : QStringLiteral("☐ "), plain);
            RichExport::insertWordTagIcons(cursor, block.tags, doc, parts);
            cursor.insertText(block.text, plain);
            break;
        }
        case PageBlock::Kind::Image:
            RichExport::insertWordTagIcons(cursor, block.tags, doc, parts);
            insertPicture(cursor, parts, doc, block.imageData, width);
            break;
        }
    }

    // The handwriting, as one picture of the ink at twice the page scale.
    const QRectF inkBounds = page.envelope.ink.bounds();
    if (!page.envelope.ink.strokes().isEmpty() && inkBounds.isValid()) {
        const QRectF area = inkBounds.adjusted(-8, -8, 8, 8);
        QImage image((area.size() * 2).toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white); // paper (D-004)
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(2, 2);
        painter.translate(-area.topLeft());
        Ink::renderStrokes(&painter, page.envelope.ink.strokes());
        painter.end();
        QTextCharFormat caption;
        caption.setFontItalic(true);
        caption.setForeground(QColor(0x59, 0x59, 0x59));
        cursor.insertBlock(QTextBlockFormat(), plain);
        cursor.insertText(QObject::tr("Handwriting"), caption);
        cursor.insertBlock(QTextBlockFormat(), plain);
        insertPicture(cursor, parts, doc, pngOf(image), qMin(width, area.width()));
    }
    Docx::WriteOptions options;
    options.application = QStringLiteral("QindaNote");
    return Docx::writeDocx(path, doc, parts, options, error);
}

} // namespace QindaOffice::Apps::Notes
