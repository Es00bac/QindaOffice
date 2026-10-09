// SPDX-License-Identifier: GPL-3.0-or-later
// Content the HTML mail sanitizer removes together with its element
// (html_sanitizer_p.h): hidden elements, and the skip mode that swallows an
// element's content — for hidden elements and for embedded or active content
// dropped whole: <object>, <svg>, <select>, <template>, <video>, ….

#include "html_sanitizer_p.h"

#include <utility>

using namespace Qt::StringLiterals;

namespace QindaOffice::Mail::Detail {

// Hidden content is removed with the element: the `hidden` attribute,
// display:none or mso-hide:all inline (any spacing, case or !important) and
// classes/ids a <style> rule hides — Qt itself ignores display:none, and
// hidden preheaders and mobile-only blocks are not meant to be read.
bool Sanitizer::hides(const HtmlToken &token) const
{
    bool style = false;
    bool classes = false;
    bool id = false; // HTML5 uses the first of duplicate attributes
    for (const HtmlAttribute &attribute : token.attributes) {
        if (attribute.name == "hidden"_L1)
            return true;
        if (attribute.name == "style"_L1 && !std::exchange(style, true) && inlineStyleHides(attribute.value))
            return true;
        if (m_hiddenSelectors.isEmpty())
            continue;
        if (attribute.name == "id"_L1 && !std::exchange(id, true)
            && matchesHidden(u'#', attribute.value, token.name))
            return true;
        if (attribute.name != "class"_L1 || std::exchange(classes, true))
            continue;
        const QStringView list(attribute.value);
        for (qsizetype i = 0; i < list.size();) {
            while (i < list.size() && list[i].isSpace())
                ++i;
            const qsizetype start = i;
            while (i < list.size() && !list[i].isSpace())
                ++i;
            if (i > start && matchesHidden(u'.', list.sliced(start, i - start), token.name))
                return true;
        }
    }
    return false;
}

bool Sanitizer::matchesHidden(QChar kind, QStringView name, const QString &tagName) const
{
    QString key;
    key.reserve(tagName.size() + 1 + name.size());
    key += kind;
    key += name;
    if (m_hiddenSelectors.contains(key))
        return true;
    key.prepend(tagName);
    return m_hiddenSelectors.contains(key);
}

void Sanitizer::beginSkip(const QString &name, std::optional<Tag> tag)
{
    m_skipping = true;
    m_skipName = name;
    m_skipTag = tag;
    m_skipForeign = tag && (tagInfo(*tag).flags & Foreign);
    m_skipDepth = 0;
}

void Sanitizer::endSkip()
{
    m_skipping = false;
    for (const Tag tag : m_skipTouched)
        m_skipInner[index(tag)] = 0;
    m_skipTouched.clear();
}

namespace {

// A start tag that ends the removed element the way HTML5 implies an end
// tag (a hidden <li> ends at the next <li>, a hidden <p> at the next block).
bool endsImplicitly(Tag removed, Tag next)
{
    switch (removed) {
    case Tag::Li:
        return next == Tag::Li;
    case Tag::Dd:
    case Tag::Dt:
        return next == Tag::Dd || next == Tag::Dt;
    case Tag::P:
        return tagInfo(next).flags & Block;
    case Tag::Td:
    case Tag::Th:
        return next == Tag::Td || next == Tag::Th || next == Tag::Tr || next == Tag::Tbody
               || next == Tag::Thead || next == Tag::Tfoot;
    case Tag::Tr:
        return next == Tag::Tr || next == Tag::Tbody || next == Tag::Thead || next == Tag::Tfoot;
    case Tag::Tbody:
    case Tag::Thead:
    case Tag::Tfoot:
        return next == Tag::Tbody || next == Tag::Thead || next == Tag::Tfoot;
    case Tag::Option:
        return next == Tag::Option || next == Tag::Optgroup;
    case Tag::A:
        return next == Tag::A;
    default:
        return false;
    }
}

} // namespace

// True when `next` implicitly ends the removed element — but not when it
// belongs to a list or table opened inside it (a nested <ul>'s <li>).
bool Sanitizer::endsSkippedElement(Tag next) const
{
    if (m_skipDepth != 0 || !m_skipTag || !endsImplicitly(*m_skipTag, next))
        return false;
    const auto inner = [this](Tag tag) { return m_skipInner[index(tag)]; };
    switch (*m_skipTag) {
    case Tag::Li:
        return inner(Tag::Ul) + inner(Tag::Ol) + inner(Tag::Dir) + inner(Tag::Menu) + inner(Tag::Table) == 0;
    case Tag::Dd:
    case Tag::Dt:
        return inner(Tag::Dl) + inner(Tag::Table) == 0;
    case Tag::Td:
    case Tag::Th:
    case Tag::Tr:
    case Tag::Tbody:
    case Tag::Thead:
    case Tag::Tfoot:
        return inner(Tag::Table) == 0;
    default:
        return true;
    }
}

// Inside an element removed with its content. True: the token is swallowed;
// false: the element ended implicitly and the token must be handled normally.
bool Sanitizer::skipToken(const HtmlToken &token)
{
    if (token.type == HtmlToken::Type::StartTag) {
        const std::optional<Tag> tag = lookupTag(token.name);
        if (tag && endsSkippedElement(*tag)) {
            endSkip();
            return false;
        }
        if (token.name == m_skipName) {
            if (!(m_skipForeign && token.selfClosing))
                ++m_skipDepth;
            return true;
        }
        if (tag && !(tagInfo(*tag).flags & Void) && m_skipInner[index(*tag)]++ == 0)
            m_skipTouched.push_back(*tag);
        return true;
    }
    if (token.type == HtmlToken::Type::EndTag) {
        if (token.name == m_skipName) {
            if (m_skipDepth > 0)
                --m_skipDepth;
            else
                endSkip();
            return true;
        }
        const std::optional<Tag> tag = lookupTag(token.name);
        if (!tag)
            return true;
        if (m_skipInner[index(*tag)] > 0) {
            --m_skipInner[index(*tag)];
            return true;
        }
        if (m_open[index(*tag)] > 0) { // an ancestor's end tag ends the removed element too
            endSkip();
            return false;
        }
    }
    return true; // text, raw text, comments: removed with the element
}

} // namespace QindaOffice::Mail::Detail
