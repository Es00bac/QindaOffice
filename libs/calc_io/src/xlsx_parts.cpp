// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_parts.h"

#include <QDir>
#include <QXmlStreamReader>

#include <xlsxcolor_p.h>
#include <xlsxzipreader_p.h>

namespace QindaOffice::Calc::IO {

std::optional<CellAddr> addrFromText(const QString &text)
{
    QString clean = text;
    clean.remove(QLatin1Char('$'));
    int i = 0;
    while (i < clean.size() && clean.at(i).isLetter()) {
        ++i;
    }
    if (i == 0 || i == clean.size()) {
        return std::nullopt;
    }
    const std::optional<qint32> col = colFromName(clean.left(i));
    bool ok = false;
    const qint32 row = clean.mid(i).toInt(&ok);
    if (!col.has_value() || !ok || row < 1) {
        return std::nullopt;
    }
    return CellAddr{row - 1, *col};
}

std::optional<CellRange> rangeFromText(const QString &text)
{
    const int colon = text.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        const std::optional<CellAddr> single = addrFromText(text);
        if (!single.has_value()) {
            return std::nullopt;
        }
        return CellRange{*single, *single};
    }
    const std::optional<CellAddr> first = addrFromText(text.left(colon));
    const std::optional<CellAddr> last = addrFromText(text.mid(colon + 1));
    if (!first.has_value() || !last.has_value()) {
        return std::nullopt;
    }
    return CellRange{*first, *last};
}

QHash<QString, QPair<QString, QString>> readRels(QXlsx::ZipReader &zip, const QString &relsPath)
{
    QHash<QString, QPair<QString, QString>> out;
    const QByteArray xml = zip.fileData(relsPath);
    if (xml.isEmpty()) {
        return out;
    }
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement
            || reader.name() != QLatin1String("Relationship")) {
            continue;
        }
        const auto attributes = reader.attributes();
        out.insert(attributes.value(QLatin1String("Id")).toString(),
                   {attributes.value(QLatin1String("Type")).toString(),
                    attributes.value(QLatin1String("Target")).toString()});
    }
    return out;
}

QVector<QString> sheetPartMap(QXlsx::ZipReader &zip)
{
    QVector<QString> out;
    const QHash<QString, QPair<QString, QString>> rels =
        readRels(zip, QStringLiteral("xl/_rels/workbook.xml.rels"));
    const QByteArray xml = zip.fileData(QStringLiteral("xl/workbook.xml"));
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement
            || reader.name() != QLatin1String("sheet")) {
            continue;
        }
        const QString rid = reader.attributes().value(QLatin1String("r:id")).toString();
        const QString target = rels.value(rid).second;
        out.append(target.isEmpty() ? QString()
                                    : QDir::cleanPath(QStringLiteral("xl/") + target));
    }
    return out;
}

QString resolveTarget(const QString &ownerDir, const QString &target)
{
    if (target.startsWith(QLatin1Char('/'))) {
        return QDir::cleanPath(target.mid(1));
    }
    return QDir::cleanPath(ownerDir + QLatin1Char('/') + target);
}

std::optional<Rgb> colorFromAttributes(const QXmlStreamAttributes &attributes,
                                       const ThemePalette &palette)
{
    if (attributes.hasAttribute(QLatin1String("rgb"))) {
        // xlsx hex colours carry no '#' ("FFC7CE" is ARGB).
        const QColor c = QColor::fromString(
            QStringLiteral("#") + attributes.value(QLatin1String("rgb")).toString());
        if (c.isValid()) {
            return Rgb{quint8(c.red()), quint8(c.green()), quint8(c.blue())};
        }
    }
    if (attributes.hasAttribute(QLatin1String("theme"))) {
        const QXlsx::XlsxColor raw(attributes.value(QLatin1String("theme")).toString(),
                                   attributes.value(QLatin1String("tint")).toString());
        return resolveXlsxColor(raw, palette);
    }
    if (attributes.hasAttribute(QLatin1String("indexed"))) {
        bool ok = false;
        const int index = attributes.value(QLatin1String("indexed")).toInt(&ok);
        if (ok) {
            return resolveXlsxColor(QXlsx::XlsxColor(index), palette);
        }
    }
    return std::nullopt;
}

} // namespace QindaOffice::Calc::IO
