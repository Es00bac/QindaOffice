// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_paragraph_format.h"

#include "docx_properties.h"
#include "docx_styles.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextTable>

#include <algorithm>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

// The paragraph beside `block` in the same frame (a table between them, or
// a frame edge, means none: the reader then kept the space as the block's
// own margin).
QTextBlock sibling(const QTextBlock &block, bool next)
{
    const QTextBlock other = next ? block.next() : block.previous();
    if (!other.isValid() || QTextCursor(other).currentFrame() != QTextCursor(block).currentFrame()) {
        return {};
    }
    // Every cell of a table is the same frame: the paragraph across a cell
    // edge is not a neighbour either.
    if (const QTextTable *table = QTextCursor(block).currentTable();
        table != nullptr && table->cellAt(other.position()) != table->cellAt(block.position())) {
        return {};
    }
    return other;
}

qreal beforePx(const OwnSpacing &s)
{
    return s.autoBefore ? Units::ptToPx(14) : Units::twipsToPx(s.before);
}

qreal afterPx(const OwnSpacing &s)
{
    return s.autoAfter ? Units::ptToPx(14) : Units::twipsToPx(s.after);
}

bool sameStyle(const QTextBlock &a, const QTextBlock &b)
{
    return a.isValid() && b.isValid()
        && a.blockFormat().stringProperty(kParagraphStyleId) == b.blockFormat().stringProperty(kParagraphStyleId);
}

} // namespace

// ---- tab stops --------------------------------------------------------------------

QString tabStopsSpec(QList<TabStop> stops)
{
    std::stable_sort(stops.begin(), stops.end(), [](const TabStop &a, const TabStop &b) { return a.position < b.position; });
    QStringList entries;
    for (const TabStop &s : std::as_const(stops)) {
        entries.append(QStringLiteral("%1/%2/%3").arg(s.position).arg(s.alignment, s.leader.isEmpty() ? QStringLiteral("none") : s.leader));
    }
    return entries.join(QLatin1Char(';'));
}

QList<TabStop> parseTabStops(QStringView spec)
{
    QList<TabStop> out;
    for (const QStringView entry : spec.split(u';', Qt::SkipEmptyParts)) {
        const QList<QStringView> f = entry.split(u'/');
        if (f.size() == 3) {
            out.append({f[0].toInt(), f[1].toString(), f[2].toString()});
        }
    }
    return out;
}

QList<TabStop> readTabStops(const XmlElement *tabs)
{
    QList<TabStop> out;
    if (tabs == nullptr) {
        return out;
    }
    for (const XmlElement *tab : tabs->elements(kW, u"tab")) {
        QString val = wAttr(tab, u"val", QStringLiteral("left"));
        if (val == u"clear" || val == u"num") {
            continue;
        }
        val = val == u"start" ? QStringLiteral("left") : val == u"end" ? QStringLiteral("right") : val;
        out.append({qRound(Units::parseTwips(wAttr(tab, u"pos"))), val, wAttr(tab, u"leader", QStringLiteral("none"))});
    }
    std::stable_sort(out.begin(), out.end(), [](const TabStop &a, const TabStop &b) { return a.position < b.position; });
    return out;
}

QList<TabStop> tabStops(const QTextBlockFormat &format)
{
    if (format.hasProperty(kTabStops)) {
        return parseTabStops(format.stringProperty(kTabStops));
    }
    QList<TabStop> out;
    for (const QTextOption::Tab &t : format.tabPositions()) {
        const QString alignment = t.type == QTextOption::CenterTab ? QStringLiteral("center")
            : t.type == QTextOption::RightTab                      ? QStringLiteral("right")
            : t.type == QTextOption::DelimiterTab                  ? QStringLiteral("decimal")
                                                                   : QStringLiteral("left");
        out.append({qRound(Units::pxToTwips(t.position + format.leftMargin())), alignment, QStringLiteral("none")});
    }
    return out;
}

QList<QTextOption::Tab> qtTabs(const QList<TabStop> &stops, qreal leftPx)
{
    QList<QTextOption::Tab> out;
    for (const TabStop &s : stops) {
        // AGENT-NOTE: Word measures stops from the text column's edge, Qt
        // from the paragraph's left indent (docx_format.cpp, probed on Qt
        // 6.11); a bar tab is a drawn rule, not a stop.
        const qreal pos = Units::twipsToPx(s.position) - leftPx;
        if (s.alignment == u"bar" || pos <= 0) {
            continue;
        }
        QTextOption::TabType type = QTextOption::LeftTab;
        QChar delimiter;
        if (s.alignment == u"center") {
            type = QTextOption::CenterTab;
        } else if (s.alignment == u"right") {
            type = QTextOption::RightTab;
        } else if (s.alignment == u"decimal") {
            type = QTextOption::DelimiterTab;
            delimiter = QLatin1Char('.');
        }
        out.append(QTextOption::Tab(pos, type, delimiter));
    }
    return out;
}

void setTabStops(QTextBlockFormat &format, const QList<TabStop> &stops)
{
    // Kept even when empty: a paragraph whose style has stops must be able
    // to say "none" (the writer then clears the style's).
    format.setProperty(kTabStops, tabStopsSpec(stops));
    format.setTabPositions(qtTabs(stops, format.leftMargin()));
}

void drawTabLeaders(QPainter *painter, const QTextDocument &document, const QRectF &clip)
{
    QAbstractTextDocumentLayout *layout = document.documentLayout();
    painter->save();
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const QString spec = block.blockFormat().stringProperty(kTabStops);
        if (spec.isEmpty() || !spec.contains(QLatin1Char('/')) || !block.text().contains(QLatin1Char('\t'))) {
            continue;
        }
        const QList<TabStop> stops = parseTabStops(spec);
        if (std::none_of(stops.cbegin(), stops.cend(), [](const TabStop &s) { return s.leader != u"none"; })) {
            continue;
        }
        const QRectF rect = layout->blockBoundingRect(block);
        const QTextLayout *lines = block.layout();
        if (lines == nullptr || !rect.intersects(clip)) {
            continue;
        }
        const QString text = block.text();
        for (int i = 0; i < text.size(); ++i) {
            if (text.at(i) != QLatin1Char('\t')) {
                continue;
            }
            const QTextLine line = lines->lineForTextPosition(i);
            if (!line.isValid()) {
                continue;
            }
            const qreal x0 = line.cursorToX(i);
            const qreal x1 = line.cursorToX(i + 1);
            // The stop this tab went to: the first after where it started
            // (Qt's rule; a right or centred stop ends past x1).
            const auto stop = std::find_if(stops.cbegin(), stops.cend(), [&](const TabStop &s) {
                return s.alignment != u"bar" && Units::twipsToPx(s.position) > x0 + 0.5;
            });
            if (stop == stops.cend() || stop->leader == u"none" || x1 - x0 < 4) {
                continue;
            }
            QTextCursor at(block);
            at.setPosition(block.position() + i + 1);
            const QTextCharFormat format = at.charFormat();
            const QFont font = format.font().resolve(document.defaultFont());
            const QFontMetricsF metrics(font);
            const QColor ink = format.hasProperty(QTextFormat::ForegroundBrush) ? format.foreground().color() : QColor(Qt::black);
            const qreal baseline = rect.top() + line.y() + line.ascent();
            const qreal left = rect.left() + x0 + metrics.averageCharWidth() * 0.3;
            const qreal right = rect.left() + x1 - metrics.averageCharWidth() * 0.3;
            if (stop->leader == u"underscore" || stop->leader == u"heavy") {
                QPen pen(ink, stop->leader == u"heavy" ? qMax<qreal>(1.5, metrics.lineWidth() * 2) : qMax<qreal>(0.75, metrics.lineWidth()));
                pen.setCapStyle(Qt::FlatCap);
                painter->setPen(pen);
                const qreal y = baseline + metrics.underlinePos();
                painter->drawLine(QPointF(left, y), QPointF(right, y));
                continue;
            }
            // Dots, hyphens and middle dots as the paragraph's own glyphs,
            // aligned to the gap's end as Word aligns them to the text.
            const QChar glyph = stop->leader == u"hyphen" ? QLatin1Char('-')
                : stop->leader == u"middleDot"           ? QChar(0x00B7)
                                                         : QLatin1Char('.');
            const qreal advance = qMax<qreal>(1.0, metrics.horizontalAdvance(glyph) * (glyph == u'-' ? 1.0 : 1.6));
            const int count = int((right - left) / advance);
            if (count <= 0) {
                continue;
            }
            painter->setPen(ink);
            painter->setFont(font);
            for (int k = 0; k < count; ++k) {
                painter->drawText(QPointF(right - (k + 1) * advance, baseline), QString(glyph));
            }
        }
    }
    painter->restore();
}

// ---- spacing ----------------------------------------------------------------------

OwnSpacing ownSpacing(const QTextBlockFormat &format)
{
    OwnSpacing s;
    if (format.hasProperty(kParagraphSpacing)) {
        const QVariantList v = format.property(kParagraphSpacing).toList();
        if (v.size() == 4) {
            s.before = v.at(0).toInt();
            s.after = v.at(1).toInt();
            s.autoBefore = v.at(2).toBool();
            s.autoAfter = v.at(3).toBool();
        }
    } else {
        s.before = qRound(Units::pxToTwips(format.topMargin()));
        s.after = qRound(Units::pxToTwips(format.bottomMargin()));
    }
    s.contextual = format.boolProperty(kContextualSpacing);
    return s;
}

void setParagraphSpacing(const QTextBlock &block, const OwnSpacing &spacing)
{
    QTextBlockFormat format = block.blockFormat();
    const OwnSpacing old = ownSpacing(format);
    format.setProperty(kParagraphSpacing, QVariantList{spacing.before, spacing.after, spacing.autoBefore, spacing.autoAfter});
    if (spacing.contextual) {
        format.setProperty(kContextualSpacing, true);
    } else {
        format.clearProperty(kContextualSpacing);
    }
    if (!format.hasProperty(kParagraphSnapshot)) {
        // A paragraph Write made: its margins are its spacing.
        format.setTopMargin(beforePx(spacing));
        format.setBottomMargin(afterPx(spacing));
        QTextCursor(block).setBlockFormat(format);
        return;
    }
    // AGENT-NOTE: the reader folded Word's spacing (after + before; zero
    // for contextual spacing between paragraphs of one style) into the
    // next paragraph's top margin, plus display extras (an empty paragraph
    // Qt hid beside a table). Moving each gap by what Word's sum moves by
    // keeps those extras (docx_reader_body.cpp, readParagraph).
    const QTextBlock previous = sibling(block, false);
    const QTextBlock next = sibling(block, true);
    auto ownBefore = [&](const OwnSpacing &s) { return s.contextual && sameStyle(previous, block) ? 0.0 : beforePx(s); };
    auto ownAfter = [&](const OwnSpacing &s) { return s.contextual && sameStyle(block, next) ? 0.0 : afterPx(s); };
    format.setTopMargin(qMax<qreal>(0, format.topMargin() + ownBefore(spacing) - ownBefore(old)));
    if (!next.isValid()) {
        format.setBottomMargin(qMax<qreal>(0, format.bottomMargin() + ownAfter(spacing) - ownAfter(old)));
    }
    QTextCursor(block).setBlockFormat(format);
    if (next.isValid()) {
        QTextBlockFormat following = next.blockFormat();
        following.setTopMargin(qMax<qreal>(0, following.topMargin() + ownAfter(spacing) - ownAfter(old)));
        QTextCursor(next).setBlockFormat(following);
    }
}

// ---- borders and outline ------------------------------------------------------------

WordBorders paragraphBorders(const QTextBlockFormat &format)
{
    return parseBordersSpec(format.stringProperty(kParagraphBorderSpec));
}

void setParagraphBorders(QTextBlockFormat &format, const WordBorders &borders)
{
    format.setProperty(kParagraphBorderSpec, bordersSpec(borders));
    const QString display = paragraphDisplaySpec(borders);
    if (display.isEmpty()) {
        format.clearProperty(kParagraphBorders);
    } else {
        format.setProperty(kParagraphBorders, display);
    }
}

int outlineLevel(const QTextBlockFormat &format)
{
    if (format.hasProperty(kOutlineLevel)) {
        return qBound(0, format.intProperty(kOutlineLevel), 9);
    }
    return format.headingLevel() > 0 ? qMin(format.headingLevel() - 1, 8) : 9;
}

void setOutlineLevel(QTextBlockFormat &format, int level)
{
    level = qBound(0, level, 9);
    format.setProperty(kOutlineLevel, level);
    format.setHeadingLevel(level <= 5 ? level + 1 : 0);
}

} // namespace QindaOffice::Docx
