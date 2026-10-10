// SPDX-License-Identifier: GPL-3.0-or-later
// RichExport's Word half: a text block's QindaTK document written as
// QTextDocument content that office_docx maps to WordprocessingML (D-068):
// QTextCharFormat runs, kWriteStyleIdProperty styles, QTextList numbering,
// QTextTable tables, qwmedia: pictures.

#include "note_rich_export.h"

#include "note_rich_text.h"
#include "note_tags.h"

#include "docx_model.h"
#include "docx_properties.h"
#include "docx_text.h"

#include <qindatk/rich_text_document.h>

#include <QBuffer>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>

namespace QindaOffice::Apps::Notes::RichExport {

using namespace QindaTK::RichText;

namespace {

// Word's hyperlink blue, for a link the note gave no colour.
const QColor kLinkBlue(0x05, 0x63, 0xc1);

QString wordFamily(const QString &family)
{
    // The Code style's generic family (note_rich_style.cpp) has no Word name.
    return family == QLatin1String("monospace") ? QStringLiteral("Consolas") : family;
}

QTextCharFormat runFormat(const QindaTK::RichTextDocument &doc, const CharFormat &direct, const ParagraphFormat &pf)
{
    const CharFormat f = effectiveCharFormat(doc.styleSheet(), direct, pf);
    QTextCharFormat out;
    if (!f.family.isEmpty()) {
        out.setFontFamilies(QStringList{wordFamily(f.family)});
    }
    if (f.sizePt > 0) {
        out.setFontPointSize(f.sizePt);
    }
    if (f.weight > 0) {
        out.setFontWeight(f.weight);
    }
    if (f.italic == 1) {
        out.setFontItalic(true);
    }
    if (f.underline == 1) {
        out.setFontUnderline(true);
    }
    if (f.strike == 1) {
        out.setFontStrikeOut(true);
    }
    if (f.color.isValid()) {
        out.setForeground(f.color);
    }
    if (f.background.isValid()) {
        out.setBackground(f.background);
    }
    if (f.verticalAlign == VerticalAlign::Superscript) {
        out.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    } else if (f.verticalAlign == VerticalAlign::Subscript) {
        out.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    }
    if (!f.href.isEmpty()) {
        out.setAnchor(true);
        out.setAnchorHref(f.href);
        if (!f.color.isValid()) {
            out.setForeground(kLinkBlue);
        }
        if (f.underline < 0) {
            out.setFontUnderline(true);
        }
    }
    return out;
}

// StyleCatalog ids (docx_restyle.h): Normal 0, Title 1, Heading 1–6 2–7,
// Quote 8, Code 9 — matched by the note style NAMES (note_rich_style.h).
int wordStyleId(const QindaTK::RichTextDocument &doc, const Paragraph &p)
{
    if (const NamedStyle *named = doc.style(p.format.styleId)) {
        if (named->name.compare(QLatin1String("Title"), Qt::CaseInsensitive) == 0) {
            return 1;
        }
        if (named->name.compare(QLatin1String("Quote"), Qt::CaseInsensitive) == 0) {
            return 8;
        }
        if (named->name.compare(QLatin1String("Code"), Qt::CaseInsensitive) == 0) {
            return 9;
        }
    }
    return p.format.headingLevel > 0 ? 1 + qMin(p.format.headingLevel, 6) : -1;
}

QTextBlockFormat blockFormat(const QindaTK::RichTextDocument &doc, const Paragraph &p)
{
    QTextBlockFormat bf;
    if (p.format.headingLevel > 0) {
        bf.setHeadingLevel(qMin(p.format.headingLevel, 6));
    }
    if (const int style = wordStyleId(doc, p); style >= 0) {
        bf.setProperty(Docx::kWriteStyleIdProperty, style);
    }
    switch (p.format.alignment) {
    case Alignment::Center:
        bf.setAlignment(Qt::AlignHCenter);
        break;
    case Alignment::Right:
    case Alignment::End:
        bf.setAlignment(Qt::AlignRight);
        break;
    case Alignment::Justify:
        bf.setAlignment(Qt::AlignJustify);
        break;
    default:
        break;
    }
    if (p.format.listId.isEmpty() && p.format.indentStartPt > 0) {
        bf.setLeftMargin(p.format.indentStartPt * PxPerPt);
    }
    if (p.format.spaceBeforePt > 0) {
        bf.setTopMargin(p.format.spaceBeforePt * PxPerPt);
    }
    if (p.format.spaceAfterPt > 0) {
        bf.setBottomMargin(p.format.spaceAfterPt * PxPerPt);
    }
    return bf;
}

QTextListFormat::Style listStyle(ListKind kind)
{
    switch (kind) {
    case ListKind::Circle:
        return QTextListFormat::ListCircle;
    case ListKind::Square:
        return QTextListFormat::ListSquare;
    case ListKind::Decimal:
        return QTextListFormat::ListDecimal;
    case ListKind::LowerAlpha:
        return QTextListFormat::ListLowerAlpha;
    case ListKind::UpperAlpha:
        return QTextListFormat::ListUpperAlpha;
    case ListKind::LowerRoman:
        return QTextListFormat::ListLowerRoman;
    case ListKind::UpperRoman:
        return QTextListFormat::ListUpperRoman;
    default:
        return QTextListFormat::ListDisc;
    }
}

// A tag's icon as a small picture, one media entry per tag look.
void insertIcon(QTextCursor &cursor, QTextDocument &doc, Docx::WordParts &parts, const Tags::TagDef &def)
{
    QString key = def.id;
    key.replace(QLatin1Char(':'), QLatin1Char('-'));
    const QString name = QStringLiteral("%1:tag-%2.png").arg(QString::fromUtf16(Docx::kMediaScheme), key);
    if (!parts.media.contains(name)) {
        QImage image(48, 48, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        Tags::paintIcon(&painter, QRectF(3, 3, 42, 42), def, false);
        painter.end();
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        parts.media.insert(name, png);
        doc.addResource(QTextDocument::ImageResource, QUrl(name), image);
    }
    QTextImageFormat format;
    format.setName(name);
    format.setWidth(14);
    format.setHeight(14);
    cursor.insertImage(format);
    cursor.insertText(QStringLiteral(" "), QTextCharFormat());
}

struct Writer {
    QTextCursor &cursor;
    QTextDocument &doc;
    Docx::WordParts &parts;
    const QindaTK::RichTextDocument &rich;
    QJsonArray tags;
    qreal maxWidth;
    bool fresh = true; // the cursor's paragraph is empty and still unused
    QHash<QString, QTextList *> lists;

    void startParagraph(const QTextBlockFormat &format)
    {
        if (fresh) {
            cursor.setBlockFormat(format);
            fresh = false;
        } else {
            cursor.insertBlock(format, QTextCharFormat());
        }
    }

    // To Do as a box, other icon tags as pictures; highlight tags colour
    // the runs (returned).
    QColor tagsFor(const QString &paragraph)
    {
        QColor band;
        for (const QJsonValue &value : std::as_const(tags)) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("p")).toString() != paragraph) {
                continue;
            }
            const Tags::TagDef def = Tags::fromEntry(entry);
            if (!def.isValid()) {
                continue;
            }
            if (def.highlight) {
                band = def.color;
            } else if (def.todo) {
                cursor.insertText(entry.value(QStringLiteral("done")).toBool() ? QStringLiteral("☑ ")
                                                                                : QStringLiteral("☐ "),
                                  QTextCharFormat());
            } else {
                insertIcon(cursor, doc, parts, def);
            }
        }
        return band;
    }

    void picture(const RichObject &object)
    {
        const QString source = object.source;
        if (!source.startsWith(QLatin1String("data:image/"))) {
            return;
        }
        const QByteArray bytes = QByteArray::fromBase64(source.mid(source.indexOf(QLatin1Char(',')) + 1).toLatin1());
        const QString name = Docx::addPicture(&doc, &parts.media, bytes);
        if (name.isEmpty()) {
            return;
        }
        QSizeF size(object.widthPt * PxPerPt, object.heightPt * PxPerPt);
        if (size.width() > maxWidth) {
            size = QSizeF(maxWidth, size.height() * maxWidth / size.width());
        }
        QTextImageFormat format;
        format.setName(name);
        format.setWidth(size.width());
        format.setHeight(size.height());
        cursor.insertImage(format);
    }

    void paragraph(const Paragraph &p, bool inCell)
    {
        startParagraph(blockFormat(rich, p));
        if (!inCell && !p.format.listId.isEmpty()) {
            const QString key = p.format.listId + QLatin1Char('/') + QString::number(p.format.listLevel);
            if (QTextList *list = lists.value(key)) {
                list->add(cursor.block());
            } else {
                QTextListFormat format;
                const ListDefinition *definition = rich.list(p.format.listId);
                const ListKind kind = definition != nullptr && p.format.listLevel < definition->levels.size()
                    ? definition->levels.at(p.format.listLevel).kind
                    : ListKind::Disc;
                format.setStyle(listStyle(kind));
                format.setIndent(p.format.listLevel + 1);
                lists.insert(key, cursor.createList(format));
            }
        }
        const QColor band = tagsFor(p.id.toString(QUuid::WithoutBraces));
        int start = 0;
        for (const Span &span : p.spans) {
            QTextCharFormat format = runFormat(rich, rich.charFormat(span.format), p.format);
            if (band.isValid() && !format.hasProperty(QTextFormat::BackgroundBrush)) {
                format.setBackground(band);
            }
            QString text;
            for (const QChar c : p.text.mid(start, span.length)) {
                if (c == QChar(ObjectChar)) {
                    cursor.insertText(text, format);
                    text.clear();
                    if (const RichObject *object = rich.object(rich.charFormat(span.format).objectId)) {
                        picture(*object);
                    }
                } else {
                    text += c == QChar(LineBreakChar) ? QChar(QChar::LineSeparator) : c;
                }
            }
            cursor.insertText(text, format);
            start += span.length;
        }
    }

    void table(const Table &t)
    {
        const int rows = int(t.rows.size());
        const int columns = int(t.columnWeights.size());
        if (rows == 0 || columns == 0) {
            return;
        }
        QTextTableFormat format;
        format.setBorder(t.borderPt * PxPerPt);
        format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        format.setCellPadding(t.cellPaddingPt * PxPerPt);
        format.setCellSpacing(0);
        format.setHeaderRowCount(t.headerRows);
        format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        double total = 0;
        for (const double weight : t.columnWeights) {
            total += weight;
        }
        QList<QTextLength> widths;
        for (const double weight : t.columnWeights) {
            widths.append(QTextLength(QTextLength::PercentageLength, 100.0 * weight / qMax(total, 1e-9)));
        }
        format.setColumnWidthConstraints(widths);
        if (!fresh) {
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        }
        fresh = false;
        QTextTable *table = cursor.insertTable(rows, columns, format);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < columns; ++c) {
                const TableCell &cell = t.rows.at(r).cells.at(c);
                if (!cell.paragraphs.isEmpty() && (cell.rowSpan > 1 || cell.columnSpan > 1)) {
                    table->mergeCells(r, c, cell.rowSpan, cell.columnSpan);
                }
            }
        }
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < columns; ++c) {
                const TableCell &cell = t.rows.at(r).cells.at(c);
                if (cell.paragraphs.isEmpty()) {
                    continue; // a slot a merged cell covers
                }
                QTextTableCell target = table->cellAt(r, c);
                if (cell.background.isValid()) {
                    QTextTableCellFormat cellFormat = target.format().toTableCellFormat();
                    cellFormat.setBackground(cell.background);
                    target.setFormat(cellFormat);
                }
                QTextCursor inCell = target.firstCursorPosition();
                Writer sub{inCell, doc, parts, rich, tags, maxWidth, true, {}};
                for (const ParagraphPtr &p : cell.paragraphs) {
                    sub.paragraph(*p, true);
                }
            }
        }
        // After the table: Qt keeps an empty paragraph there, ours to use.
        cursor.movePosition(QTextCursor::End);
        fresh = true;
    }
};

} // namespace

void insertWord(QTextCursor &cursor, const QJsonObject &block, QTextDocument &doc, Docx::WordParts &parts,
                qreal maxWidth)
{
    const std::unique_ptr<QindaTK::RichTextDocument> rich = RichBlock::forBlock(block);
    Writer writer{cursor, doc, parts, *rich, block.value(QStringLiteral("tags")).toArray(), maxWidth, true, {}};
    for (const Block &body : rich->blocks()) {
        if (body.paragraph) {
            writer.paragraph(*body.paragraph, false);
        } else {
            writer.table(*body.table);
        }
    }
}

void insertWordTagIcons(QTextCursor &cursor, const QJsonArray &tags, QTextDocument &doc, Docx::WordParts &parts)
{
    for (const QJsonValue &value : tags) {
        const QJsonObject entry = value.toObject();
        const Tags::TagDef def = Tags::fromEntry(entry);
        if (!def.isValid() || !entry.value(QStringLiteral("p")).toString().isEmpty()) {
            continue;
        }
        if (def.todo) {
            cursor.insertText(entry.value(QStringLiteral("done")).toBool() ? QStringLiteral("☑ ")
                                                                            : QStringLiteral("☐ "),
                              QTextCharFormat());
        } else if (!def.highlight) {
            insertIcon(cursor, doc, parts, def);
        }
    }
}

} // namespace QindaOffice::Apps::Notes::RichExport
