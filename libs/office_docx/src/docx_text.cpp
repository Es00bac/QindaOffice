// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_text.h"

#include "docx_properties.h"

#include <QImage>
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

QString htmlWithEmbeddedPictures(const QTextDocument &document, const QHash<QString, QByteArray> &media)
{
    QString html = document.toHtml();
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
    std::unique_ptr<QTextDocument> copy(document.clone());
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
            const QString field = fragment.charFormat().stringProperty(kFieldInstruction).trimmed().section(QLatin1Char(' '), 0, 0).toUpper();
            if (field == u"PAGE") {
                fields.append({fragment.position(), fragment.length(), QString::number(page), fragment.charFormat()});
            } else if (field == u"NUMPAGES" || field == u"SECTIONPAGES") {
                fields.append({fragment.position(), fragment.length(), QString::number(pageCount), fragment.charFormat()});
            }
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

} // namespace QindaOffice::Docx
