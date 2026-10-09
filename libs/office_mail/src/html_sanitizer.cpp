// SPDX-License-Identifier: GPL-3.0-or-later
// The HTML mail sanitizer (html_sanitizer.h): the token-driven state machine.
// Element, attribute and value tables are in html_sanitizer_tags.cpp, URL
// checks in html_sanitizer_urls.cpp, CSS in html_sanitizer_css.cpp, hidden
// and dropped content in html_sanitizer_hidden.cpp, the open-element stack
// and table repair in html_sanitizer_tree.cpp.

#include "html_sanitizer_p.h"

#include <utility>

using namespace Qt::StringLiterals;

namespace QindaOffice::Mail {

SanitizedHtml sanitizeHtml(const QString &html, const HtmlSanitizeOptions &options)
{
    SanitizedHtml result;
    QStringView input(html);
    if (input.size() > kMaxHtmlLength) {
        qsizetype cut = kMaxHtmlLength;
        if (input[cut - 1].isHighSurrogate())
            --cut;
        input = input.first(cut);
        result.truncated = true;
    }
    Detail::Sanitizer(options, result).run(input);
    return result;
}

namespace Detail {

namespace {

const QString *attributeValue(const HtmlToken &token, QLatin1StringView name)
{
    for (const HtmlAttribute &attribute : token.attributes) {
        if (attribute.name == name)
            return &attribute.value;
    }
    return nullptr;
}

bool remember(QSet<QString> &seen, QStringList &list, const QString &value, int limit)
{
    if (seen.contains(value))
        return true;
    if (list.size() >= limit)
        return false;
    seen.insert(value);
    list.append(value);
    return true;
}

bool isLink(UrlKind kind)
{
    return kind == UrlKind::Http || kind == UrlKind::Https || kind == UrlKind::Mailto
           || kind == UrlKind::Fragment;
}

bool isRemote(UrlKind kind)
{
    return kind == UrlKind::Http || kind == UrlKind::Https;
}

} // namespace

Sanitizer::Sanitizer(const HtmlSanitizeOptions &options, SanitizedHtml &result)
    : m_options(options)
    , m_result(result)
{
    m_css.allowRemote = options.allowRemoteImages;
}

void Sanitizer::run(QStringView html)
{
    m_body.reserve(html.size() + html.size() / 8);
    HtmlTokenizer tokenizer(html);
    HtmlToken token;
    while (tokenizer.next(token))
        handle(token);
    finish();
}

void Sanitizer::handle(const HtmlToken &token)
{
    if (m_skipping && skipToken(token))
        return;
    switch (token.type) {
    case HtmlToken::Type::Text:
        text(token.text);
        break;
    case HtmlToken::Type::StartTag:
        m_raw = Raw::None;
        startTag(token);
        break;
    case HtmlToken::Type::EndTag:
        endTag(token);
        break;
    case HtmlToken::Type::RawText:
        rawText(token);
        break;
    default: // comments (Outlook's conditional ones too), doctype, CDATA, PIs: never written
        break;
    }
}

void Sanitizer::startTag(const HtmlToken &token)
{
    const std::optional<Tag> found = lookupTag(token.name);
    if (!found) { // <o:p>, <custom-element>, <scr<script>: unwrapped unless hidden
        if (hides(token)) {
            ++m_result.removedElements;
            beginSkip(token.name, std::nullopt);
        }
        return;
    }
    const Tag tag = *found;
    const TagInfo &info = tagInfo(tag);
    switch (info.action) {
    case TagAction::Emit:
    case TagAction::Unwrap:
        if (hides(token)) {
            ++m_result.removedElements;
            if (!(info.flags & Void))
                beginSkip(token.name, tag);
        } else if (info.action == TagAction::Emit) {
            startElement(tag, token);
        }
        return;
    case TagAction::Drop:
        // A plain <meta> (charset, generator) is not worth reporting; one
        // with http-equiv (refresh, content-type games) is.
        if ((info.flags & Counted) || attributeValue(token, "http-equiv"_L1))
            ++m_result.removedElements;
        if (!(info.flags & Void) && !((info.flags & Foreign) && token.selfClosing))
            beginSkip(token.name, tag);
        return;
    case TagAction::DropRaw:
        if (info.flags & Counted)
            ++m_result.removedElements;
        m_raw = Raw::Drop;
        return;
    case TagAction::Style:
        m_raw = styleApplies(token) ? Raw::Style : Raw::Drop;
        return;
    case TagAction::Title:
        m_raw = Raw::Title;
        return;
    case TagAction::Structure:
        if (tag == Tag::Body && !m_bodySeen) {
            m_bodySeen = true;
            m_bodyAttributes = attributes(tag, token);
        }
        return;
    case TagAction::Ignore:
        return;
    }
}

void Sanitizer::startElement(Tag tag, const HtmlToken &token)
{
    if (m_suppressedTables > 0 && (tagInfo(tag).flags & TableFamily)) {
        suppressedTableTag(tag, true);
        return;
    }
    switch (tag) {
    case Tag::Img:
    case Tag::Image:
        image(token);
        return;
    case Tag::Br:
        lineBreak();
        return;
    case Tag::Wbr: // Qt ignores <wbr>; a zero-width space is the same break opportunity
        ensureFlow();
        m_body += QChar(0x200B);
        return;
    case Tag::Table:
        openTable(token);
        return;
    case Tag::Tbody:
    case Tag::Thead:
    case Tag::Tfoot:
    case Tag::Tr:
    case Tag::Td:
    case Tag::Th:
        tableStructure(tag, token);
        return;
    default:
        break;
    }
    closeImplied(tag);
    ensureFlow();
    open(tag, &token);
    if (tag == Tag::Xmp || tag == Tag::Plaintext)
        m_raw = Raw::Text; // their raw content is shown as text, in a <pre>
}

void Sanitizer::endTag(const HtmlToken &token)
{
    const std::optional<Tag> found = lookupTag(token.name);
    if (!found)
        return;
    const Tag tag = *found;
    const TagInfo &info = tagInfo(tag);
    if (info.action != TagAction::Emit)
        return;
    if (m_suppressedTables > 0 && (info.flags & TableFamily)) {
        suppressedTableTag(tag, false);
        return;
    }
    if (m_suppressed[index(tag)] > 0) {
        --m_suppressed[index(tag)];
        return;
    }
    if (tag == Tag::Br) // "</br>" is a line break in HTML5 too
        lineBreak();
    else if (info.flags & TableFamily)
        tableEnd(tag);
    else if (!(info.flags & Void))
        close(tag);
}

void Sanitizer::text(QStringView text)
{
    if (isBlank(text)) { // layout whitespace: kept between content, never forced into a cell
        if (m_contentStarted && !atTableLevel())
            appendEscaped(m_body, text);
        return;
    }
    ensureFlow();
    m_contentStarted = true;
    appendEscaped(m_body, text);
}

void Sanitizer::rawText(const HtmlToken &token)
{
    switch (std::exchange(m_raw, Raw::None)) {
    case Raw::Style:
        m_styles += filterStyleSheet(token.text, m_css, m_hiddenSelectors);
        break;
    case Raw::Title:
        if (m_result.title.isEmpty())
            m_result.title = token.text.simplified();
        break;
    case Raw::Text:
        if (!token.text.isEmpty()) {
            ensureFlow();
            m_contentStarted = true;
            appendEscaped(m_body, token.text);
        }
        break;
    case Raw::Drop:
    case Raw::None:
        break;
    }
}

void Sanitizer::image(const HtmlToken &token)
{
    const QString *src = attributeValue(token, "src"_L1);
    const CheckedUrl url = checkUrl(src ? QStringView(*src) : QStringView());
    QString source;
    switch (url.kind) {
    case UrlKind::Cid: {
        const QString id = decodeCid(QStringView(url.url).sliced(4));
        if (!id.isEmpty() && remember(m_cidSeen, m_result.cidReferences, id, kMaxCidImages))
            source = cidUrl(id);
        break;
    }
    case UrlKind::Data: {
        const int at = inlineImage(url.url);
        if (at >= 0)
            source = inlineImageUrl(at);
        break;
    }
    case UrlKind::Http:
    case UrlKind::Https:
        // AGENT-NOTE: with pictures allowed, an image of 2 × 2 px or less is
        // a read receipt nobody can see: dropped from `html` and from
        // remoteImages, so a renderer that fetches that list never sends it.
        if (m_options.allowRemoteImages && isTrackingPixel(token))
            break;
        if (!remember(m_remoteSeen, m_result.remoteImages, url.url, kMaxRemoteImages))
            break;
        if (!m_options.allowRemoteImages) {
            m_result.remoteBlocked = true;
            const QString *alt = attributeValue(token, "alt"_L1);
            const QString shown = alt ? alt->simplified() : QString();
            if (!shown.isEmpty()) {
                ensureFlow();
                m_contentStarted = true;
                m_body += "<span>"_L1;
                appendEscaped(m_body, shown);
                m_body += "</span>"_L1;
            }
            return;
        }
        source = url.url;
        break;
    default: // relative paths, file:, ftp:, javascript:, other data: — never loaded
        break;
    }
    if (source.isEmpty()) {
        ++m_result.removedElements;
        return;
    }
    ensureFlow();
    m_contentStarted = true;
    m_body += "<img src=\""_L1;
    appendEscaped(m_body, source);
    m_body += u'"';
    m_body += attributes(Tag::Img, token);
    m_body += u'>';
}

int Sanitizer::inlineImage(const QString &dataUrl)
{
    if (const auto known = m_dataIndex.constFind(dataUrl); known != m_dataIndex.cend())
        return known.value();
    if (m_result.inlineImages.size() >= kMaxInlineImages)
        return -1;
    std::optional<QByteArray> bytes = decodeDataImage(QStringView(dataUrl).sliced(5), m_options.maxInlineImageBytes);
    if (!bytes)
        return -1;
    const int at = int(m_result.inlineImages.size());
    m_result.inlineImages.append(std::move(*bytes));
    m_dataIndex.insert(dataUrl, at);
    return at;
}

bool Sanitizer::isTrackingPixel(const HtmlToken &token) const
{
    const QString *widthValue = attributeValue(token, "width"_L1);
    const QString *heightValue = attributeValue(token, "height"_L1);
    int width = widthValue ? leadingInteger(*widthValue) : -1;
    int height = heightValue ? leadingInteger(*heightValue) : -1;
    if (const QString *style = attributeValue(token, "style"_L1)) {
        if (const int pixels = inlineStylePixels(*style, "width"_L1); pixels >= 0)
            width = pixels;
        if (const int pixels = inlineStylePixels(*style, "height"_L1); pixels >= 0)
            height = pixels;
    }
    return width >= 0 && height >= 0 && width <= 2 && height <= 2;
}

// <style media="print"> or a media query (mobile overrides) is not for the
// reading pane.
bool Sanitizer::styleApplies(const HtmlToken &token) const
{
    const QString *media = attributeValue(token, "media"_L1);
    if (!media || isBlank(*media))
        return true;
    const QString lower = media->toLower();
    return !lower.contains(u'(') && (lower.contains("screen"_L1) || lower.contains("all"_L1));
}

void Sanitizer::noteBackground(QStringView value)
{
    if (isRemote(checkUrl(value).kind) && !m_options.allowRemoteImages)
        m_result.remoteBlocked = true;
}

QString Sanitizer::attributes(Tag tag, const HtmlToken &token, int colspan)
{
    QString out;
    quint32 seen = 0;
    m_result.removedAttributes += token.droppedAttributes;
    for (const HtmlAttribute &attribute : token.attributes) {
        const std::optional<Attr> attr = allowedAttribute(tag, attribute.name);
        if (!attr) {
            if (attribute.name == "background"_L1) // an image URL Qt would load
                noteBackground(attribute.value);
            ++m_result.removedAttributes;
            continue;
        }
        const quint32 bit = 1u << unsigned(*attr);
        if (seen & bit)
            continue; // HTML5 keeps the first of duplicate attributes
        seen |= bit;
        switch (*attr) {
        case Attr::Src:     // image() writes the checked source itself
        case Attr::Colspan: // the table code decides the span (`colspan`)
            break;
        case Attr::Href: {
            const CheckedUrl url = checkUrl(attribute.value);
            if (isLink(url.kind))
                appendAttribute(out, "href"_L1, url.url);
            else
                ++m_result.removedAttributes; // the link text stays, without a target
            break;
        }
        case Attr::Style: {
            const QString css = filterInlineStyle(attribute.value, m_css);
            if (!css.isEmpty())
                appendAttribute(out, "style"_L1, css);
            else if (!isBlank(attribute.value))
                ++m_result.removedAttributes;
            break;
        }
        default: {
            QString value;
            if (cleanAttributeValue(*attr, attribute.value, value))
                appendAttribute(out, attributeName(*attr), value);
            break;
        }
        }
    }
    if (colspan > 1)
        appendAttribute(out, "colspan"_L1, QString::number(colspan));
    return out;
}

void Sanitizer::finish()
{
    popTo(0);
    QString &html = m_result.html;
    html.reserve(m_body.size() + m_styles.size() + m_bodyAttributes.size() + 64);
    html += "<html><head>"_L1;
    if (!m_styles.isEmpty()) {
        // Qt's HTML parser has no raw-text mode for <style>: it decodes
        // references there and would start a tag at '<'. So the sheet is
        // escaped like text (the filter already removed every '<').
        html += "<style>"_L1;
        appendEscaped(html, m_styles);
        html += "</style>"_L1;
    }
    html += "</head><body"_L1;
    html += m_bodyAttributes;
    html += u'>';
    html += m_body;
    html += "</body></html>"_L1;
    m_result.remoteBlocked = m_result.remoteBlocked || m_css.remoteBlocked;
}

} // namespace Detail

} // namespace QindaOffice::Mail
