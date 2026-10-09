// SPDX-License-Identifier: GPL-3.0-or-later
#include "fodp_read_text.h"

namespace QindaOffice::Show::Fodp {

namespace {

using Odf::OdfNode;

// Most specific first: span styles (innermost first), then the paragraph
// style, then the shape's text style, then its graphic style. Legacy spans
// (pre-M62 QindaShow) carry fo: attributes directly; they come first.
struct Chain {
    QList<const OdfNode *> legacy;
    QList<QPair<QString, QString>> styles; // name, family
};

QString runProperty(const StyleSheet &sheet, const Chain &chain, QStringView ns, QStringView local)
{
    for (const OdfNode *span : chain.legacy) {
        if (span->hasAttr(ns, local)) {
            return span->attr(ns, local);
        }
    }
    for (const auto &[name, family] : chain.styles) {
        const QString value = sheet.property(name, family, u"text-properties", ns, local);
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

QString firstFamily(QString value)
{
    value = value.section(QLatin1Char(','), 0, 0).trimmed();
    if (value.size() >= 2 && (value.front() == QLatin1Char('\'') || value.front() == QLatin1Char('"'))) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

TextRun formattedRun(const StyleSheet &sheet, const Chain &chain)
{
    using namespace Odf;
    TextRun run;
    const QString weight = runProperty(sheet, chain, kFo, u"font-weight");
    run.bold = weight == u"bold" || weight.toInt() >= 600;
    const QString style = runProperty(sheet, chain, kFo, u"font-style");
    run.italic = style == u"italic" || style == u"oblique";
    const QString exact = runProperty(sheet, chain, kQq, u"color");
    const QString color = runProperty(sheet, chain, kFo, u"color");
    if (exact == u"none") {
        run.color = QColor();
    } else if (!exact.isEmpty()) {
        run.color = QColor::fromString(exact);
    } else if (!color.isEmpty()) {
        run.color = QColor::fromString(color);
    }
    const qreal size = lengthToPt(runProperty(sheet, chain, kFo, u"font-size"));
    if (size > 0) {
        run.pointSize = size;
    }
    QString family = firstFamily(runProperty(sheet, chain, kFo, u"font-family"));
    if (family.isEmpty()) {
        family = firstFamily(runProperty(sheet, chain, kStyle, u"font-name"));
    }
    run.fontFamily = family;
    return run;
}

int alignmentFrom(const QString &value)
{
    if (value == u"center") {
        return int(Qt::AlignHCenter);
    }
    if (value == u"end" || value == u"right") {
        return int(Qt::AlignRight);
    }
    if (value == u"justify") {
        return int(Qt::AlignJustify);
    }
    if (value == u"start" || value == u"left") {
        return int(Qt::AlignLeft);
    }
    return 0;
}

// Builds the runs of one paragraph, collapsing raw white space across span
// boundaries the way ODF consumers do.
class ParagraphBuilder {
public:
    ParagraphBuilder(const StyleSheet &sheet)
        : m_sheet(sheet)
    {
    }

    void walk(const OdfNode &node, const Chain &chain)
    {
        using namespace Odf;
        for (const Odf::OdfNodePtr &childPtr : node.children) {
            const OdfNode &child = *childPtr;
            if (child.isText()) {
                // Indentation between elements (it always holds a newline,
                // which no producer writes as content) is not text.
                if (child.text.trimmed().isEmpty() && child.text.contains(QLatin1Char('\n'))) {
                    continue;
                }
                appendRaw(child.text, chain);
            } else if (child.is(kText, u"s")) {
                const int count = qMax(1, child.attr(kText, u"c", QStringLiteral("1")).toInt());
                appendLiteral(QString(qMin(count, 10000), QLatin1Char(' ')), chain);
            } else if (child.is(kText, u"tab")) {
                appendLiteral(QStringLiteral("\t"), chain);
            } else if (child.is(kText, u"line-break")) {
                appendLiteral(QString(QChar(QChar::LineSeparator)), chain);
            } else if (child.is(kText, u"span")) {
                Chain inner = chain;
                inner.legacy.prepend(&child);
                const QString style = child.attr(kText, u"style-name");
                if (!style.isEmpty()) {
                    inner.styles.prepend({style, QStringLiteral("text")});
                }
                startRun(inner);
                walk(child, inner);
                m_current = -1; // text after the span starts a new run
            } else if (child.is(kText, u"note") || child.is(kOffice, u"annotation")) {
                continue;
            } else {
                walk(child, chain); // text:a, fields, bookmarks' content
            }
        }
    }

    QList<TextRun> finish()
    {
        if (m_rawTrailingSpaceRun >= 0) {
            QString &text = m_runs[m_rawTrailingSpaceRun].text;
            if (text.endsWith(QLatin1Char(' '))) {
                text.chop(1);
            }
        }
        return m_runs;
    }

private:
    void startRun(const Chain &chain)
    {
        m_runs.append(formattedRun(m_sheet, chain));
        m_current = int(m_runs.size()) - 1;
    }

    TextRun &current(const Chain &chain)
    {
        if (m_current < 0) {
            startRun(chain);
        }
        return m_runs[m_current];
    }

    void appendRaw(const QString &text, const Chain &chain)
    {
        for (const QChar c : text) {
            if (c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\n')
                || c == QLatin1Char('\r')) {
                if (m_previousSpace) {
                    continue;
                }
                current(chain).text += QLatin1Char(' ');
                m_previousSpace = true;
                m_rawTrailingSpaceRun = m_current;
            } else {
                current(chain).text += c;
                m_previousSpace = false;
                m_rawTrailingSpaceRun = -1;
            }
        }
    }

    void appendLiteral(const QString &text, const Chain &chain)
    {
        current(chain).text += text;
        m_previousSpace = true;
        m_rawTrailingSpaceRun = -1;
    }

    const StyleSheet &m_sheet;
    QList<TextRun> m_runs;
    int m_current = -1;
    bool m_previousSpace = true; // leading white space is dropped
    int m_rawTrailingSpaceRun = -1;
};

struct ListContext {
    int depth = 0;           // 0 = not in a list
    QString style;           // the governing list style
    bool header = false;     // text:list-header: no label
};

void readBlock(const OdfNode &node, const StyleSheet &sheet, const TextDefaults &defaults, const ListContext &list,
               QList<TextParagraph> &out)
{
    using namespace Odf;
    for (const OdfNode *child : node.elements()) {
        if (child->is(kText, u"p") || child->is(kText, u"h")) {
            TextParagraph paragraph;
            const QString style = child->attr(kText, u"style-name");
            Chain chain;
            if (!style.isEmpty()) {
                chain.styles.append({style, QStringLiteral("paragraph")});
            }
            if (!defaults.textStyle.isEmpty()) {
                chain.styles.append({defaults.textStyle, QStringLiteral("paragraph")});
            }
            if (!defaults.graphicStyle.isEmpty()) {
                chain.styles.append({defaults.graphicStyle, defaults.graphicFamily});
            }
            // Alignment: the exact model value, the ODF one, the legacy
            // attribute on text:p, the shape's text style.
            QString exact = sheet.property(style, QStringLiteral("paragraph"), u"paragraph-properties", kQq, u"align");
            if (!exact.isEmpty()) {
                paragraph.alignment = exact.toInt();
            } else {
                QString align = sheet.property(style, QStringLiteral("paragraph"), u"paragraph-properties", kFo,
                                               u"text-align");
                if (align.isEmpty()) {
                    align = child->attr(kFo, u"text-align");
                }
                if (align.isEmpty() && !defaults.textStyle.isEmpty()) {
                    align = sheet.property(defaults.textStyle, QStringLiteral("paragraph"), u"paragraph-properties",
                                           kFo, u"text-align");
                }
                paragraph.alignment = alignmentFrom(align);
            }
            if (list.depth > 0) {
                paragraph.listLevel = list.depth - 1;
                const QString kind = sheet.listLevelKind(list.style, list.depth);
                paragraph.bullet = !list.header && kind != u"none";
            } else if (child->hasAttr(kQq, u"level")) {
                paragraph.listLevel = qBound(0, child->attr(kQq, u"level").toInt(), 8);
            } else if (style.startsWith(QLatin1String("qq-list-"))) {
                paragraph.bullet = true; // pre-M62 QindaShow bullets
                paragraph.listLevel = qMax(0, style.mid(8).toInt() - 1);
            }
            ParagraphBuilder builder(sheet);
            builder.walk(*child, chain);
            paragraph.runs = builder.finish();
            out.append(paragraph);
        } else if (child->is(kText, u"list")) {
            ListContext inner = list;
            inner.depth = list.depth + 1;
            const QString style = child->attr(kText, u"style-name");
            if (!style.isEmpty()) {
                inner.style = style;
            }
            inner.header = false;
            readBlock(*child, sheet, defaults, inner, out);
        } else if (child->is(kText, u"list-item") || child->is(kText, u"list-header")) {
            ListContext inner = list;
            inner.header = child->is(kText, u"list-header");
            readBlock(*child, sheet, defaults, inner, out);
        }
    }
}

void collectPlain(const OdfNode &node, QString &line)
{
    using namespace Odf;
    for (const Odf::OdfNodePtr &child : node.children) {
        if (child->isText()) {
            if (!(child->text.trimmed().isEmpty() && child->text.contains(QLatin1Char('\n')))) {
                line += child->text;
            }
        } else if (child->is(kText, u"s")) {
            line += QString(qMax(1, child->attr(kText, u"c", QStringLiteral("1")).toInt()), QLatin1Char(' '));
        } else if (child->is(kText, u"tab")) {
            line += QLatin1Char('\t');
        } else if (child->is(kText, u"line-break")) {
            line += QLatin1Char('\n');
        } else {
            collectPlain(*child, line);
        }
    }
}

void collectParagraphs(const OdfNode &node, QStringList &lines)
{
    using namespace Odf;
    for (const OdfNode *child : node.elements()) {
        if (child->is(kText, u"p") || child->is(kText, u"h")) {
            QString line;
            collectPlain(*child, line);
            lines.append(line);
        } else {
            collectParagraphs(*child, lines);
        }
    }
}

} // namespace

QList<TextParagraph> readParagraphs(const Odf::OdfNode &container, const StyleSheet &sheet,
                                   const TextDefaults &defaults)
{
    QList<TextParagraph> out;
    readBlock(container, sheet, defaults, ListContext{}, out);
    return out;
}

QString readPlainText(const Odf::OdfNode &container)
{
    QStringList lines;
    collectParagraphs(container, lines);
    return lines.join(QLatin1Char('\n'));
}

} // namespace QindaOffice::Show::Fodp
