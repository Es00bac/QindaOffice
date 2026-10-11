// SPDX-License-Identifier: GPL-3.0-or-later
#include "chart_interchange.h"
#include "pptx_read_shapes.h"
#include "render_parts.h"
#include "shape_json_parts.h"
#include <QJsonObject>

namespace QindaOffice::Show::ChartIO {
ChartData read(const std::shared_ptr<const Ooxml::Package> &source,
               const QString &part, const Ooxml::Theme &theme)
{
    Pptx::ThemeContext context;
    context.theme = theme;
    Pptx::PartContext ctx;
    ctx.package = source;
    ctx.part = part;
    ctx.theme = &context;
    return Pptx::readChart(ctx, part);
}
QVariantMap toMap(const ChartData &chart) { return Json::chart(chart).toVariantMap(); }
ChartData fromMap(const QVariantMap &map) { return Json::chart(QJsonObject::fromVariantMap(map)); }
void paint(QPainter *painter, const QRectF &rect, const ChartData &chart)
{
    Render::paintChart(painter, rect, chart);
}
} // namespace QindaOffice::Show::ChartIO
