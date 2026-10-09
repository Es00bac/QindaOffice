// SPDX-License-Identifier: GPL-3.0-or-later
// Word's list numbers: counting, the marker's look and place, drawing, and
// the first line's start after the marker (docx_list_labels.h).

#include "docx_list_labels.h"

#include "docx_format.h"
#include "docx_lists.h"
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
#include <QTextLayout>
#include <QTextList>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

// The markers of one document, valid until its next change.
class MarkerCache final : public QObject {
    Q_OBJECT
public:
    explicit MarkerCache(QTextDocument *document)
        : QObject(document)
    {
        setObjectName(QStringLiteral("qqoListMarkers"));
        connect(document, &QTextDocument::contentsChange, this, [this] { valid = false; });
    }
    QHash<int, ListMarker> markers;
    bool valid = false;
};

// What a level's w:rPr says about the number's look, over the paragraph
// mark's format (Word formats the number like the mark, then the level).
void overlayLevelRun(QTextCharFormat &f, const XmlElement *rPr)
{
    if (rPr == nullptr) {
        return;
    }
    if (const QString family = latinFont(rPr->child(kW, u"rFonts"), Ooxml::Theme()); !family.isEmpty()) {
        f.setFontFamilies({family});
    }
    if (const QString sz = childVal(rPr, u"sz"); !sz.isEmpty() && sz.toDouble() > 0) {
        f.setFontPointSize(sz.toDouble() / 2.0);
    }
    if (rPr->child(kW, u"b") != nullptr) {
        f.setFontWeight(onOff(rPr, u"b") ? QFont::Bold : QFont::Normal);
    }
    if (rPr->child(kW, u"i") != nullptr) {
        f.setFontItalic(onOff(rPr, u"i"));
    }
    if (const XmlElement *u = rPr->child(kW, u"u")) {
        f.setFontUnderline(wAttr(u, u"val", QStringLiteral("single")) != u"none");
    }
    if (const QColor color = wordColor(rPr->child(kW, u"color"), Ooxml::Theme()); color.isValid()) {
        f.setForeground(color);
    }
}

QString fontOf(const XmlElement *rPr)
{
    const XmlElement *fonts = rPr != nullptr ? rPr->child(kW, u"rFonts") : nullptr;
    return wAttr(fonts, u"ascii", wAttr(fonts, u"hAnsi"));
}

struct Counters {
    std::array<int, 9> value{};
    bool started = false;
};

QHash<int, ListMarker> computeMarkers(const QTextDocument &document)
{
    QHash<int, ListMarker> out;
    QHash<QString, Counters> sets;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const QTextList *list = block.textList();
        if (list == nullptr || !isWordList(list->format())) {
            continue;
        }
        const QTextListFormat lf = list->format();
        const int numId = lf.intProperty(kListNumId);
        const int level = qBound(0, lf.intProperty(kListLevel), 8);
        const std::shared_ptr<const ListDefinition> definition = listDefinition(lf.stringProperty(kListDefinition));
        const QList<int> starts = listStarts(lf);
        const bool own = std::any_of(starts.cbegin(), starts.cend(), [](int s) { return s >= 0; });
        const QString abstractKey = lf.stringProperty(kListAbstractKey);
        // Word: instances of one definition count together, unless one
        // overrides a start — that one keeps its own count.
        Counters &c = sets[own || abstractKey.isEmpty() ? QStringLiteral("n:%1").arg(numId) : abstractKey];
        const auto startOf = [&](int k) { return starts.at(k) >= 0 ? starts.at(k) : definition->levels[size_t(k)].start; };
        if (!c.started) {
            for (int k = 0; k < 9; ++k) {
                c.value[size_t(k)] = startOf(k) - 1;
            }
            c.started = true;
        }
        c.value[size_t(level)] += 1;
        for (int deeper = level + 1; deeper < 9; ++deeper) {
            const int after = definition->levels[size_t(deeper)].restartAfterLevel;
            if (after != -1 && (after == -2 || level <= after)) {
                c.value[size_t(deeper)] = startOf(deeper) - 1;
            }
        }
        const NumberingLevel &l = definition->levels[size_t(level)];
        ListMarker m;
        m.numId = numId;
        m.level = level;
        m.value = c.value[size_t(level)];
        m.justification = l.justification.isEmpty() ? QStringLiteral("left") : l.justification;
        m.suffix = l.suffix;
        const QTextBlockFormat bf = block.blockFormat();
        m.hanging = bf.hasProperty(kListHanging) ? bf.doubleProperty(kListHanging) : Units::twipsToPx(levelHanging(l));
        QTextCharFormat format = block.charFormat();
        overlayLevelRun(format, l.rPr.get());
        m.text = definition->defined[size_t(level)] ? levelLabel(*definition, level, c.value) : QString();
        if (l.isBullet()) {
            const QString shown = displayGlyph(m.text, fontOf(l.rPr.get()));
            if (shown != m.text) {
                // The symbol font is rarely installed: its look-alike in the paragraph's font.
                format.clearProperty(QTextFormat::FontFamilies);
                format.clearProperty(QTextFormat::FontFamily);
                if (const QStringList own = block.charFormat().fontFamilies().toStringList(); !own.isEmpty()) {
                    format.setFontFamilies(own);
                }
            }
            m.text = shown;
        }
        m.format = format;
        out.insert(block.blockNumber(), m);
    }
    return out;
}

QFont markerFont(const ListMarker &marker, const QTextDocument &document)
{
    QFont font = marker.format.font().resolve(document.defaultFont());
    if (marker.format.fontFamilies().toStringList().isEmpty()) {
        font.setFamilies(document.defaultFont().families());
    }
    return font;
}

} // namespace

QString displayGlyph(const QString &text, const QString &font)
{
    if (text.size() != 1) {
        return text;
    }
    const char16_t code = text.at(0).unicode();
    const bool privateUse = code >= 0xF000 && code <= 0xF0FF;
    const QString name = font.toLower();
    const bool wingdings = name.startsWith(u"wingdings");
    const bool symbolFont = wingdings || name == u"symbol" || name == u"webdings";
    if (!privateUse && !symbolFont) {
        return text;
    }
    const int low = code & 0xFF;
    static const QHash<int, char16_t> kSymbol{{0xB7, 0x2022}, {0xA7, 0x2663}, {0xA8, 0x2666}, {0xA9, 0x2665},
                                              {0xAA, 0x2660}, {0xAE, 0x2192}, {0xDE, 0x21D2}, {0x2D, 0x2212},
                                              {0xD7, 0x22C5}, {0xE0, 0x25CA}};
    static const QHash<int, char16_t> kWingdings{
        {0x6C, 0x25CF}, {0x6E, 0x25A0}, {0x6F, 0x25A1}, {0x71, 0x2751}, {0x72, 0x2752}, {0x75, 0x25C6},
        {0x76, 0x2756}, {0x77, 0x2B25}, {0xA7, 0x25AA}, {0xA8, 0x25FB}, {0xD8, 0x27A2}, {0xE8, 0x2794},
        {0xF0, 0x21E8}, {0xFB, 0x2717}, {0xFC, 0x2713}, {0xFD, 0x2612}, {0xFE, 0x2611}, {0x46, 0x261E},
        {0x4A, 0x263A}, {0x9F, 0x2022}, {0xA1, 0x25CB}, {0xA2, 0x25CB}};
    const QHash<int, char16_t> &table = wingdings ? kWingdings : kSymbol;
    if (const auto it = table.constFind(low); it != table.constEnd()) {
        return QString(QChar(it.value()));
    }
    if (!privateUse && low < 0x80 && !wingdings && name == u"symbol") {
        return text; // Symbol's ASCII letters are Greek; shown as typed
    }
    return QString(QChar(0x2022));
}

QHash<int, ListMarker> listMarkers(const QTextDocument &document, bool fresh)
{
    if (fresh) {
        return computeMarkers(document);
    }
    auto *cache = document.findChild<MarkerCache *>(QStringLiteral("qqoListMarkers"), Qt::FindDirectChildrenOnly);
    if (cache == nullptr) {
        cache = new MarkerCache(const_cast<QTextDocument *>(&document));
    }
    if (!cache->valid) {
        cache->markers = computeMarkers(document);
        cache->valid = true;
    }
    return cache->markers;
}

ListMarker listMarker(const QTextBlock &block)
{
    if (!block.isValid() || block.document() == nullptr || block.textList() == nullptr) {
        return {};
    }
    return listMarkers(*block.document()).value(block.blockNumber());
}

void drawListMarkers(QPainter *painter, const QTextDocument &document, const QRectF &clip)
{
    const QHash<int, ListMarker> markers = listMarkers(document);
    if (markers.isEmpty()) {
        return;
    }
    QAbstractTextDocumentLayout *layout = document.documentLayout();
    painter->save();
    for (auto it = markers.cbegin(); it != markers.cend(); ++it) {
        const ListMarker &marker = it.value();
        const QTextBlock block = document.findBlockByNumber(it.key());
        const QTextLayout *lines = block.isValid() ? block.layout() : nullptr;
        if (marker.text.isEmpty() || lines == nullptr || lines->lineCount() == 0 || !block.isVisible()) {
            continue;
        }
        const QRectF rect = layout->blockBoundingRect(block);
        if (!rect.intersects(clip)) {
            continue;
        }
        const QTextLine first = lines->lineAt(0);
        // Laid out like Qt lays out its own list numbers, on the layout's
        // device, so screen, paper and PDF agree.
        QTextLayout label(marker.text, markerFont(marker, document), layout->paintDevice());
        label.beginLayout();
        QTextLine line = label.createLine();
        label.endLayout();
        if (!line.isValid()) {
            continue;
        }
        const qreal width = line.naturalTextWidth();
        const qreal position = rect.left() + first.x() - block.blockFormat().textIndent() - marker.hanging;
        const qreal x = marker.justification == u"right" ? position - width
            : marker.justification == u"center"          ? position - width / 2
                                                         : position;
        const qreal y = rect.top() + first.y() + first.ascent() - line.ascent();
        const QBrush ink = marker.format.foreground();
        painter->setPen(ink.style() != Qt::NoBrush ? ink.color() : QColor(Qt::black));
        label.draw(painter, QPointF(x, y));
    }
    painter->restore();
}

qreal markerTextOffset(const QTextBlock &block, const ListMarker &marker, qreal defaultTab)
{
    if (!marker.isValid() || block.document() == nullptr) {
        return 0;
    }
    const QTextBlockFormat bf = block.blockFormat();
    const qreal left = bf.leftMargin();
    const qreal number = left - marker.hanging;
    const QFontMetricsF metrics(markerFont(marker, *block.document()));
    const qreal width = marker.text.isEmpty() ? 0.0 : metrics.horizontalAdvance(marker.text);
    const qreal start = marker.justification == u"right" ? number - width
        : marker.justification == u"center"             ? number - width / 2
                                                         : number;
    const qreal end = start + width;
    if (marker.suffix == u"nothing") {
        return end - left;
    }
    if (marker.suffix == u"space") {
        return end + metrics.horizontalAdvance(QLatin1Char(' ')) - left;
    }
    // A tab: to the left indent (Word's hanging indent is a stop) when the
    // number ends before it, else to the next stop.
    if (marker.hanging > 0.5 && end <= left + 0.5) {
        return 0;
    }
    for (const TabStop &stop : tabStops(bf)) {
        const qreal at = Units::twipsToPx(stop.position);
        if (at > end + 0.5 && stop.alignment != u"bar") {
            return at - left;
        }
    }
    const qreal step = defaultTab > 1 ? defaultTab : 48.0;
    return std::ceil((end + 0.5) / step) * step - left;
}

int layoutListText(QTextDocument *document, qreal defaultTab)
{
    int changed = 0;
    const QHash<int, ListMarker> markers = listMarkers(*document, true);
    for (auto it = markers.cbegin(); it != markers.cend(); ++it) {
        const QTextBlock block = document->findBlockByNumber(it.key());
        const qreal offset = markerTextOffset(block, it.value(), defaultTab);
        QTextBlockFormat f = block.blockFormat();
        if (qAbs(f.textIndent() - offset) > 0.25) {
            f.setTextIndent(offset);
            QTextCursor(block).setBlockFormat(f);
            ++changed;
        }
    }
    return changed;
}

} // namespace QindaOffice::Docx

#include "docx_list_labels.moc"
