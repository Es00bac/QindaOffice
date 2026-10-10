// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_rich_import.h"

#include "note_blocks.h"
#include "note_markdown_style.h"
#include "note_rich_text.h"

#include <qindatk/rich_text_document.h>
#include <qindatk/rich_text_html.h>
#include <qindatk/rich_text_spans.h>

#include <QImage>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

#include <functional>

namespace QindaOffice::Apps::Notes::RichImport {

using namespace QindaTK::RichText;

namespace {

// Private-use sentinels that carry a Markdown task item's box through
// QTextDocument's HTML round trip (never left in a note).
constexpr char16_t kTaskOpen = 0xE010;
constexpr char16_t kTaskDone = 0xE011;

constexpr auto kCase = QRegularExpression::CaseInsensitiveOption;
constexpr auto kAll = QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption;

QString attribute(const QString &tag, const QString &name)
{
    const QRegularExpression re(QStringLiteral("\\b%1\\s*=\\s*(\"([^\"]*)\"|'([^']*)'|([^\\s>]+))").arg(name), kCase);
    const QRegularExpressionMatch m = re.match(tag);
    if (!m.hasMatch()) {
        return {};
    }
    for (int group : {2, 3, 4}) {
        if (m.capturedStart(group) >= 0) {
            return m.captured(group);
        }
    }
    return {};
}

// An inline (data:) picture with its size, scaled to fit; anything else
// becomes its alternative text.
QString pictureTag(const QString &tag, qreal maxWidth)
{
    const QString src = attribute(tag, QStringLiteral("src")).trimmed();
    const QString alt = attribute(tag, QStringLiteral("alt"));
    if (!src.startsWith(QLatin1String("data:image/"), Qt::CaseInsensitive)) {
        return alt.toHtmlEscaped();
    }
    const qsizetype comma = src.indexOf(QLatin1Char(','));
    const QImage image = QImage::fromData(QByteArray::fromBase64(src.mid(comma + 1).toLatin1()));
    if (comma < 0 || image.isNull()) {
        return alt.toHtmlEscaped();
    }
    QSizeF size(attribute(tag, QStringLiteral("width")).toDouble(), attribute(tag, QStringLiteral("height")).toDouble());
    if (size.width() <= 0 || size.height() <= 0) {
        size = image.size();
    }
    if (size.width() > maxWidth) {
        size = QSizeF(maxWidth, size.height() * maxWidth / size.width());
    }
    return QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%3\">")
        .arg(src)
        .arg(qRound(size.width()))
        .arg(qRound(size.height()));
}

// Rewrites every match of `re` in `html` through `rewrite`.
template <typename Rewrite>
QString rewritten(const QString &html, const QRegularExpression &re, Rewrite rewrite)
{
    QString out;
    qsizetype last = 0;
    for (auto it = re.globalMatch(html); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(html).mid(last, m.capturedStart() - last);
        out += rewrite(m);
        last = m.capturedEnd();
    }
    out += QStringView(html).mid(last);
    return out;
}

void forEachParagraph(Fragment &fragment, const std::function<void(Paragraph &)> &visit)
{
    for (Block &block : fragment.blocks) {
        if (block.paragraph) {
            visit(*block.paragraph);
            continue;
        }
        for (TableRow &row : block.table->rows) {
            for (TableCell &cell : row.cells) {
                for (const ParagraphPtr &p : cell.paragraphs) {
                    visit(*p);
                }
            }
        }
    }
}

// Task sentinels → To Do tags; unsafe link addresses dropped.
QJsonArray finish(Fragment &fragment)
{
    for (CharFormat &format : fragment.formats) {
        if (!format.href.isEmpty() && !safeHref(format.href)) {
            format.href.clear();
        }
    }
    QJsonArray tags;
    forEachParagraph(fragment, [&tags](Paragraph &p) {
        // Zero-width spaces only keep an empty HTML paragraph alive (the
        // templates' empty lines); they are not text.
        for (qsizetype i = p.text.size() - 1; i >= 0; --i) {
            if (p.text.at(i) == QChar(0x200B)) {
                p.text.remove(i, 1);
                p.spans = Spans::replaced(p.spans, int(i), 1, {});
            }
        }
        if (p.text.isEmpty() || (p.text.at(0) != QChar(kTaskOpen) && p.text.at(0) != QChar(kTaskDone))) {
            return;
        }
        const bool done = p.text.at(0) == QChar(kTaskDone);
        p.text.remove(0, 1);
        p.spans = Spans::replaced(p.spans, 0, 1, {});
        // The box replaces the bullet, as OneNote's To Do does.
        p.format.listId.clear();
        p.format.listLevel = 0;
        tags.append(QJsonObject{{QStringLiteral("p"), p.id.toString(QUuid::WithoutBraces)},
                                {QStringLiteral("tag"), QStringLiteral("todo")},
                                {QStringLiteral("done"), done}});
    });
    return tags;
}

} // namespace

bool safeHref(const QString &href)
{
    const QString trimmed = href.trimmed();
    if (trimmed.startsWith(QLatin1Char('#'))) {
        return true;
    }
    const QString scheme = QUrl(trimmed).scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("mailto")
        || scheme == QLatin1String("ftp") || scheme == QLatin1String("file");
}

QString sanitizeHtml(const QString &input, qreal maxPictureWidth)
{
    QString html = input;
    static const QRegularExpression comments(QStringLiteral("<!--.*?-->"), kAll);
    html.remove(comments);
    // Elements whose content must go with them.
    for (const char *tag : {"script", "style", "iframe", "object", "embed", "noscript", "template", "svg", "math",
                            "select", "textarea", "button", "applet", "frameset", "canvas", "video", "audio"}) {
        html.remove(QRegularExpression(QStringLiteral("<%1\\b[^>]*>.*?</%1\\s*>").arg(QLatin1String(tag)), kAll));
        html.remove(QRegularExpression(QStringLiteral("<%1\\b[^>]*>").arg(QLatin1String(tag)), kCase));
    }
    // Elements that only point elsewhere or take input.
    for (const char *tag : {"link", "meta", "base", "input", "form", "/form", "param", "source", "track", "frame"}) {
        html.remove(QRegularExpression(QStringLiteral("<%1\\b[^>]*>").arg(QLatin1String(tag)), kCase));
    }
    static const QRegularExpression handlers(QStringLiteral("\\s+on[a-z]+\\s*=\\s*(\"[^\"]*\"|'[^']*'|[^\\s>]+)"), kCase);
    html.remove(handlers);
    static const QRegularExpression urls(QStringLiteral("url\\s*\\([^)]*\\)"), kCase);
    html.replace(urls, QStringLiteral("none"));
    static const QRegularExpression hrefs(QStringLiteral("\\s+href\\s*=\\s*(\"([^\"]*)\"|'([^']*)'|([^\\s>]+))"), kCase);
    html = rewritten(html, hrefs, [](const QRegularExpressionMatch &m) {
        QString target;
        for (int group : {2, 3, 4}) {
            if (m.capturedStart(group) >= 0) {
                target = m.captured(group);
            }
        }
        return safeHref(target) ? m.captured(0) : QString();
    });
    static const QRegularExpression images(QStringLiteral("<img\\b[^>]*>"), kCase);
    return rewritten(html, images,
                     [maxPictureWidth](const QRegularExpressionMatch &m) { return pictureTag(m.captured(0), maxPictureWidth); });
}

Imported fromHtml(const QString &html, qreal maxPictureWidth)
{
    Fragment fragment = QindaTK::RichTextHtml::toFragment(sanitizeHtml(html, maxPictureWidth));
    Imported out;
    out.tags = finish(fragment);
    out.doc = RichBlock::fromFragment(std::move(fragment));
    return out;
}

Imported fromMarkdown(const QString &markdown, qreal maxPictureWidth)
{
    QTextDocument document;
    document.setDefaultFont(pageFont());
    QTextCursor cursor(&document);
    // The page's own Markdown reading (M57): callouts, ==highlights== and
    // the rest of the vocabulary become formats.
    MarkdownStyle::insertWithCallouts(cursor, MarkdownStyle::markInline(markdown));
    MarkdownStyle::applyInline(&document);
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat::MarkerType marker = block.blockFormat().marker();
        if (marker == QTextBlockFormat::MarkerType::Unchecked || marker == QTextBlockFormat::MarkerType::Checked) {
            QTextCursor(block).insertText(
                QString(QChar(marker == QTextBlockFormat::MarkerType::Checked ? kTaskDone : kTaskOpen)));
        }
    }
    return fromHtml(document.toHtml(), maxPictureWidth);
}

Imported fromTextDocument(const QTextDocument &document, const QHash<QString, QByteArray> &media,
                          qreal maxPictureWidth)
{
    // Pictures travel as data: URIs (the sanitiser keeps exactly those).
    static const QRegularExpression names(QStringLiteral("src=\"(qwmedia:[^\"]+)\""));
    const QString html = rewritten(document.toHtml(), names, [&media](const QRegularExpressionMatch &m) {
        const QString name = m.captured(1);
        const QByteArray bytes = media.value(name);
        const QString type = name.endsWith(QLatin1String(".jpg"), Qt::CaseInsensitive)
                || name.endsWith(QLatin1String(".jpeg"), Qt::CaseInsensitive)
            ? QStringLiteral("jpeg")
            : name.section(QLatin1Char('.'), -1).toLower();
        return bytes.isEmpty() ? QStringLiteral("src=\"\"")
                               : QStringLiteral("src=\"data:image/%1;base64,%2\"")
                                     .arg(type, QString::fromLatin1(bytes.toBase64()));
    });
    return fromHtml(html, maxPictureWidth);
}

QJsonObject textBlock(const Imported &imported)
{
    QJsonObject block{{QStringLiteral("type"), QStringLiteral("text")}};
    if (!imported.tags.isEmpty()) {
        block.insert(QStringLiteral("tags"), imported.tags);
    }
    if (imported.doc) {
        RichBlock::store(&block, *imported.doc);
    }
    return block;
}

} // namespace QindaOffice::Apps::Notes::RichImport
