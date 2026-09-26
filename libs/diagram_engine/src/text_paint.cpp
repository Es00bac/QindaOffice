// SPDX-License-Identifier: GPL-3.0-or-later
#include "text_paint.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QStringList>

#include <algorithm>

namespace QindaOffice::Diagram::TextPaint {

namespace {

constexpr double kReference = 100.0; // px, the layout size

int qtAlign(Qt::Alignment align)
{
    return int(align & (Qt::AlignHorizontal_Mask | Qt::AlignVertical_Mask));
}

} // namespace

QFont referenceFont(const TextStyle &style, bool bold, double sizeFactor)
{
    Q_UNUSED(sizeFactor)
    QFont font(style.family.isEmpty() ? QStringLiteral("Inter") : style.family);
    font.setPixelSize(int(kReference));
    font.setBold(bold);
    font.setItalic(style.italic);
    font.setUnderline(style.underline);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setKerning(true);
    return font;
}

double pageScale(const TextStyle &style, double sizeFactor)
{
    const double pagePixels = std::max(1.0, style.size) * sizeFactor * 96.0 / 72.0;
    return pagePixels / kReference;
}

double lineHeight(const TextStyle &style, double sizeFactor)
{
    return QFontMetricsF(referenceFont(style, style.bold, sizeFactor)).lineSpacing() * pageScale(style, sizeFactor);
}

QSizeF measure(const QString &text, const TextStyle &style)
{
    const QFontMetricsF metrics(referenceFont(style, style.bold));
    const double k = pageScale(style);
    double width = 0;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        width = std::max(width, metrics.horizontalAdvance(line));
    }
    return {width * k, metrics.lineSpacing() * k * double(lines.size())};
}

void drawBlock(QPainter *painter, const QString &text, const TextStyle &style, const QRectF &box)
{
    if (text.isEmpty() || box.width() <= 0) {
        return;
    }
    const double k = pageScale(style);
    painter->save();
    painter->translate(box.topLeft());
    painter->scale(k, k);
    painter->setFont(referenceFont(style, style.bold));
    painter->setPen(style.color);
    painter->drawText(QRectF(0, 0, box.width() / k, box.height() / k), qtAlign(style.align) | Qt::TextWordWrap, text);
    painter->restore();
}

void drawCard(QPainter *painter, const QString &text, const TextStyle &style, const QRectF &box)
{
    const qsizetype split = text.indexOf(QLatin1Char('\n'));
    const QString name = split < 0 ? text : text.left(split);
    const QString rest = split < 0 ? QString() : text.mid(split + 1);
    TextStyle nameStyle = style;
    nameStyle.bold = true;
    nameStyle.size = style.size * 1.15;
    nameStyle.align = (style.align & Qt::AlignHorizontal_Mask) | Qt::AlignTop;
    TextStyle restStyle = style;
    restStyle.size = style.size * 0.95;
    restStyle.align = (style.align & Qt::AlignHorizontal_Mask) | Qt::AlignTop;
    QColor muted = style.color;
    muted.setAlphaF(style.color.alphaF() * 0.75);
    restStyle.color = muted;
    const double nameHeight = lineHeight(nameStyle);
    const double restHeight = rest.isEmpty() ? 0 : lineHeight(restStyle) * double(rest.count(QLatin1Char('\n')) + 1);
    const double total = nameHeight + restHeight;
    double top = box.top();
    if (style.align & Qt::AlignVCenter) {
        top = box.center().y() - total / 2;
    } else if (style.align & Qt::AlignBottom) {
        top = box.bottom() - total;
    }
    // A card is one line per field: long names and titles elide rather than
    // wrap into the next line.
    const auto elided = [&](const QString &text, const TextStyle &style) {
        const double k = pageScale(style);
        const QFontMetricsF metrics(referenceFont(style, style.bold));
        QStringList lines;
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            lines.append(metrics.elidedText(line, Qt::ElideRight, box.width() / k));
        }
        return lines.join(QLatin1Char('\n'));
    };
    drawBlock(painter, elided(name, nameStyle), nameStyle, QRectF(box.left(), top, box.width(), nameHeight + 1));
    if (!rest.isEmpty()) {
        drawBlock(painter, elided(rest, restStyle), restStyle,
                  QRectF(box.left(), top + nameHeight, box.width(), restHeight + 1));
    }
}

void drawClass(QPainter *painter, const QString &text, const TextStyle &style, double width, const QColor &rule,
               double ruleWidth)
{
    QList<QStringList> sections{{}};
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.trimmed() == QLatin1String("--")) {
            sections.append(QStringList());
        } else {
            sections.last().append(line);
        }
    }
    const double pad = 6;
    const double line = lineHeight(style);
    double y = 0;
    for (int i = 0; i < sections.size(); ++i) {
        const QStringList &section = sections.at(i);
        TextStyle sectionStyle = style;
        sectionStyle.align = Qt::AlignTop | (i == 0 ? Qt::AlignHCenter : Qt::AlignLeft);
        sectionStyle.bold = i == 0 || style.bold;
        const double height = std::max(1.0, double(section.size())) * line + pad * 2;
        if (i > 0) {
            painter->save();
            painter->setPen(QPen(rule, ruleWidth));
            painter->drawLine(QPointF(0, y), QPointF(width, y));
            painter->restore();
        }
        drawBlock(painter, section.join(QLatin1Char('\n')), sectionStyle,
                  QRectF(pad * 1.5, y + pad, std::max(1.0, width - pad * 3), height - pad * 2 + 1));
        y += height;
    }
}

} // namespace QindaOffice::Diagram::TextPaint
