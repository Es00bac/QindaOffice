// SPDX-License-Identifier: GPL-3.0-or-later
// Action-button outlines are document geometry, shared by screen, print and
// export. Keep their DrawingML preset names; a fallback rectangle would erase
// the navigation symbol visually even though the saved action still worked.
#include "preset_geometry_parts.h"
#include <QTransform>

namespace QindaOffice::Show::Geometry::Presets {
Outline actionButtonOutline(const QString &name, const QRectF &rect)
{
    QPainterPath symbol;
    const auto line = [&symbol](qreal x1, qreal y1, qreal x2, qreal y2) {
        symbol.moveTo(x1, y1);
        symbol.lineTo(x2, y2);
    };
    if (name == u"actionButtonForwardNext" || name == u"actionButtonEnd") {
        symbol = polygon({{25, 15}, {80, 50}, {25, 85}});
        if (name == u"actionButtonEnd") symbol.addRect(86, 15, 7, 70);
    } else if (name == u"actionButtonBackPrevious" || name == u"actionButtonBeginning") {
        symbol = polygon({{75, 15}, {20, 50}, {75, 85}});
        if (name == u"actionButtonBeginning") symbol.addRect(7, 15, 7, 70);
    } else if (name == u"actionButtonHome") {
        symbol = polygon({{5, 45}, {50, 5}, {95, 45}, {83, 45}, {83, 90},
                          {61, 90}, {61, 62}, {39, 62}, {39, 90}, {17, 90}, {17, 45}});
    } else if (name == u"actionButtonReturn") {
        symbol.moveTo(50, 15);
        symbol.lineTo(18, 40);
        symbol.lineTo(50, 65);
        symbol.lineTo(50, 49);
        symbol.cubicTo(78, 47, 81, 77, 39, 83);
        symbol.cubicTo(98, 94, 110, 29, 50, 31);
        symbol.closeSubpath();
    } else if (name == u"actionButtonDocument") {
        symbol = polygon({{20, 5}, {62, 5}, {85, 28}, {85, 95}, {20, 95}});
        line(62, 5, 62, 28); line(62, 28, 85, 28);
        for (int y : {43, 58, 73}) line(32, y, 71, y);
    } else if (name == u"actionButtonSound") {
        symbol = polygon({{5, 37}, {28, 37}, {54, 12}, {54, 88}, {28, 63}, {5, 63}});
        symbol.moveTo(65, 28); symbol.cubicTo(81, 39, 81, 61, 65, 72);
        symbol.moveTo(76, 14); symbol.cubicTo(103, 33, 103, 67, 76, 86);
    } else if (name == u"actionButtonMovie") {
        symbol.addRect(5, 15, 90, 70);
        symbol.addRect(26, 15, 48, 70);
        for (int y : {23, 44, 65}) {
            symbol.addRect(11, y, 9, 12);
            symbol.addRect(80, y, 9, 12);
        }
    } else if (name == u"actionButtonInformation") {
        symbol.addEllipse(QRectF(40, 6, 20, 20));
        symbol.addRect(40, 37, 20, 55);
        line(30, 37, 60, 37); line(30, 92, 70, 92);
    } else if (name == u"actionButtonHelp") {
        symbol.moveTo(23, 27);
        symbol.cubicTo(23, -1, 80, -1, 80, 28);
        symbol.cubicTo(80, 45, 51, 49, 51, 67);
        symbol.lineTo(41, 67);
        symbol.cubicTo(41, 43, 68, 40, 68, 28);
        symbol.cubicTo(68, 11, 35, 11, 35, 27);
        symbol.closeSubpath();
        symbol.addEllipse(QRectF(39, 79, 15, 15));
    } else if (name != u"actionButtonBlank") {
        return {};
    }
    QPainterPath border;
    border.addRect(rect);
    const qreal side = qMin(rect.width(), rect.height()) * 0.7;
    QTransform transform;
    transform.translate(rect.center().x() - side / 2, rect.center().y() - side / 2);
    transform.scale(side / 100, side / 100);
    QPainterPath stroke = border;
    stroke.addPath(transform.map(symbol));
    return {border, stroke};
}
} // namespace QindaOffice::Show::Geometry::Presets
