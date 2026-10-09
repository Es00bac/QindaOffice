// SPDX-License-Identifier: GPL-3.0-or-later
// The dxf table of conditional formats (xlsx_rules.h, M78, D-087): the
// source package's <dxf> elements as written, decoded into the model's
// CondFormat, and the table a save writes — the source's dxfs first and
// unchanged (a kept rule, an untouched format and a carried PivotTable
// format keep their dxfId), then one new dxf per format nobody had.
//
// AGENT-NOTE: a dxf's visible fill is patternFill's bgColor (Excel's dxf
// convention — the reverse of a cell fill), with fgColor as the fallback
// some producers write.

#include "xlsx_rules.h"

#include "xlsx_parts.h"
#include "xlsx_rules_internal.h"

#include <QXmlStreamReader>

namespace QindaOffice::Calc::IO {

using namespace Rules;

namespace {

// A <font> / <fill> child of a dxf: its text, and the part of the font the
// model does not hold.
struct DxfParts {
    QString font;        // the whole <font> element
    QString fontKept;    // <font>'s children other than b, i, strike, u, color
    QString numFmt;
    QString fill;
    QString alignment;
    QString protection;
    QString border;
};

DxfParts splitDxf(const QString &dxfXml)
{
    DxfParts parts;
    if (dxfXml.isEmpty()) {
        return parts;
    }
    QString text;
    const QList<RawNode> nodes = scanDxfs(QByteArrayLiteral("<s xmlns=\"") + QString::fromUtf16(kMainNs).toUtf8()
                                              + QByteArrayLiteral("\"><dxfs>") + dxfXml.toUtf8()
                                              + QByteArrayLiteral("</dxfs></s>"),
                                          &text);
    if (nodes.isEmpty()) {
        return parts;
    }
    for (const RawNode &child : nodes.first().children) {
        const QString slice = text.mid(child.start, child.end - child.start);
        if (child.name == QLatin1String("font")) {
            parts.font = slice;
            for (const RawNode &item : child.children) {
                static const QStringList modelled{QStringLiteral("b"), QStringLiteral("i"), QStringLiteral("strike"),
                                                  QStringLiteral("u"), QStringLiteral("color")};
                if (!modelled.contains(item.name)) {
                    parts.fontKept += text.mid(item.start, item.end - item.start);
                }
            }
        } else if (child.name == QLatin1String("numFmt")) {
            parts.numFmt = slice;
        } else if (child.name == QLatin1String("fill")) {
            parts.fill = slice;
        } else if (child.name == QLatin1String("alignment")) {
            parts.alignment = slice;
        } else if (child.name == QLatin1String("protection")) {
            parts.protection = slice;
        } else if (child.name == QLatin1String("border")) {
            parts.border = slice;
        }
    }
    return parts;
}

QString flag(const char *name, const std::optional<bool> &on)
{
    if (!on.has_value()) {
        return {};
    }
    return *on ? QStringLiteral("<%1/>").arg(QLatin1String(name)) : QStringLiteral("<%1 val=\"0\"/>").arg(QLatin1String(name));
}

} // namespace

namespace Rules {

CondFormat decodeDxf(const QString &dxfXml, const ThemePalette &palette)
{
    CondFormat format;
    QXmlStreamReader reader(dxfXml);
    bool inFont = false;
    bool inPattern = false;
    std::optional<Rgb> foregroundFill;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement) {
            if (reader.name() == QLatin1String("font")) {
                inFont = false;
            } else if (reader.name() == QLatin1String("patternFill")) {
                inPattern = false;
            }
            continue;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView name = reader.name();
        const auto attributes = reader.attributes();
        const auto value = [&attributes]() { return attributes.value(QLatin1String("val")).toString(); };
        if (name == QLatin1String("font")) {
            inFont = true;
        } else if (name == QLatin1String("patternFill")) {
            inPattern = attributes.value(QLatin1String("patternType")) != QLatin1String("none");
        } else if (inFont && name == QLatin1String("b")) {
            format.bold = boolAttr(value(), true);
        } else if (inFont && name == QLatin1String("i")) {
            format.italic = boolAttr(value(), true);
        } else if (inFont && name == QLatin1String("strike")) {
            format.strikeout = boolAttr(value(), true);
        } else if (inFont && name == QLatin1String("u")) {
            format.underline = value() != QLatin1String("none");
        } else if (inFont && name == QLatin1String("color")) {
            format.foreground = colorFromAttributes(attributes, palette);
        } else if (inPattern && name == QLatin1String("bgColor")) {
            format.background = colorFromAttributes(attributes, palette);
        } else if (inPattern && name == QLatin1String("fgColor")) {
            foregroundFill = colorFromAttributes(attributes, palette);
        }
    }
    if (!format.background.has_value()) {
        format.background = foregroundFill;
    }
    return format;
}

QByteArray encodeDxf(const CondFormat &format, const QString &base, const ThemePalette &palette)
{
    const DxfParts parts = splitDxf(base);
    QString font = flag("b", format.bold) + flag("i", format.italic) + flag("strike", format.strikeout);
    if (format.underline.has_value()) {
        font += *format.underline ? QStringLiteral("<u/>") : QStringLiteral("<u val=\"none\"/>");
    }
    if (format.foreground.has_value()) {
        font += QStringLiteral("<color rgb=\"%1\"/>").arg(rgbText(*format.foreground));
    }
    font += parts.fontKept;
    QString out = QStringLiteral("<dxf>");
    if (!font.isEmpty()) {
        out += QStringLiteral("<font>") + font + QStringLiteral("</font>");
    }
    out += parts.numFmt;
    if (format.background.has_value()) {
        out += QStringLiteral("<fill><patternFill><bgColor rgb=\"%1\"/></patternFill></fill>").arg(rgbText(*format.background));
    } else if (!parts.fill.isEmpty() && !decodeDxf(base, palette).background.has_value()) {
        out += parts.fill; // a fill the model cannot read (a gradient, a pattern) stays
    }
    out += parts.alignment + parts.protection + parts.border + QStringLiteral("</dxf>");
    return out.toUtf8();
}

} // namespace Rules

QStringList readSourceDxfs(const QByteArray &stylesXml)
{
    QStringList out;
    if (stylesXml.isEmpty()) {
        return out;
    }
    QString text;
    const QList<RawNode> dxfs = scanDxfs(stylesXml, &text);
    // A dxf goes into a styles part whose root declares SpreadsheetML as the
    // default namespace and nothing else.
    const QHash<QString, QString> scope{{QString(), QString::fromUtf16(kMainNs)}};
    for (const RawNode &dxf : dxfs) {
        out.append(selfContained(text, dxf, scope));
    }
    return out;
}

DxfTable::DxfTable(const QStringList &sourceDxfs, const ThemePalette &palette)
    : m_source(sourceDxfs)
    , m_palette(palette)
{
}

int DxfTable::idFor(const CondFormatRule &rule)
{
    const bool fromSource = rule.dxfId >= 0 && rule.dxfId < m_source.size();
    if (fromSource && decodeDxf(m_source.at(rule.dxfId), m_palette) == rule.format) {
        return rule.dxfId;
    }
    if (!fromSource && rule.format == CondFormat{}) {
        return -1; // a rule that formats nothing names no dxf
    }
    const QByteArray xml = encodeDxf(rule.format, fromSource ? m_source.at(rule.dxfId) : QString(), m_palette);
    qsizetype index = m_added.indexOf(xml);
    if (index < 0) {
        m_added.append(xml);
        m_addedFormats.append(rule.format);
        index = m_added.size() - 1;
    }
    return int(m_source.size() + index);
}

QByteArray DxfTable::xml() const
{
    if (m_source.isEmpty() && m_added.isEmpty()) {
        return {};
    }
    QByteArray out = QByteArrayLiteral("<dxfs count=\"") + QByteArray::number(m_source.size() + m_added.size())
        + QByteArrayLiteral("\">");
    for (const QString &dxf : m_source) {
        out += dxf.toUtf8();
    }
    for (const QByteArray &dxf : m_added) {
        out += dxf;
    }
    return out + QByteArrayLiteral("</dxfs>");
}

} // namespace QindaOffice::Calc::IO
