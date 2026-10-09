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

bool insertBeforeAny(QByteArray *sheetXml, const QStringList &beforeTags,
                     const QByteArray &fragment)
{
    int earliest = -1;
    for (const QString &tag : beforeTags) {
        // Match "<tag " and "<tag>" so namespace-free and attributed shapes
        // both count (QXlsx writes no prefix).
        const QByteArray withAttr = QByteArrayLiteral("<") + tag.toUtf8() + QByteArrayLiteral(" ");
        const QByteArray bare = QByteArrayLiteral("<") + tag.toUtf8() + QByteArrayLiteral(">");
        const int a = sheetXml->indexOf(withAttr);
        const int b = sheetXml->indexOf(bare);
        const int found = a >= 0 && b >= 0 ? qMin(a, b) : qMax(a, b);
        if (found >= 0 && (earliest < 0 || found < earliest)) {
            earliest = found;
        }
    }
    if (earliest < 0) {
        return false;
    }
    sheetXml->insert(earliest, fragment);
    return true;
}

bool insertAfterTag(QByteArray *sheetXml, const QString &tag, const QByteArray &fragment)
{
    const QByteArray marker = QByteArrayLiteral("</") + tag.toUtf8() + QByteArrayLiteral(">");
    const int found = sheetXml->indexOf(marker);
    if (found < 0) {
        return false;
    }
    sheetXml->insert(found + marker.size(), fragment);
    return true;
}

} // namespace QindaOffice::Calc::IO
