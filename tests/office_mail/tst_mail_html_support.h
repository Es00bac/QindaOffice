// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Helpers for tst_mail_html (M64): sanitizing with options, cutting the body
// out of the skeleton, rendering through QTextDocument while recording every
// resource it asks for, re-tokenizing the output to check its structure.

#include "html_sanitizer.h"
#include "html_tokenizer.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QSet>
#include <QTextDocument>

namespace MailHtmlTest {

using namespace QindaOffice::Mail;
using namespace Qt::StringLiterals;

// Records every resource the rich-text engine asks for and returns nothing:
// through loadResource(), which QTextDocument asks first, and through a
// resource provider, which it asks when that returns nothing.
class RecordingDocument : public QTextDocument
{
public:
    RecordingDocument()
    {
        setResourceProvider([this](const QUrl &name) {
            requested << name.toString();
            return QVariant();
        });
    }

    QStringList requested;

protected:
    QVariant loadResource(int type, const QUrl &name) override
    {
        Q_UNUSED(type);
        requested << name.toString();
        return {};
    }
};

inline SanitizedHtml clean(const QString &html, bool allowRemote = false)
{
    HtmlSanitizeOptions options;
    options.allowRemoteImages = allowRemote;
    return sanitizeHtml(html, options);
}

inline QString bodyOf(const QString &html)
{
    const qsizetype open = html.indexOf(u'>', html.indexOf("<body"_L1));
    return html.mid(open + 1, html.lastIndexOf("</body>"_L1) - open - 1);
}

inline QString bodyOf(const SanitizedHtml &result)
{
    return bodyOf(result.html);
}

inline QString plainOf(const QString &html)
{
    QTextDocument document;
    document.setHtml(html);
    return document.toPlainText();
}

// Lays the HTML out and paints it, as the reading pane and print do.
inline void render(RecordingDocument &document, const QString &html)
{
    document.setHtml(html);
    document.setTextWidth(600);
    (void)document.documentLayout()->documentSize();
    QImage image(600, 400, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    document.drawContents(&painter);
}

inline QStringList nonLocalRequests(const QString &html)
{
    RecordingDocument document;
    render(document, html);
    QStringList bad;
    for (const QString &url : std::as_const(document.requested)) {
        if (!url.startsWith("cid:"_L1) && !url.startsWith("qindamail-inline:"_L1))
            bad << url;
    }
    return bad;
}

// Re-tokenizes the output: only the elements and attributes the sanitizer
// may write, safe link and image targets, balanced tags, no comments.
inline QString structureProblem(const QString &html, bool allowRemote)
{
    static const QSet<QString> elements = {
        u"html"_s, u"head"_s, u"body"_s, u"style"_s, u"div"_s, u"span"_s, u"p"_s, u"br"_s, u"hr"_s,
        u"h1"_s, u"h2"_s, u"h3"_s, u"h4"_s, u"h5"_s, u"h6"_s, u"b"_s, u"strong"_s, u"i"_s, u"em"_s,
        u"u"_s, u"s"_s, u"del"_s, u"sub"_s, u"sup"_s, u"small"_s, u"big"_s, u"font"_s, u"code"_s,
        u"kbd"_s, u"samp"_s, u"tt"_s, u"var"_s, u"pre"_s, u"blockquote"_s, u"cite"_s, u"address"_s,
        u"center"_s, u"dfn"_s, u"nobr"_s, u"a"_s, u"img"_s, u"ul"_s, u"ol"_s, u"li"_s, u"dl"_s,
        u"dt"_s, u"dd"_s, u"table"_s, u"tbody"_s, u"thead"_s, u"tfoot"_s, u"tr"_s, u"td"_s, u"th"_s};
    static const QSet<QString> attributes = {
        u"align"_s, u"alt"_s, u"bgcolor"_s, u"border"_s, u"cellpadding"_s, u"cellspacing"_s, u"class"_s,
        u"color"_s, u"colspan"_s, u"dir"_s, u"face"_s, u"height"_s, u"href"_s, u"hspace"_s, u"id"_s,
        u"lang"_s, u"name"_s, u"nowrap"_s, u"rowspan"_s, u"size"_s, u"src"_s, u"start"_s, u"style"_s,
        u"title"_s, u"type"_s, u"valign"_s, u"value"_s, u"vspace"_s, u"width"_s};
    static const QSet<QString> voids = {u"br"_s, u"hr"_s, u"img"_s};
    HtmlTokenizer tokenizer(html);
    HtmlToken token;
    QStringList open;
    while (tokenizer.next(token)) {
        switch (token.type) {
        case HtmlToken::Type::StartTag:
            if (!elements.contains(token.name))
                return u"element "_s + token.name;
            for (const HtmlAttribute &a : std::as_const(token.attributes)) {
                const QString v = a.value.toLower();
                if (!attributes.contains(a.name))
                    return u"attribute "_s + a.name;
                if (a.name == "href"_L1 && !v.startsWith("https://"_L1) && !v.startsWith("http://"_L1)
                    && !v.startsWith("mailto:"_L1) && !v.startsWith(u'#'))
                    return u"href "_s + a.value;
                const bool remote = v.startsWith("https://"_L1) || v.startsWith("http://"_L1);
                if (a.name == "src"_L1 && !v.startsWith("cid:"_L1)
                    && !v.startsWith("qindamail-inline:"_L1) && !(allowRemote && remote))
                    return u"src "_s + a.value;
                if (a.name == "style"_L1 && (v.contains("url("_L1) || v.contains("expression"_L1)))
                    return u"style "_s + a.value;
            }
            if (!voids.contains(token.name))
                open << token.name;
            break;
        case HtmlToken::Type::EndTag:
            if (open.isEmpty() || open.takeLast() != token.name)
                return u"unbalanced </"_s + token.name + u'>';
            break;
        case HtmlToken::Type::RawText:
            if (token.name != "style"_L1 || token.text.contains("url("_L1) || token.text.contains(u'@'))
                return u"raw "_s + token.name;
            break;
        case HtmlToken::Type::Text:
            break;
        default:
            return u"markup "_s + token.text;
        }
    }
    return open.isEmpty() ? QString() : u"unclosed "_s + open.join(u' ');
}

inline QString tokenDump(const QString &html)
{
    HtmlTokenizer tokenizer(html);
    HtmlToken t;
    QString out;
    while (tokenizer.next(t)) {
        switch (t.type) {
        case HtmlToken::Type::Text: out += u"T("_s + t.text + u')'; break;
        case HtmlToken::Type::StartTag: {
            out += u"S("_s + t.name;
            for (const HtmlAttribute &a : std::as_const(t.attributes))
                out += u' ' + a.name + u"=["_s + a.value + u']';
            out += t.selfClosing ? u"/)"_s : u")"_s;
            break;
        }
        case HtmlToken::Type::EndTag: out += u"E("_s + t.name + u')'; break;
        case HtmlToken::Type::RawText: out += u"R("_s + t.text + u')'; break;
        case HtmlToken::Type::Comment: out += u"C("_s + t.text + u')'; break;
        case HtmlToken::Type::Doctype: out += u"D("_s + t.text + u')'; break;
        case HtmlToken::Type::Cdata: out += u"X("_s + t.text + u')'; break;
        case HtmlToken::Type::ProcessingInstruction: out += u"P("_s + t.text + u')'; break;
        }
    }
    return out;
}

inline QByteArray pngBytes(QColor color, int width = 4, int height = 3)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

inline QString dataUrl(const QByteArray &bytes, const char *type = "image/png")
{
    return u"data:"_s + QLatin1StringView(type) + u";base64,"_s + QString::fromLatin1(bytes.toBase64());
}

} // namespace MailHtmlTest
