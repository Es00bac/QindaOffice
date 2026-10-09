// SPDX-License-Identifier: GPL-3.0-or-later
// Data bars and icon sets (calc_cell_look.h, M78, D-087). The icons are
// drawn, not taken from an icon font: QindaTK's Lucide set is chrome and
// has none of Excel's traffic lights, ratings or quarters, and these are
// document content, so they keep Excel's colours on every theme.

#include "calc_cell_look.h"

#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace QindaOffice::Apps::QindaCalc {

namespace {

const QColor kRed(0xd9, 0x37, 0x2b);
const QColor kYellow(0xf2, 0xb7, 0x05);
const QColor kGreen(0x2e, 0x9e, 0x48);
const QColor kGray(0x80, 0x80, 0x80);
const QColor kBlack(0x26, 0x26, 0x26);
const QColor kPink(0xf2, 0x9b, 0x9b);
const QColor kRating(0x3b, 0x6f, 0xb6);
const QColor kEmpty(0xd0, 0xd0, 0xd4);
const QColor kStar(0xf4, 0xb4, 0x00);

enum class Shape { Arrow, Circle, RimmedCircle, Flag, Diamond, Triangle, CircledCross, CircledBang, CircledCheck,
                   Cross, Bang, Check, Bars, Quarter, Star, Dash, Boxes };

struct Icon {
    Shape shape;
    QColor color;
    int value = 0; // Arrow: angle in degrees (90 up, 0 right, -90 down); Bars/Quarter/Star/Boxes: how much
};

QList<Icon> arrows(const QList<int> &angles, bool gray)
{
    QList<Icon> out;
    for (qsizetype i = 0; i < angles.size(); ++i) {
        const QColor color = gray ? kGray : (i == 0 ? kRed : (i + 1 == angles.size() ? kGreen : kYellow));
        out.append({Shape::Arrow, color, angles.at(i)});
    }
    return out;
}

// Each set's icons, lowest band first (the file's cfvo order).
const QHash<QString, QList<Icon>> &sets()
{
    static const QHash<QString, QList<Icon>> table{
        {QStringLiteral("3Arrows"), arrows({-90, 0, 90}, false)},
        {QStringLiteral("3ArrowsGray"), arrows({-90, 0, 90}, true)},
        {QStringLiteral("4Arrows"), arrows({-90, -45, 45, 90}, false)},
        {QStringLiteral("4ArrowsGray"), arrows({-90, -45, 45, 90}, true)},
        {QStringLiteral("5Arrows"), arrows({-90, -45, 0, 45, 90}, false)},
        {QStringLiteral("5ArrowsGray"), arrows({-90, -45, 0, 45, 90}, true)},
        {QStringLiteral("3Flags"), {{Shape::Flag, kRed}, {Shape::Flag, kYellow}, {Shape::Flag, kGreen}}},
        {QStringLiteral("3TrafficLights1"), {{Shape::Circle, kRed}, {Shape::Circle, kYellow}, {Shape::Circle, kGreen}}},
        {QStringLiteral("3TrafficLights2"),
         {{Shape::RimmedCircle, kRed}, {Shape::RimmedCircle, kYellow}, {Shape::RimmedCircle, kGreen}}},
        {QStringLiteral("4TrafficLights"),
         {{Shape::Circle, kBlack}, {Shape::Circle, kRed}, {Shape::Circle, kYellow}, {Shape::Circle, kGreen}}},
        {QStringLiteral("4RedToBlack"),
         {{Shape::Circle, kBlack}, {Shape::Circle, kGray}, {Shape::Circle, kPink}, {Shape::Circle, kRed}}},
        {QStringLiteral("3Signs"), {{Shape::Diamond, kRed}, {Shape::Triangle, kYellow}, {Shape::Circle, kGreen}}},
        {QStringLiteral("3Symbols"),
         {{Shape::CircledCross, kRed}, {Shape::CircledBang, kYellow}, {Shape::CircledCheck, kGreen}}},
        {QStringLiteral("3Symbols2"), {{Shape::Cross, kRed}, {Shape::Bang, kYellow}, {Shape::Check, kGreen}}},
        {QStringLiteral("4Rating"),
         {{Shape::Bars, kRating, 1}, {Shape::Bars, kRating, 2}, {Shape::Bars, kRating, 3}, {Shape::Bars, kRating, 4}}},
        {QStringLiteral("5Rating"),
         {{Shape::Bars, kRating, 0}, {Shape::Bars, kRating, 1}, {Shape::Bars, kRating, 2}, {Shape::Bars, kRating, 3},
          {Shape::Bars, kRating, 4}}},
        {QStringLiteral("5Quarters"),
         {{Shape::Quarter, kBlack, 0}, {Shape::Quarter, kBlack, 1}, {Shape::Quarter, kBlack, 2},
          {Shape::Quarter, kBlack, 3}, {Shape::Quarter, kBlack, 4}}},
        {QStringLiteral("3Stars"), {{Shape::Star, kStar, 0}, {Shape::Star, kStar, 1}, {Shape::Star, kStar, 2}}},
        {QStringLiteral("3Triangles"),
         {{Shape::Triangle, kRed, -1}, {Shape::Dash, kYellow}, {Shape::Triangle, kGreen, 1}}},
        {QStringLiteral("5Boxes"),
         {{Shape::Boxes, kRating, 0}, {Shape::Boxes, kRating, 1}, {Shape::Boxes, kRating, 2},
          {Shape::Boxes, kRating, 3}, {Shape::Boxes, kRating, 4}}},
    };
    return table;
}

void paintArrow(QPainter &p, const QRectF &box, const QColor &color, int angle)
{
    p.save();
    p.translate(box.center());
    p.rotate(-angle);
    const qreal s = box.width() / 2;
    QPainterPath path;
    path.moveTo(s * 0.9, 0);
    path.lineTo(s * 0.05, -s * 0.75);
    path.lineTo(s * 0.05, -s * 0.3);
    path.lineTo(-s * 0.85, -s * 0.3);
    path.lineTo(-s * 0.85, s * 0.3);
    path.lineTo(s * 0.05, s * 0.3);
    path.lineTo(s * 0.05, s * 0.75);
    path.closeSubpath();
    p.fillPath(path, color);
    p.restore();
}

void paintGlyph(QPainter &p, const QRectF &box, Shape shape, const QColor &ink)
{
    const QPointF c = box.center();
    const qreal s = box.width() / 2;
    p.setPen(QPen(ink, qMax(1.5, box.width() / 7), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (shape == Shape::Cross || shape == Shape::CircledCross) {
        p.drawLine(c + QPointF(-s * 0.4, -s * 0.4), c + QPointF(s * 0.4, s * 0.4));
        p.drawLine(c + QPointF(s * 0.4, -s * 0.4), c + QPointF(-s * 0.4, s * 0.4));
    } else if (shape == Shape::Bang || shape == Shape::CircledBang) {
        p.drawLine(c + QPointF(0, -s * 0.5), c + QPointF(0, s * 0.15));
        p.drawPoint(c + QPointF(0, s * 0.5));
    } else {
        p.drawPolyline(QPolygonF({c + QPointF(-s * 0.45, 0), c + QPointF(-s * 0.1, s * 0.4), c + QPointF(s * 0.5, -s * 0.4)}));
    }
}

QPolygonF starPolygon(const QRectF &box)
{
    QPolygonF star;
    const QPointF c = box.center();
    const qreal outer = box.width() / 2;
    for (int i = 0; i < 10; ++i) {
        const qreal radius = i % 2 == 0 ? outer : outer * 0.45;
        const qreal angle = qDegreesToRadians(-90.0 + i * 36.0);
        star << c + QPointF(radius * qCos(angle), radius * qSin(angle));
    }
    return star;
}

void paintIcon(QPainter &p, const QRectF &box, const Icon &icon)
{
    const QRectF inner = box.adjusted(1, 1, -1, -1);
    switch (icon.shape) {
    case Shape::Arrow:
        paintArrow(p, inner, icon.color, icon.value);
        break;
    case Shape::Circle:
    case Shape::RimmedCircle:
        p.setPen(icon.shape == Shape::RimmedCircle ? QPen(kBlack, 1.2) : QPen(icon.color.darker(130), 0.8));
        p.setBrush(icon.color);
        p.drawEllipse(inner);
        break;
    case Shape::Flag: {
        p.setPen(QPen(kBlack, 1));
        p.drawLine(QPointF(inner.left() + 1, inner.top()), QPointF(inner.left() + 1, inner.bottom()));
        p.setPen(Qt::NoPen);
        p.setBrush(icon.color);
        p.drawPolygon(QPolygonF({QPointF(inner.left() + 1.5, inner.top()), QPointF(inner.right(), inner.top() + inner.height() * 0.3),
                                 QPointF(inner.left() + 1.5, inner.top() + inner.height() * 0.6)}));
        break;
    }
    case Shape::Diamond:
        p.setPen(Qt::NoPen);
        p.setBrush(icon.color);
        p.drawPolygon(QPolygonF({QPointF(inner.center().x(), inner.top()), QPointF(inner.right(), inner.center().y()),
                                 QPointF(inner.center().x(), inner.bottom()), QPointF(inner.left(), inner.center().y())}));
        break;
    case Shape::Triangle: {
        p.setPen(Qt::NoPen);
        p.setBrush(icon.color);
        const bool down = icon.value < 0;
        p.drawPolygon(QPolygonF({QPointF(inner.center().x(), down ? inner.bottom() : inner.top()),
                                 QPointF(inner.right(), down ? inner.top() : inner.bottom()),
                                 QPointF(inner.left(), down ? inner.top() : inner.bottom())}));
        break;
    }
    case Shape::Dash:
        p.fillRect(QRectF(inner.left(), inner.center().y() - inner.height() * 0.12, inner.width(), inner.height() * 0.24),
                   icon.color);
        break;
    case Shape::CircledCross:
    case Shape::CircledBang:
    case Shape::CircledCheck:
        p.setPen(Qt::NoPen);
        p.setBrush(icon.color);
        p.drawEllipse(inner);
        paintGlyph(p, inner, icon.shape, Qt::white);
        break;
    case Shape::Cross:
    case Shape::Bang:
    case Shape::Check:
        paintGlyph(p, inner, icon.shape, icon.color);
        break;
    case Shape::Bars:
        for (int bar = 0; bar < 4; ++bar) {
            const qreal w = inner.width() / 4;
            const qreal h = inner.height() * (bar + 1) / 4.0;
            p.fillRect(QRectF(inner.left() + bar * w + 0.5, inner.bottom() - h, w - 1, h), bar < icon.value ? icon.color : kEmpty);
        }
        break;
    case Shape::Quarter:
        p.setPen(QPen(icon.color, 1));
        p.setBrush(Qt::white);
        p.drawEllipse(inner);
        p.setBrush(icon.color);
        if (icon.value >= 4) {
            p.drawEllipse(inner);
        } else if (icon.value > 0) {
            p.drawPie(inner, 90 * 16, -icon.value * 90 * 16);
        }
        break;
    case Shape::Star: {
        const QPolygonF star = starPolygon(inner);
        p.setPen(QPen(icon.color.darker(120), 0.8));
        p.setBrush(icon.value >= 2 ? QBrush(icon.color) : QBrush(Qt::white));
        p.drawPolygon(star);
        if (icon.value == 1) {
            p.save();
            p.setClipRect(QRectF(inner.left(), inner.top(), inner.width() / 2, inner.height()));
            p.setBrush(icon.color);
            p.drawPolygon(star);
            p.restore();
        }
        break;
    }
    case Shape::Boxes:
        for (int quadrant = 0; quadrant < 4; ++quadrant) {
            const qreal w = inner.width() / 2;
            const QRectF cell(inner.left() + (quadrant % 2) * w, inner.top() + (quadrant / 2) * w, w - 1, w - 1);
            p.fillRect(cell, quadrant < icon.value ? icon.color : kEmpty);
        }
        break;
    }
}

} // namespace

void paintDataBar(QPainter &p, const QRectF &cell, const CalcEng::DataBarFill &bar)
{
    const QRectF inset = cell.adjusted(2, 2, -2, -2);
    if (inset.width() <= 0 || inset.height() <= 0) {
        return;
    }
    const qreal originX = bar.rightToLeft ? inset.right() - bar.origin * inset.width() : inset.left() + bar.origin * inset.width();
    const qreal length = qBound(0.0, bar.fraction, 1.0) * inset.width();
    // Rightwards for a positive value left to right; a negative value, or a
    // right-to-left bar, runs the other way.
    const bool leftwards = bar.negative != bar.rightToLeft;
    const QRectF rect(leftwards ? originX - length : originX, inset.top(), length, inset.height());
    const QColor color(bar.color.r, bar.color.g, bar.color.b);
    if (bar.gradient) {
        // Excel's gradient fades towards the bar's far end.
        QLinearGradient fade(leftwards ? rect.topRight() : rect.topLeft(), leftwards ? rect.topLeft() : rect.topRight());
        fade.setColorAt(0, color);
        fade.setColorAt(1, color.lighter(185));
        p.fillRect(rect, fade);
    } else {
        p.fillRect(rect, color);
    }
    if (bar.border.has_value()) {
        p.setPen(QPen(QColor(bar.border->r, bar.border->g, bar.border->b), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
    }
    if (bar.axisColor.has_value() && bar.origin > 0.0 && bar.origin < 1.0) {
        QPen axis(QColor(bar.axisColor->r, bar.axisColor->g, bar.axisColor->b), 1, Qt::DashLine);
        p.setPen(axis);
        p.drawLine(QPointF(originX, cell.top()), QPointF(originX, cell.bottom()));
    }
}

QRectF iconBox(const QRectF &cell)
{
    const qreal side = qMin(cell.height() - 4, 16.0);
    return {cell.left() + 2, cell.center().y() - side / 2, side, side};
}

void paintCfIcon(QPainter &p, const QRectF &box, const CalcEng::IconPaint &icon)
{
    const auto found = sets().constFind(icon.set);
    if (found == sets().constEnd() || icon.index < 0 || icon.index >= found->size()) {
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    paintIcon(p, box, found->at(icon.index));
    p.restore();
}

int iconSetSize(const QString &set)
{
    return int(sets().value(set).size());
}

} // namespace QindaOffice::Apps::QindaCalc
