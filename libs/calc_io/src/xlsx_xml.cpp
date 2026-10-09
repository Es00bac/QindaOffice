// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_xml.h"

#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

namespace QindaOffice::Calc::IO {

namespace {

QString appendRelationshipEntry(QByteArray *relsXml, const QString &type, const QByteArray &target, bool external);

} // namespace

QString appendRelationship(QByteArray *relsXml, const QString &type, const QString &target)
{
    return appendRelationshipEntry(relsXml, type, target.toUtf8(), false);
}

QString appendExternalRelationship(QByteArray *relsXml, const QString &type, const QString &target)
{
    return appendRelationshipEntry(relsXml, type, xmlAttribute(target), true);
}

QByteArray xmlAttribute(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '"': out += QLatin1String("&quot;"); break;
        case '\t': out += QLatin1String("&#9;"); break;
        case '\n': out += QLatin1String("&#10;"); break;
        case '\r': out += QLatin1String("&#13;"); break;
        default:
            if (c.unicode() >= 0x20 && c.unicode() != 0xfffe && c.unicode() != 0xffff) {
                out += c;
            }
        }
    }
    return out.toUtf8();
}

namespace {

QString appendRelationshipEntry(QByteArray *relsXml, const QString &type, const QByteArray &target, bool external)
{
    // Mint a fresh id: collect every existing one first (producers are free
    // to use non-numeric ids — "Rb140c…" — so max+1 is not a strategy).
    QSet<QString> used;
    if (!relsXml->isEmpty()) {
        QXmlStreamReader reader(*relsXml);
        while (!reader.atEnd()) {
            if (reader.readNext() == QXmlStreamReader::StartElement
                && reader.name() == QLatin1String("Relationship")) {
                used.insert(reader.attributes().value(QLatin1String("Id")).toString());
            }
        }
    }
    QString id;
    for (int n = 1;; ++n) {
        id = QStringLiteral("rId%1").arg(n);
        if (!used.contains(id)) {
            break;
        }
    }
    const QByteArray entry = QByteArrayLiteral("<Relationship Id=\"") + id.toUtf8()
        + QByteArrayLiteral("\" Type=\"") + type.toUtf8()
        + QByteArrayLiteral("\" Target=\"") + target
        + (external ? QByteArrayLiteral("\" TargetMode=\"External") : QByteArray())
        + QByteArrayLiteral("\"/>");
    if (relsXml->isEmpty()) {
        *relsXml = QByteArrayLiteral(
                       "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                       "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">")
            + entry + QByteArrayLiteral("</Relationships>");
    } else {
        const int close = relsXml->lastIndexOf("</Relationships>");
        if (close < 0) {
            return {};
        }
        relsXml->insert(close, entry);
    }
    return id;
}

} // namespace

void appendContentTypeOverride(QByteArray *contentTypesXml, const QString &partName,
                               const QString &contentType)
{
    if (contentTypesXml->contains(partName.toUtf8())) {
        return;
    }
    const QByteArray entry = QByteArrayLiteral("<Override PartName=\"") + partName.toUtf8()
        + QByteArrayLiteral("\" ContentType=\"") + contentType.toUtf8()
        + QByteArrayLiteral("\"/>");
    const int close = contentTypesXml->lastIndexOf("</Types>");
    if (close >= 0) {
        contentTypesXml->insert(close, entry);
    }
}

void appendContentTypeDefault(QByteArray *contentTypesXml, const QString &extension,
                              const QString &contentType)
{
    const QByteArray probe = QByteArrayLiteral("Extension=\"") + extension.toUtf8()
        + QByteArrayLiteral("\"");
    if (contentTypesXml->contains(probe)) {
        return;
    }
    const QByteArray entry = QByteArrayLiteral("<Default Extension=\"") + extension.toUtf8()
        + QByteArrayLiteral("\" ContentType=\"") + contentType.toUtf8()
        + QByteArrayLiteral("\"/>");
    const int types = int(contentTypesXml->indexOf("<Types"));
    const int close = types >= 0 ? int(contentTypesXml->indexOf('>', types)) : -1;
    if (close >= 0) {
        contentTypesXml->insert(close + 1, entry);
    }
}

namespace {

// A tag's local name ("x14:cfRule" → "cfRule" is NOT done: names here are
// compared as written, prefix included, since the post-pass works on its
// own unprefixed SpreadsheetML).
QByteArray tagName(const QByteArray &xml, qsizetype nameStart)
{
    qsizetype end = nameStart;
    while (end < xml.size()) {
        const char c = xml.at(end);
        if (c == ' ' || c == '>' || c == '/' || c == '\t' || c == '\n' || c == '\r') {
            break;
        }
        ++end;
    }
    return xml.mid(nameStart, end - nameStart);
}

// The end of the tag that starts at `open` ('<'): the offset of its '>',
// quoted attribute values skipped (they may hold '>'). -1 when unterminated.
qsizetype tagEnd(const QByteArray &xml, qsizetype open)
{
    char quote = 0;
    for (qsizetype i = open + 1; i < xml.size(); ++i) {
        const char c = xml.at(i);
        if (quote != 0) {
            quote = c == quote ? 0 : quote;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '>') {
            return i;
        }
    }
    return -1;
}

// Calls `visit(offset, depth, name, isEnd, selfClosing)` for every element
// tag of `xml`, depth counting from 0 for the root element; comments,
// processing instructions and CDATA are skipped. Stops when visit returns
// true; returns that offset or -1.
template<typename Visit>
qsizetype walkTags(const QByteArray &xml, Visit visit)
{
    int depth = -1;
    for (qsizetype pos = xml.indexOf('<'); pos >= 0 && pos + 1 < xml.size(); pos = xml.indexOf('<', pos + 1)) {
        const char next = xml.at(pos + 1);
        if (next == '?') {
            pos = xml.indexOf("?>", pos);
            if (pos < 0) {
                return -1;
            }
            continue;
        }
        if (next == '!') {
            if (xml.mid(pos, 4) == "<!--") {
                pos = xml.indexOf("-->", pos);
            } else if (xml.mid(pos, 9) == "<![CDATA[") {
                pos = xml.indexOf("]]>", pos);
            } else {
                pos = xml.indexOf('>', pos);
            }
            if (pos < 0) {
                return -1;
            }
            continue;
        }
        const qsizetype end = tagEnd(xml, pos);
        if (end < 0) {
            return -1;
        }
        if (next == '/') {
            const QByteArray name = tagName(xml, pos + 2);
            if (visit(pos, depth, name, true, false)) {
                return pos;
            }
            --depth;
        } else {
            const bool selfClosing = xml.at(end - 1) == '/';
            ++depth;
            const QByteArray name = tagName(xml, pos + 1);
            if (visit(pos, depth, name, false, selfClosing)) {
                return pos;
            }
            if (selfClosing) {
                --depth;
            }
        }
        pos = end;
    }
    return -1;
}

// "name", "<name", "/root" or "</root>" → the bare name and whether it is
// the root's end tag.
QPair<QByteArray, bool> anchorOf(QString tag)
{
    if (tag.startsWith(QLatin1Char('<'))) {
        tag.remove(0, 1);
    }
    if (tag.endsWith(QLatin1Char('>'))) {
        tag.chop(1);
    }
    const bool end = tag.startsWith(QLatin1Char('/'));
    return {(end ? tag.mid(1) : tag).toUtf8(), end};
}

} // namespace

qsizetype topLevelTagOffset(const QByteArray &xml, const QStringList &tags)
{
    QList<QPair<QByteArray, bool>> anchors;
    for (const QString &tag : tags) {
        anchors.append(anchorOf(tag));
    }
    return walkTags(xml, [&anchors](qsizetype, int depth, const QByteArray &name, bool isEnd, bool) {
        for (const auto &[anchor, rootEnd] : anchors) {
            if (rootEnd ? (isEnd && depth == 0 && name == anchor) : (!isEnd && depth == 1 && name == anchor)) {
                return true;
            }
        }
        return false;
    });
}

qsizetype topLevelElementEnd(const QByteArray &xml, const QString &name)
{
    const QByteArray wanted = name.toUtf8();
    return walkTags(xml, [&wanted](qsizetype, int depth, const QByteArray &tag, bool isEnd, bool) {
        return isEnd && depth == 1 && tag == wanted;
    });
}

bool insertBeforeAny(QByteArray *sheetXml, const QStringList &beforeTags,
                     const QByteArray &fragment)
{
    // AGENT-GUARD (M78, D-087): only the root's own children count. A kept
    // conditional format carries an <extLst> of its own, a custom sheet view
    // a <pageSetup>: the first textual match used to be one of those, and
    // the fragment landed inside the wrong element.
    const qsizetype at = topLevelTagOffset(*sheetXml, beforeTags);
    if (at < 0) {
        return false;
    }
    sheetXml->insert(at, fragment);
    return true;
}

bool insertAfterTag(QByteArray *sheetXml, const QString &tag, const QByteArray &fragment)
{
    const qsizetype found = topLevelElementEnd(*sheetXml, tag);
    if (found < 0) {
        return false;
    }
    sheetXml->insert(sheetXml->indexOf('>', found) + 1, fragment);
    return true;
}

} // namespace QindaOffice::Calc::IO
