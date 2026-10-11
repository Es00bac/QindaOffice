// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_graphics_p.h"
#include "chart_edit.h"
#include "chart_interchange.h"
#include <QJsonObject>
#include <cmath>

namespace QindaOffice::Docx {
namespace {
QString tableText(const Show::ChartData &chart)
{
    const auto table = Show::ChartEdit::tableOf(chart);
    QStringList lines{(QStringList{QStringLiteral("Category")} + table.series).join(QLatin1Char('\t'))};
    for (qsizetype i = 0; i < table.categories.size(); ++i) {
        QStringList row{table.categories.at(i)};
        for (const auto &series : table.values) row.append(Show::ChartEdit::valueText(series.value(i, qQNaN())));
        lines.append(row.join(QLatin1Char('\t')));
    }
    return lines.join(QLatin1Char('\n'));
}
bool parseTable(const QString &text, Show::ChartData &chart, QString *error)
{
    auto fail = [&](const QString &reason) { if (error) *error = reason; return false; };
    QStringList lines = text.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();
    if (lines.size() < 2 || lines.size() > 257) return fail(QStringLiteral("Enter a header row and 1–256 data rows."));
    const auto header = lines.takeFirst().split(QLatin1Char('\t'));
    if (header.size() < 2 || header.size() > 33) return fail(QStringLiteral("Separate the category and 1–32 series with tabs."));
    Show::ChartEdit::ChartTable table;
    table.series = header.mid(1); table.values.resize(table.series.size());
    for (const QString &line : lines) {
        const auto cells = line.split(QLatin1Char('\t'));
        if (cells.size() != header.size()) return fail(QStringLiteral("Every row must have the same number of tab-separated cells."));
        table.categories.append(cells.first());
        for (int i = 1; i < cells.size(); ++i) {
            bool ok = cells.at(i).trimmed().isEmpty();
            const qreal number = ok ? qQNaN() : cells.at(i).trimmed().toDouble(&ok);
            if (!ok || std::isinf(number)) return fail(QStringLiteral("Values must be finite numbers; leave a cell blank for a gap."));
            table.values[i - 1].append(number);
        }
    }
    Show::ChartEdit::setTable(chart, table); return true;
}
} // namespace
QVariantMap graphicInfo(const QTextImageFormat &format)
{
    const QString kind = format.stringProperty(kGraphicKind);
    if (kind.isEmpty()) return {};
    QVariantMap info{{QStringLiteral("kind"), kind}, {QStringLiteral("width"), format.width()}, {QStringLiteral("height"), format.height()}};
    const auto data = format.property(kGraphicData).toMap();
    if (kind == u"chart") {
        const auto chart = Show::ChartIO::fromMap(data);
        info.insert(QStringLiteral("data"), tableText(chart));
        QStringList kinds;
        for (const auto &item : Show::ChartEdit::kinds()) kinds.append(QString::fromUtf8(item.label));
        info.insert(QStringLiteral("chartTypes"), kinds);
        QVariantList plots;
        for (int i = 0; i < chart.plotCount(); ++i) plots.append(Show::ChartEdit::plotKindIndex(chart, i));
        info.insert(QStringLiteral("plotKinds"), plots);
        info.insert(QStringLiteral("chartType"), Show::ChartEdit::kindIndex(chart));
    } else if (kind == u"shape") {
        const auto shape = Show::shapeFromJson(QJsonObject::fromVariantMap(data));
        info.insert(QStringLiteral("text"), shape.displayText()); info.insert(QStringLiteral("preset"), shape.geometry);
        info.insert(QStringLiteral("fill"), shape.fill.name(QColor::HexArgb));
        info.insert(QStringLiteral("stroke"), shape.stroke.name(QColor::HexArgb));
        info.insert(QStringLiteral("strokeWidth"), shape.strokeWidth);
    } else {
        info.insert(QStringLiteral("nodes"), data.value(QStringLiteral("texts")));
        info.insert(QStringLiteral("text"), data.value(QStringLiteral("texts")).toStringList().join(QLatin1Char('\n')));
    }
    return info;
}
bool editGraphic(QTextImageFormat &format, const QVariantMap &answer, QTextDocument &document, QString *error)
{
    const QString kind = format.stringProperty(kGraphicKind);
    auto data = format.property(kGraphicData).toMap();
    auto fail = [&](const QString &reason) { if (error) *error = reason; return false; };
    const qreal width = answer.value(QStringLiteral("width"), format.width()).toDouble();
    const qreal height = answer.value(QStringLiteral("height"), format.height()).toDouble();
    if (!std::isfinite(width) || !std::isfinite(height) || width < 24 || height < 24 || width > 2400 || height > 2400)
        return fail(QStringLiteral("Graphic width and height must be 24–2400 document pixels."));
    if (kind == u"chart") {
        auto chart = Show::ChartIO::fromMap(data);
        if (answer.contains(QStringLiteral("data")) && !parseTable(answer.value(QStringLiteral("data")).toString(), chart, error)) return false;
        if (answer.contains(QStringLiteral("chartType"))) {
            const int index = answer.value(QStringLiteral("chartType")).toInt();
            const int plot = answer.value(QStringLiteral("plot"), 0).toInt();
            if (index < 0 || index >= Show::ChartEdit::kinds().size() || plot < 0 || plot >= chart.plotCount())
                return fail(QStringLiteral("Choose a supported chart type and plot."));
            if (chart.plotCount() > 1 && !Show::ChartEdit::comboKind(index))
                return fail(QStringLiteral("A combo chart needs a chart type with axes for each plot."));
            if (chart.plotCount() > 1) Show::ChartEdit::setPlotKind(chart, plot, index);
            else Show::ChartEdit::setKind(chart, index);
        }
        if (chart.type == u"scatter") {
            for (const auto &series : chart.series) for (const auto &x : series.categories) {
                bool ok = false; const qreal number = x.toDouble(&ok);
                if (!x.trimmed().isEmpty() && (!ok || !std::isfinite(number)))
                    return fail(QStringLiteral("Scatter charts need numeric X values in the first column."));
            }
        }
        data = Show::ChartIO::toMap(chart);
    } else if (kind == u"shape") {
        auto shape = Show::shapeFromJson(QJsonObject::fromVariantMap(data));
        const QStringList presets{QStringLiteral("rect"), QStringLiteral("roundRect"), QStringLiteral("ellipse"), QStringLiteral("triangle"), QStringLiteral("diamond"), QStringLiteral("rightArrow"), QStringLiteral("line")};
        if (answer.contains(QStringLiteral("preset"))) {
            const auto preset = answer.value(QStringLiteral("preset")).toString();
            if (preset != shape.geometry && !presets.contains(preset)) return fail(QStringLiteral("Choose a supported shape."));
            if (preset != shape.geometry) { shape.geometry = preset; shape.customGeometry.clear(); shape.kind = Show::ShapeKind::AutoShape; }
        }
        if (answer.contains(QStringLiteral("text"))) Graphics::replaceText(shape, answer.value(QStringLiteral("text")).toString());
        for (const auto &key : {QStringLiteral("fill"), QStringLiteral("stroke")}) if (answer.contains(key)) {
            const QColor color(answer.value(key).toString());
            if (!color.isValid()) return fail(QStringLiteral("Use a valid colour name or #RRGGBB value."));
            if (key == u"fill") { if (shape.fill != color) shape.fillStyle = {}; shape.fill = color; } else shape.stroke = color;
        }
        if (answer.contains(QStringLiteral("strokeWidth"))) {
            const qreal stroke = answer.value(QStringLiteral("strokeWidth")).toDouble();
            if (!std::isfinite(stroke) || stroke < 0 || stroke > 24) return fail(QStringLiteral("Outline width must be 0–24 points."));
            shape.strokeWidth = stroke;
        }
        data = Show::shapeToJson(shape).toVariantMap();
    } else if (kind == u"smartArt") {
        if (answer.contains(QStringLiteral("nodes")) && !Graphics::editDiagram(data, answer.value(QStringLiteral("nodes")).toStringList(), error)) return false;
        if (!answer.contains(QStringLiteral("nodes")) && answer.contains(QStringLiteral("text")) && !Graphics::editDiagram(data, answer.value(QStringLiteral("text")).toString().split(QLatin1Char('\n')), error)) return false;
    } else return fail(QStringLiteral("Select a supported graphic first."));
    format.setProperty(kGraphicData, data); format.setWidth(width); format.setHeight(height); refreshGraphic(format, document); return true;
}
} // namespace QindaOffice::Docx
