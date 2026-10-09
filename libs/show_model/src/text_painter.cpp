// SPDX-License-Identifier: GPL-3.0-or-later
#include "text_painter.h"

#include <QImage>
#include <QPainter>
#include <QTextLayout>

#include <array>
#include <memory>
#include <vector>

namespace QindaOffice::Show::Render {

namespace {

// AGENT-GUARD: every QTextLayout here is built on this 72-dpi device and
// with design metrics (see the header). A layout on the default (screen)
// device sizes 18 pt type as 24 slide points; QFont::resolve() hands the
// layout font's dpi to the run formats, which is why per-run formats stay
// right.
QPaintDevice *layoutDevice()
{
    static QImage device = [] {
        QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.setDotsPerMeterX(2835); // 72.009 dpi → logicalDpi 72
        image.setDotsPerMeterY(2835);
        return image;
    }();
    return &device;
}

QFont fontFor(const TextRun &run, qreal scale, qreal sizeOverride = 0)
{
    QFont font;
    if (!run.fontFamily.isEmpty()) {
        font.setFamilies({run.fontFamily});
    }
    font.setPointSizeF(qMax<qreal>(0.5, (sizeOverride > 0 ? sizeOverride : run.pointSize) * scale));
    font.setBold(run.bold);
    font.setItalic(run.italic);
    font.setStrikeOut(!run.strike.isEmpty() && run.strike != u"noStrike");
    if (run.caps == u"all") {
        font.setCapitalization(QFont::AllUppercase);
    } else if (run.caps == u"small") {
        font.setCapitalization(QFont::SmallCaps);
    }
    if (run.spacing != 0) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, run.spacing * scale);
    }
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

QTextCharFormat formatFor(const TextRun &run, qreal scale)
{
    QTextCharFormat format;
    format.setFont(fontFor(run, scale));
    format.setForeground(run.color.isValid() ? run.color : QColor(Qt::black));
    if (run.highlight.isValid() && run.highlight.alpha() > 0) {
        format.setBackground(run.highlight);
    }
    if (!run.underline.isEmpty() && run.underline != u"none") {
        QTextCharFormat::UnderlineStyle style = QTextCharFormat::SingleUnderline;
        if (run.underline.contains(QLatin1String("wavy"))) {
            style = QTextCharFormat::WaveUnderline;
        } else if (run.underline.contains(QLatin1String("dotDash"))) {
            style = QTextCharFormat::DashDotLine;
        } else if (run.underline.contains(QLatin1String("dot"))) {
            style = QTextCharFormat::DotLine;
        } else if (run.underline.contains(QLatin1String("dash"))) {
            style = QTextCharFormat::DashUnderline;
        }
        format.setUnderlineStyle(style);
    }
    if (run.baseline > 0) {
        format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    } else if (run.baseline < 0) {
        format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    }
    return format;
}

const TextRun &leadRun(const TextParagraph &paragraph)
{
    static const TextRun fallback;
    for (const TextRun &run : paragraph.runs) {
        if (!run.text.isEmpty()) {
            return run;
        }
    }
    return paragraph.runs.isEmpty() ? fallback : paragraph.runs.first();
}

Qt::Alignment alignmentOf(int alignment)
{
    if (alignment & int(Qt::AlignHCenter)) {
        return Qt::AlignHCenter;
    }
    if (alignment & int(Qt::AlignJustify)) {
        return Qt::AlignJustify;
    }
    if (alignment & int(Qt::AlignRight)) {
        return Qt::AlignRight;
    }
    return Qt::AlignLeft;
}

qreal spacingPoints(const Spacing &spacing, qreal fontSize)
{
    return spacing.unit == Spacing::Unit::Points ? spacing.value : fontSize * spacing.value / 100.0;
}

struct Block {
    std::unique_ptr<QTextLayout> layout;
    std::unique_ptr<QTextLayout> bullet;
    qreal bulletX = 0;
    qreal spaceBefore = 0;
    qreal spaceAfter = 0;
    qreal height = 0;
};

std::unique_ptr<QTextLayout> bulletLayout(const TextParagraph &paragraph, const QString &label, qreal scale)
{
    const TextRun &lead = leadRun(paragraph);
    TextRun look = lead;
    if (!paragraph.bulletFont.isEmpty()) {
        look.fontFamily = paragraph.bulletFont;
    }
    look.bold = paragraph.autoNumber.isEmpty() ? false : lead.bold;
    look.italic = false;
    look.underline.clear();
    look.strike.clear();
    look.caps.clear();
    const qreal size = qMax<qreal>(1.0, lead.pointSize * paragraph.bulletSize / 100.0);
    auto layout = std::make_unique<QTextLayout>(label, fontFor(look, scale, size), layoutDevice());
    QTextCharFormat format;
    format.setForeground(paragraph.bulletColor.isValid() ? paragraph.bulletColor
                                                         : (lead.color.isValid() ? lead.color : QColor(Qt::black)));
    layout->setFormats({{0, int(label.size()), format}});
    QTextOption option;
    option.setUseDesignMetrics(true);
    option.setWrapMode(QTextOption::NoWrap);
    layout->setTextOption(option);
    layout->beginLayout();
    QTextLine line = layout->createLine();
    if (line.isValid()) {
        line.setLineWidth(1e6);
    }
    layout->endLayout();
    return layout;
}

Block layoutParagraph(const TextParagraph &paragraph, qreal width, const TextBodyStyle &style, qreal scale,
                      qreal reduction, const QString &label)
{
    Block block;
    const TextRun &lead = leadRun(paragraph);
    QString text;
    QList<QTextLayout::FormatRange> formats;
    for (const TextRun &run : paragraph.runs) {
        if (run.text.isEmpty()) {
            continue;
        }
        QString piece = run.text;
        piece.replace(QLatin1Char('\n'), QChar(QChar::LineSeparator));
        formats.append({int(text.size()), int(piece.size()), formatFor(run, scale)});
        text += piece;
    }
    const qreal size = text.isEmpty() && paragraph.endPointSize > 0 ? paragraph.endPointSize : lead.pointSize;
    block.layout = std::make_unique<QTextLayout>(text, fontFor(lead, scale, size), layoutDevice());
    QTextOption option;
    option.setAlignment(alignmentOf(paragraph.alignment));
    option.setWrapMode(style.wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    option.setUseDesignMetrics(true);
    option.setTabStopDistance(72.0);
    block.layout->setTextOption(option);
    block.layout->setFormats(formats);

    qreal firstX = paragraph.marginLeft + paragraph.indent;
    if (paragraph.bullet && !label.isEmpty()) {
        block.bullet = bulletLayout(paragraph, label, scale);
        block.bulletX = paragraph.marginLeft + paragraph.indent;
        // AGENT-GUARD: the label's NATURAL width — its line is laid out
        // 1e6 pt wide (no wrapping), and boundingRect() reports that.
        const qreal bulletEnd = block.bulletX + block.bullet->lineAt(0).naturalTextWidth();
        const qreal gap = size * scale * 0.3;
        firstX = paragraph.indent < 0 ? qMax(paragraph.marginLeft, bulletEnd + gap) : bulletEnd + gap;
    }
    const qreal fontSize = size * scale;
    block.spaceBefore = spacingPoints(paragraph.spaceBefore, fontSize);
    block.spaceAfter = spacingPoints(paragraph.spaceAfter, fontSize);

    block.layout->beginLayout();
    qreal y = 0;
    bool first = true;
    for (;;) {
        QTextLine line = block.layout->createLine();
        if (!line.isValid()) {
            break;
        }
        const qreal x = first ? firstX : paragraph.marginLeft;
        line.setLineWidth(qMax<qreal>(1.0, width - x));
        const qreal natural = line.ascent() + line.descent();
        qreal height = natural;
        if (paragraph.lineSpacing.unit == Spacing::Unit::Points && paragraph.lineSpacing.value > 0) {
            height = paragraph.lineSpacing.value;
        } else if (paragraph.lineSpacing.unit == Spacing::Unit::Percent && paragraph.lineSpacing.value > 0) {
            height = natural * paragraph.lineSpacing.value / 100.0;
        }
        height *= (1.0 - reduction);
        // Extra spacing goes above the line, as PowerPoint puts it.
        line.setPosition(QPointF(x, y + height - natural));
        y += height;
        first = false;
    }
    block.layout->endLayout();
    block.height = y;
    return block;
}

std::vector<Block> layoutAll(const QList<TextParagraph> &paragraphs, qreal width, const TextBodyStyle &style)
{
    const qreal scale = style.autoFit == AutoFit::Normal ? qBound(0.01, style.fontScale / 100.0, 4.0) : 1.0;
    const qreal reduction = style.autoFit == AutoFit::Normal ? qBound(0.0, style.lineSpaceReduction / 100.0, 0.9) : 0.0;
    std::array<int, 9> counters{};
    std::array<QString, 9> schemes{};
    std::vector<Block> blocks;
    for (const TextParagraph &paragraph : paragraphs) {
        const int level = qBound(0, paragraph.listLevel, 8);
        QString label;
        if (paragraph.bullet && !paragraph.autoNumber.isEmpty()) {
            if (schemes[level] == paragraph.autoNumber && counters[level] > 0) {
                ++counters[level];
            } else {
                counters[level] = paragraph.autoNumberStart;
                schemes[level] = paragraph.autoNumber;
            }
            label = autoNumberLabel(paragraph.autoNumber, counters[level]);
        } else {
            counters[level] = 0;
            schemes[level].clear();
            if (paragraph.bullet) {
                label = paragraph.bulletChar.isEmpty() ? QStringLiteral("•") : paragraph.bulletChar;
            }
        }
        for (int deeper = level + 1; deeper < 9; ++deeper) {
            counters[deeper] = 0;
            schemes[deeper].clear();
        }
        blocks.push_back(layoutParagraph(paragraph, width, style, scale, reduction, label));
    }
    return blocks;
}

qreal totalHeight(const std::vector<Block> &blocks)
{
    qreal total = 0;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        // PowerPoint adds no space above the first paragraph nor below the last.
        total += (i > 0 ? blocks.at(i).spaceBefore : 0) + blocks.at(i).height
            + (i + 1 < blocks.size() ? blocks.at(i).spaceAfter : 0);
    }
    return total;
}

bool isVertical(const QString &vertical)
{
    return !vertical.isEmpty() && vertical != u"horz";
}

} // namespace

void paintText(QPainter *painter, const QRectF &box, const QList<TextParagraph> &paragraphs, const TextBodyStyle &style)
{
    if (paragraphs.isEmpty()) {
        return;
    }
    QRectF inner = box.normalized().adjusted(style.insetLeft, style.insetTop, -style.insetRight, -style.insetBottom);
    painter->save();
    if (isVertical(style.vertical)) {
        // Turn the page under the text: top-to-bottom for "vert", bottom-to-top for "vert270".
        const bool up = style.vertical == u"vert270";
        painter->translate(up ? inner.bottomLeft() : inner.topRight());
        painter->rotate(up ? -90 : 90);
        inner = QRectF(0, 0, inner.height(), inner.width());
    }
    const std::vector<Block> blocks = layoutAll(paragraphs, inner.width(), style);
    const qreal total = totalHeight(blocks);
    qreal y = inner.top();
    if (style.anchor == TextAnchor::Middle || style.anchor == TextAnchor::Distributed) {
        y += (inner.height() - total) / 2.0;
    } else if (style.anchor == TextAnchor::Bottom) {
        y += inner.height() - total;
    }
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const Block &block = blocks.at(i);
        if (i > 0) {
            y += block.spaceBefore;
        }
        block.layout->draw(painter, QPointF(inner.left(), y));
        if (block.bullet && block.layout->lineCount() > 0) {
            // On the first line's baseline.
            const QTextLine first = block.layout->lineAt(0);
            const QTextLine mark = block.bullet->lineAt(0);
            const qreal baseline = y + first.y() + first.ascent();
            block.bullet->draw(painter, QPointF(inner.left() + block.bulletX, baseline - mark.ascent()));
        }
        y += block.height + block.spaceAfter;
    }
    painter->restore();
}

qreal textHeight(const QList<TextParagraph> &paragraphs, qreal width, const TextBodyStyle &style)
{
    const qreal inner = qMax<qreal>(1.0, width - style.insetLeft - style.insetRight);
    return totalHeight(layoutAll(paragraphs, inner, style)) + style.insetTop + style.insetBottom;
}

QString autoNumberLabel(const QString &scheme, int number)
{
    const auto roman = [](int n, bool upper) {
        static const std::pair<int, const char *> table[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"},
                                                             {100, "c"},  {90, "xc"},  {50, "l"},  {40, "xl"},
                                                             {10, "x"},   {9, "ix"},   {5, "v"},   {4, "iv"},
                                                             {1, "i"}};
        QString out;
        for (const auto &[value, digits] : table) {
            while (n >= value) {
                out += QLatin1String(digits);
                n -= value;
            }
        }
        return upper ? out.toUpper() : out;
    };
    const auto alpha = [](int n, bool upper) {
        // a..z, then aa..zz, as PowerPoint counts.
        const int letter = (n - 1) % 26;
        const int repeat = (n - 1) / 26 + 1;
        const QChar c = QChar((upper ? u'A' : u'a') + letter);
        return QString(repeat, c);
    };
    number = qMax(1, number);
    QString body;
    if (scheme.startsWith(QLatin1String("alphaLc"))) {
        body = alpha(number, false);
    } else if (scheme.startsWith(QLatin1String("alphaUc"))) {
        body = alpha(number, true);
    } else if (scheme.startsWith(QLatin1String("romanLc"))) {
        body = roman(number, false);
    } else if (scheme.startsWith(QLatin1String("romanUc"))) {
        body = roman(number, true);
    } else if (scheme.startsWith(QLatin1String("circleNum")) && number <= 20) {
        return QString(QChar(0x2460 + number - 1));
    } else {
        body = QString::number(number);
    }
    if (scheme.endsWith(QLatin1String("ParenBoth"))) {
        return QLatin1Char('(') + body + QLatin1Char(')');
    }
    if (scheme.endsWith(QLatin1String("ParenR"))) {
        return body + QLatin1Char(')');
    }
    if (scheme.endsWith(QLatin1String("Plain"))) {
        return body;
    }
    if (scheme.endsWith(QLatin1String("Minus"))) {
        return QStringLiteral("- ") + body + QStringLiteral(" -");
    }
    return body + QLatin1Char('.');
}

} // namespace QindaOffice::Show::Render
