// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_math.h"
#include "docx_text.h"

#include "docx_format.h"
#include "docx_properties.h"
#include "docx_sections.h"
#include "docx_table_format.h"

#include <QAbstractTextDocumentLayout>
#include <QImage>
#include <QPainter>
#include <QTextFrame>
#include <QTextLayout>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextTable>
#include <QUrl>

namespace QindaOffice::Docx {

namespace {

QString extensionFor(const QByteArray &bytes)
{
    if (bytes.startsWith("\x89PNG")) {
        return QStringLiteral("png");
    }
    if (bytes.startsWith("\xFF\xD8")) {
        return QStringLiteral("jpeg");
    }
    if (bytes.startsWith("GIF8")) {
        return QStringLiteral("gif");
    }
    if (bytes.startsWith("BM")) {
        return QStringLiteral("bmp");
    }
    return QStringLiteral("png");
}

QString mimeFor(const QString &extension)
{
    return extension == u"jpeg" ? QStringLiteral("image/jpeg") : QStringLiteral("image/") + extension;
}

// Copies image resources a clone needs: QTextDocument::clone() copies
// formats but not resources added with addResource().
void copyResources(const QTextDocument &from, QTextDocument *to)
{
    for (QTextBlock b = from.begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat f = it.fragment().charFormat();
            if (f.isImageFormat()) {
                const QUrl url(f.toImageFormat().name());
                const QVariant resource = from.resource(QTextDocument::ImageResource, url);
                if (resource.isValid()) {
                    to->addResource(QTextDocument::ImageResource, url, resource);
                }
            }
        }
    }
}

} // namespace

void setBlockCharFormatKeepingCell(QTextCursor &cursor, const QTextCharFormat &format)
{
    QTextTable *table = cursor.currentTable();
    const QTextTableCell cell = table != nullptr ? table->cellAt(cursor) : QTextTableCell();
    if (!cell.isValid() || cell.firstCursorPosition().block() != cursor.block()) {
        cursor.setBlockCharFormat(format);
        return;
    }
    // AGENT-GUARD: the first block of a table cell has no separator of its
    // own — its "block char format" IS the cell's format, which holds the
    // cell's spans, shading, borders and padding. setBlockCharFormat() there
    // silently resets every one of them (probed on Qt 6.11: a 3-column merge
    // became span 1 while the grid kept the merge). Merge the character
    // properties into the cell format instead — never the background or the
    // vertical alignment, which the cell format shares with its characters.
    QTextCharFormat merged = cell.format();
    QTextCharFormat chars = format;
    chars.clearBackground();
    chars.clearProperty(QTextFormat::TextVerticalAlignment);
    merged.merge(chars);
    QTextTableCell(cell).setFormat(merged);
}

QTextCharFormat withoutCellProperties(QTextCharFormat format, const QTextCharFormat &cell)
{
    if (format.objectType() != QTextFormat::TableCellObject && !format.hasProperty(QTextFormat::TableCellRowSpan)) {
        return format;
    }
    if (format.hasProperty(QTextFormat::BackgroundBrush) && format.background() == cell.background()) {
        format.clearBackground();
    }
    const int alignment = format.intProperty(QTextFormat::TextVerticalAlignment);
    if (alignment != QTextCharFormat::AlignSuperScript && alignment != QTextCharFormat::AlignSubScript) {
        format.clearProperty(QTextFormat::TextVerticalAlignment);
    }
    for (int p = QTextFormat::TableCellRowSpan; p <= QTextFormat::TableCellRightBorderBrush; ++p) {
        format.clearProperty(p);
    }
    for (const int p : {kCellXml, kRowXml, kCellSnapshot, kRowHeight, kCellBorders, kCellMargins, kRowHeightRule,
                        kRowCantSplit, kRowPad, kRowPadTop}) {
        format.clearProperty(p);
    }
    format.clearProperty(QTextFormat::ObjectType);
    return format;
}

QString addPicture(QTextDocument *document, QHash<QString, QByteArray> *media, const QByteArray &bytes)
{
    const QImage image = QImage::fromData(bytes);
    if (image.isNull()) {
        return {};
    }
    const QString extension = extensionFor(bytes);
    QString name;
    for (int n = int(media->size()) + 1;; ++n) {
        name = QStringLiteral("%1:image%2.%3").arg(QString::fromUtf16(kMediaScheme)).arg(n).arg(extension);
        if (!media->contains(name)) {
            break;
        }
    }
    media->insert(name, bytes);
    document->addResource(QTextDocument::ImageResource, QUrl(name), image);
    return name;
}

QTextCharFormat insertPageField(QTextCursor &cursor, bool total)
{
    QTextCharFormat plain = cursor.charFormat();
    plain.clearProperty(kFieldInstruction);
    QTextCharFormat field = plain;
    field.setProperty(kFieldInstruction, total ? QStringLiteral("NUMPAGES") : QStringLiteral("PAGE"));
    cursor.insertText(QStringLiteral("1"), field);
    cursor.setCharFormat(plain);
    return plain;
}

int removeSyntheticBreaks(QTextDocument *document)
{
    int removed = 0;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        if (!format.hasProperty(kSyntheticBreak)) {
            continue;
        }
        format.setPageBreakPolicy(ownPageBreaks(format));
        format.clearProperty(kSyntheticBreak);
        QTextCursor(block).setBlockFormat(format);
        ++removed;
    }
    // A table moved whole (its leading rows keep with the next).
    for (QTextFrame *frame : document->rootFrame()->childFrames()) {
        QTextFrameFormat format = frame->frameFormat();
        if (format.hasProperty(kSyntheticBreak)) {
            format.setPageBreakPolicy(QTextFormat::PageBreakFlags(format.intProperty(kSyntheticBreak)));
            format.clearProperty(kSyntheticBreak);
            frame->setFrameFormat(format);
            ++removed;
        }
    }
    return removed;
}

QString htmlWithEmbeddedPictures(const QTextDocument &document, const QHash<QString, QByteArray> &media)
{
    // Display-only page breaks (write_pagination) are not the author's: an
    // exported page must not carry them as page-break-before.
    // Nor are a section's display-only pads (M88): its paragraphs' own
    // right indents go out.
    bool synthetic = false;
    for (QTextBlock block = document.begin(); block.isValid() && !synthetic; block = block.next()) {
        synthetic = block.blockFormat().hasProperty(kSyntheticBreak) || block.blockFormat().hasProperty(kSectionPad);
    }
    for (const QTextFrame *frame : document.rootFrame()->childFrames()) {
        synthetic = synthetic || frame->frameFormat().hasProperty(kSyntheticBreak)
            || frame->frameFormat().hasProperty(kSectionPad);
    }
    std::unique_ptr<QTextDocument> clean;
    if (synthetic) {
        clean.reset(document.clone());
        removeSyntheticBreaks(clean.get());
        for (QTextBlock block = clean->begin(); block.isValid(); block = block.next()) {
            if (QTextBlockFormat format = block.blockFormat(); format.hasProperty(kSectionPad)) {
                setSectionPad(format, 0);
                QTextCursor(block).setBlockFormat(format);
            }
        }
        for (QTextFrame *frame : clean->rootFrame()->childFrames()) {
            if (QTextFrameFormat format = frame->frameFormat(); format.hasProperty(kSectionPad)) {
                setSectionPad(format, 0);
                frame->setFrameFormat(format);
            }
        }
    }
    QString html = (clean ? *clean : document).toHtml();
    for (auto it = media.cbegin(); it != media.cend(); ++it) {
        const QString extension = it.key().section(QLatin1Char('.'), -1);
        const QString uri = QStringLiteral("data:%1;base64,%2").arg(mimeFor(extension), QString::fromLatin1(it.value().toBase64()));
        html.replace(QStringLiteral("src=\"%1\"").arg(it.key()), QStringLiteral("src=\"%1\"").arg(uri));
    }
    return html;
}

std::unique_ptr<QTextDocument> headerFooterFromPattern(const QString &pattern, const QFont &font)
{
    auto document = std::make_unique<QTextDocument>();
    document->setDefaultFont(font);
    QTextCursor cursor(document.get());
    QTextBlockFormat centred;
    centred.setAlignment(Qt::AlignHCenter);
    cursor.setBlockFormat(centred);
    QTextCharFormat plain;
    plain.setFontPointSize(font.pointSizeF() > 0 ? font.pointSizeF() : 9.0);
    static const QRegularExpression kToken(QStringLiteral("\\{(page|pages)\\}"));
    qsizetype last = 0;
    auto it = kToken.globalMatch(pattern);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        cursor.insertText(pattern.mid(last, match.capturedStart() - last), plain);
        QTextCharFormat field = plain;
        field.setProperty(kFieldInstruction, match.captured(1) == u"page" ? QStringLiteral("PAGE")
                                                                          : QStringLiteral("NUMPAGES"));
        cursor.insertText(QStringLiteral("1"), field);
        last = match.capturedEnd();
    }
    cursor.insertText(pattern.mid(last), plain);
    document->setModified(true);
    return document;
}

QString patternFromHeaderFooter(const QTextDocument &document, bool *simple)
{
    QString out;
    bool plain = document.blockCount() <= 1;
    QTextCharFormat first;
    bool haveFirst = false;
    for (QTextBlock b = document.begin(); b.isValid(); b = b.next()) {
        if (b.textList() != nullptr) {
            plain = false;
        }
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QTextCharFormat f = fragment.charFormat();
            if (f.isImageFormat() || f.hasProperty(kInlineXml)) {
                plain = false;
                continue;
            }
            const QString field = f.stringProperty(kFieldInstruction).trimmed().section(QLatin1Char(' '), 0, 0).toUpper();
            if (field == u"PAGE") {
                out += QStringLiteral("{page}");
            } else if (field == u"NUMPAGES" || field == u"SECTIONPAGES") {
                out += QStringLiteral("{pages}");
            } else {
                out += fragment.text();
                if (!field.isEmpty()) {
                    plain = false;
                }
            }
            if (!haveFirst) {
                first = f;
                haveFirst = true;
            } else if (f.fontWeight() != first.fontWeight() || f.fontItalic() != first.fontItalic()
                       || f.foreground() != first.foreground()) {
                plain = false;
            }
        }
        if (b.next().isValid()) {
            out += QLatin1Char(' ');
        }
    }
    if (document.rootFrame()->childFrames().size() > 0) {
        plain = false; // tables
    }
    if (simple != nullptr) {
        *simple = plain;
    }
    return out.replace(QChar::LineSeparator, QLatin1Char(' ')).trimmed();
}

std::unique_ptr<QTextDocument> instantiateHeaderFooter(const QTextDocument &document, int page, int pageCount)
{
    return instantiateHeaderFooter(document, PageFieldValues{page, QString(), pageCount, pageCount});
}

std::unique_ptr<QTextDocument> instantiateHeaderFooter(const QTextDocument &document, const PageFieldValues &values)
{
    std::unique_ptr<QTextDocument> copy(document.clone());
    installMathLayout(copy.get());
    copyResources(document, copy.get());
    struct Field {
        int position;
        int length;
        QString text;
        QTextCharFormat format;
    };
    QList<Field> fields;
    for (QTextBlock b = copy->begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QString instruction = fragment.charFormat().stringProperty(kFieldInstruction).trimmed();
            const QString field = instruction.section(QLatin1Char(' '), 0, 0).toUpper();
            QString text;
            if (field == u"PAGE") {
                const QString own = fieldNumberFormat(instruction);
                text = formatPageNumber(values.page, own.isEmpty() ? values.pageFormat : own);
            } else if (field == u"NUMPAGES") {
                text = formatPageNumber(values.pages, fieldNumberFormat(instruction));
            } else if (field == u"SECTIONPAGES") {
                text = formatPageNumber(values.sectionPages, fieldNumberFormat(instruction));
            } else {
                continue;
            }
            fields.append({fragment.position(), fragment.length(), text, fragment.charFormat()});
        }
    }
    // From the end, so earlier positions stay valid.
    for (auto it = fields.crbegin(); it != fields.crend(); ++it) {
        QTextCursor cursor(copy.get());
        cursor.setPosition(it->position);
        cursor.setPosition(it->position + it->length, QTextCursor::KeepAnchor);
        cursor.insertText(it->text, it->format);
    }
    return copy;
}

namespace {

struct Side {
    qreal width = 0;
    QString style;
    QColor color;
    qreal space = 0;
};

QHash<QString, Side> parseBorders(const QString &spec)
{
    QHash<QString, Side> out;
    for (const QString &entry : spec.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QStringList f = entry.split(QLatin1Char(':'));
        if (f.size() == 5) {
            out.insert(f[0], {f[1].toDouble(), f[2], QColor(f[3]), f[4].toDouble()});
        }
    }
    return out;
}

void line(QPainter *painter, const Side &side, QPointF a, QPointF b)
{
    QPen pen(side.color, side.width);
    pen.setCapStyle(Qt::FlatCap);
    pen.setStyle(side.style == u"dot" ? Qt::DotLine : side.style == u"dash" ? Qt::DashLine : Qt::SolidLine);
    painter->setPen(pen);
    painter->drawLine(a, b);
    if (side.style == u"double") {
        const bool horizontal = qFuzzyCompare(a.y(), b.y());
        const QPointF shift = horizontal ? QPointF(0, side.width * 2) : QPointF(side.width * 2, 0);
        painter->drawLine(a + shift, b + shift);
    }
}

bool topLevel(const QTextDocument &document, const QTextBlock &block)
{
    return QTextCursor(block).currentFrame() == document.rootFrame();
}

} // namespace

void drawParagraphBorders(QPainter *painter, const QTextDocument &document, const QRectF &clip)
{
    QAbstractTextDocumentLayout *layout = document.documentLayout();
    const QTextFrameFormat root = document.rootFrame()->frameFormat();
    const qreal pageWidth = document.pageSize().width();
    const int start = layout->hitTest(clip.topLeft(), Qt::FuzzyHit);
    QTextBlock block = document.findBlock(qMax(0, start));
    if (block.previous().isValid()) {
        block = block.previous();
    }
    painter->save();
    for (; block.isValid(); block = block.next()) {
        const QRectF rect = layout->blockBoundingRect(block);
        if (rect.top() > clip.bottom() + 8) {
            break;
        }
        const QString spec = block.blockFormat().stringProperty(kParagraphBorders);
        if (spec.isEmpty() || !topLevel(document, block)) {
            continue;
        }
        const QTextBlockFormat f = block.blockFormat();
        const auto same = [&](const QTextBlock &other) {
            return other.isValid() && topLevel(document, other) && other.blockFormat().stringProperty(kParagraphBorders) == spec
                && other.blockFormat().leftMargin() == f.leftMargin() && other.blockFormat().rightMargin() == f.rightMargin();
        };
        const QHash<QString, Side> sides = parseBorders(spec);
        const bool first = !same(block.previous());
        const bool last = !same(block.next());
        const qreal x0 = root.leftMargin() + f.leftMargin() + qMin<qreal>(0, f.textIndent()) - sides.value(QStringLiteral("left")).space;
        const qreal x1 = pageWidth - root.rightMargin() - f.rightMargin() + sides.value(QStringLiteral("right")).space;
        const qreal y0 = rect.top() - (first ? sides.value(QStringLiteral("top")).space : 0);
        const qreal y1 = rect.bottom() + (last ? sides.value(QStringLiteral("bottom")).space : 0);
        if (first && sides.contains(QStringLiteral("top"))) {
            line(painter, sides.value(QStringLiteral("top")), {x0, y0}, {x1, y0});
        }
        if (last && sides.contains(QStringLiteral("bottom"))) {
            line(painter, sides.value(QStringLiteral("bottom")), {x0, y1}, {x1, y1});
        } else if (!last && sides.contains(QStringLiteral("between"))) {
            line(painter, sides.value(QStringLiteral("between")), {x0, y1}, {x1, y1});
        }
        // Side rules run on through the gap to the next paragraph of the box.
        const qreal bottom = last ? y1 : layout->blockBoundingRect(block.next()).top();
        if (sides.contains(QStringLiteral("left"))) {
            line(painter, sides.value(QStringLiteral("left")), {x0, y0}, {x0, bottom});
        }
        if (sides.contains(QStringLiteral("right"))) {
            line(painter, sides.value(QStringLiteral("right")), {x1, y0}, {x1, bottom});
        }
    }
    painter->restore();
}

void applyRowHeights(QTextDocument *document)
{
    refreshRowHeights(document); // docx_table_format.h
}

qreal headerFooterHeight(const QTextDocument &story, qreal width)
{
    if (story.isEmpty()) {
        return 0;
    }
    const std::unique_ptr<QTextDocument> shown = instantiateHeaderFooter(story, 1, 1);
    shown->setDocumentMargin(0);
    shown->setTextWidth(width);
    return shown->size().height();
}

} // namespace QindaOffice::Docx
