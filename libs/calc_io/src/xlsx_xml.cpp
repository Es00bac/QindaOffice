// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_xml.h"

#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

namespace QindaOffice::Calc::IO {

QString appendRelationship(QByteArray *relsXml, const QString &type, const QString &target)
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
        + QByteArrayLiteral("\" Target=\"") + target.toUtf8()
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
