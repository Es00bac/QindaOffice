// SPDX-License-Identifier: GPL-3.0-or-later
#include "shape_json_parts.h"

#include <cmath>
#include <limits>

namespace QindaOffice::Show::Json {

namespace {

QJsonArray reals(const QList<qreal> &values)
{
    QJsonArray array;
    for (const qreal v : values) {
        // JSON has no NaN: a chart gap travels as null.
        array.append(std::isnan(v) ? QJsonValue(QJsonValue::Null) : QJsonValue(v));
    }
    return array;
}

QList<qreal> reals(const QJsonArray &array)
{
    QList<qreal> values;
    for (const QJsonValue &v : array) {
        values.append(v.isNull() ? std::numeric_limits<qreal>::quiet_NaN() : v.toDouble());
    }
    return values;
}

QJsonObject run(const TextRun &r)
{
    QJsonObject o{{QStringLiteral("text"), r.text},         {QStringLiteral("bold"), r.bold},
                  {QStringLiteral("italic"), r.italic},     {QStringLiteral("color"), color(r.color)},
                  {QStringLiteral("pointSize"), r.pointSize}, {QStringLiteral("fontFamily"), r.fontFamily},
                  {QStringLiteral("underline"), r.underline}, {QStringLiteral("strike"), r.strike},
                  {QStringLiteral("baseline"), r.baseline}, {QStringLiteral("caps"), r.caps},
                  {QStringLiteral("spacing"), r.spacing},   {QStringLiteral("highlight"), color(r.highlight)},
                  {QStringLiteral("hyperlink"), r.hyperlink}, {QStringLiteral("fieldType"), r.fieldType},
                  {QStringLiteral("fieldId"), r.fieldId}};
    return o;
}

TextRun run(const QJsonObject &o)
{
    TextRun r;
    r.text = o.value(QStringLiteral("text")).toString();
    r.bold = o.value(QStringLiteral("bold")).toBool();
    r.italic = o.value(QStringLiteral("italic")).toBool();
    r.color = color(o.value(QStringLiteral("color")), Qt::black);
    r.pointSize = o.value(QStringLiteral("pointSize")).toDouble(18);
    r.fontFamily = o.value(QStringLiteral("fontFamily")).toString();
    r.underline = o.value(QStringLiteral("underline")).toString();
    r.strike = o.value(QStringLiteral("strike")).toString();
    r.baseline = o.value(QStringLiteral("baseline")).toInt();
    r.caps = o.value(QStringLiteral("caps")).toString();
    r.spacing = o.value(QStringLiteral("spacing")).toDouble();
    r.highlight = color(o.value(QStringLiteral("highlight")));
    r.hyperlink = o.value(QStringLiteral("hyperlink")).toString();
    r.fieldType = o.value(QStringLiteral("fieldType")).toString();
    r.fieldId = o.value(QStringLiteral("fieldId")).toString();
    return r;
}

QJsonArray spacing(const Spacing &s)
{
    return {s.unit == Spacing::Unit::Points ? 1 : 0, s.value};
}

Spacing spacing(const QJsonValue &v, const Spacing &fallback)
{
    const QJsonArray a = v.toArray();
    if (a.size() != 2) {
        return fallback;
    }
    return {a.at(0).toInt() == 1 ? Spacing::Unit::Points : Spacing::Unit::Percent, a.at(1).toDouble()};
}

QJsonObject border(const CellBorder &b)
{
    return {{QStringLiteral("set"), b.set}, {QStringLiteral("color"), color(b.color)},
            {QStringLiteral("width"), b.width}, {QStringLiteral("dash"), b.dash}};
}

CellBorder border(const QJsonObject &o)
{
    return {o.value(QStringLiteral("set")).toBool(), color(o.value(QStringLiteral("color"))),
            o.value(QStringLiteral("width")).toDouble(), o.value(QStringLiteral("dash")).toString()};
}

QJsonObject region(const TableRegion &r)
{
    return {{QStringLiteral("fill"), color(r.fill)},        {QStringLiteral("text"), color(r.text)},
            {QStringLiteral("bold"), r.bold},               {QStringLiteral("insideH"), border(r.insideH)},
            {QStringLiteral("insideV"), border(r.insideV)}, {QStringLiteral("outer"), border(r.outer)}};
}

TableRegion region(const QJsonObject &o)
{
    TableRegion r;
    r.fill = color(o.value(QStringLiteral("fill")));
    r.text = color(o.value(QStringLiteral("text")));
    r.bold = o.value(QStringLiteral("bold")).toBool();
    r.insideH = border(o.value(QStringLiteral("insideH")).toObject());
    r.insideV = border(o.value(QStringLiteral("insideV")).toObject());
    r.outer = border(o.value(QStringLiteral("outer")).toObject());
    return r;
}

} // namespace

QJsonValue color(const QColor &c)
{
    if (!c.isValid()) {
        return QJsonValue(QJsonValue::Null);
    }
    return QJsonObject{{QStringLiteral("r"), c.red()}, {QStringLiteral("g"), c.green()},
                       {QStringLiteral("b"), c.blue()}, {QStringLiteral("a"), c.alpha()}};
}

QColor color(const QJsonValue &value, const QColor &fallback)
{
    if (!value.isObject()) {
        return value.isNull() ? QColor() : fallback;
    }
    const QJsonObject o = value.toObject();
    return QColor(o.value(QStringLiteral("r")).toInt(), o.value(QStringLiteral("g")).toInt(),
                  o.value(QStringLiteral("b")).toInt(), o.value(QStringLiteral("a")).toInt(255));
}

QJsonArray paragraphs(const QList<TextParagraph> &list)
{
    QJsonArray array;
    for (const TextParagraph &p : list) {
        QJsonArray runs;
        for (const TextRun &r : p.runs) {
            runs.append(run(r));
        }
        array.append(QJsonObject{
            {QStringLiteral("runs"), runs},
            {QStringLiteral("alignment"), p.alignment},
            {QStringLiteral("bullet"), p.bullet},
            {QStringLiteral("listLevel"), p.listLevel},
            {QStringLiteral("bulletChar"), p.bulletChar},
            {QStringLiteral("autoNumber"), p.autoNumber},
            {QStringLiteral("autoNumberStart"), p.autoNumberStart},
            {QStringLiteral("bulletFont"), p.bulletFont},
            {QStringLiteral("bulletColor"), color(p.bulletColor)},
            {QStringLiteral("bulletSize"), p.bulletSize},
            {QStringLiteral("marginLeft"), p.marginLeft},
            {QStringLiteral("indent"), p.indent},
            {QStringLiteral("lineSpacing"), spacing(p.lineSpacing)},
            {QStringLiteral("spaceBefore"), spacing(p.spaceBefore)},
            {QStringLiteral("spaceAfter"), spacing(p.spaceAfter)},
            {QStringLiteral("endPointSize"), p.endPointSize},
        });
    }
    return array;
}

QList<TextParagraph> paragraphs(const QJsonArray &array)
{
    QList<TextParagraph> list;
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        TextParagraph p;
        for (const QJsonValue &r : o.value(QStringLiteral("runs")).toArray()) {
            p.runs.append(run(r.toObject()));
        }
        p.alignment = o.value(QStringLiteral("alignment")).toInt();
        p.bullet = o.value(QStringLiteral("bullet")).toBool();
        p.listLevel = o.value(QStringLiteral("listLevel")).toInt();
        p.bulletChar = o.value(QStringLiteral("bulletChar")).toString();
        p.autoNumber = o.value(QStringLiteral("autoNumber")).toString();
        p.autoNumberStart = o.value(QStringLiteral("autoNumberStart")).toInt(1);
        p.bulletFont = o.value(QStringLiteral("bulletFont")).toString();
        p.bulletColor = color(o.value(QStringLiteral("bulletColor")));
        p.bulletSize = o.value(QStringLiteral("bulletSize")).toDouble(100);
        p.marginLeft = o.value(QStringLiteral("marginLeft")).toDouble();
        p.indent = o.value(QStringLiteral("indent")).toDouble();
        p.lineSpacing = spacing(o.value(QStringLiteral("lineSpacing")), Spacing::percent(100));
        p.spaceBefore = spacing(o.value(QStringLiteral("spaceBefore")), Spacing::points(0));
        p.spaceAfter = spacing(o.value(QStringLiteral("spaceAfter")), Spacing::points(0));
        p.endPointSize = o.value(QStringLiteral("endPointSize")).toDouble();
        list.append(p);
    }
    return list;
}

QJsonObject fillStyle(const FillStyle &s)
{
    QJsonArray stops;
    for (const GradientStop &stop : s.stops) {
        stops.append(QJsonArray{stop.position, color(stop.color)});
    }
    QJsonObject object{{QStringLiteral("kind"), int(s.kind)},
                       {QStringLiteral("stops"), stops},
                       {QStringLiteral("angle"), s.angle},
                       {QStringLiteral("path"), s.path},
                       {QStringLiteral("image"), QString::fromLatin1(s.image.toBase64())},
                       {QStringLiteral("imageMime"), s.imageMime},
                       {QStringLiteral("tile"), s.tile},
                       {QStringLiteral("pattern"), s.pattern},
                       {QStringLiteral("foreground"), color(s.foreground)},
                       {QStringLiteral("background"), color(s.background)}};
    if (s.imageAlpha < 1.0) {
        object.insert(QStringLiteral("imageAlpha"), s.imageAlpha); // M75
    }
    return object;
}

FillStyle fillStyle(const QJsonObject &o)
{
    FillStyle s;
    s.kind = FillKind(qBound(0, o.value(QStringLiteral("kind")).toInt(), int(FillKind::Pattern)));
    for (const QJsonValue &v : o.value(QStringLiteral("stops")).toArray()) {
        const QJsonArray stop = v.toArray();
        s.stops.append({stop.at(0).toDouble(), color(stop.at(1))});
    }
    s.angle = o.value(QStringLiteral("angle")).toDouble();
    s.path = o.value(QStringLiteral("path")).toString();
    s.image = QByteArray::fromBase64(o.value(QStringLiteral("image")).toString().toLatin1());
    s.imageMime = o.value(QStringLiteral("imageMime")).toString();
    s.tile = o.value(QStringLiteral("tile")).toBool();
    s.pattern = o.value(QStringLiteral("pattern")).toString();
    s.foreground = color(o.value(QStringLiteral("foreground")));
    s.background = color(o.value(QStringLiteral("background")));
    s.imageAlpha = qBound(0.0, o.value(QStringLiteral("imageAlpha")).toDouble(1.0), 1.0);
    return s;
}

QJsonObject media(const MediaInfo &m)
{
    return {{QStringLiteral("kind"), int(m.kind)},
            {QStringLiteral("data"), QString::fromLatin1(m.data.toBase64())},
            {QStringLiteral("mime"), m.mime},
            {QStringLiteral("link"), m.link},
            {QStringLiteral("name"), m.name},
            {QStringLiteral("durationMs"), m.durationMs},
            {QStringLiteral("autoPlay"), m.autoPlay},
            {QStringLiteral("loop"), m.loop},
            {QStringLiteral("hideWhenStopped"), m.hideWhenStopped},
            {QStringLiteral("volume"), m.volume}};
}

MediaInfo media(const QJsonObject &o)
{
    MediaInfo m;
    m.kind = MediaKind(qBound(0, o.value(QStringLiteral("kind")).toInt(), int(MediaKind::Audio)));
    m.data = QByteArray::fromBase64(o.value(QStringLiteral("data")).toString().toLatin1());
    m.mime = o.value(QStringLiteral("mime")).toString();
    m.link = o.value(QStringLiteral("link")).toString();
    m.name = o.value(QStringLiteral("name")).toString();
    m.durationMs = o.value(QStringLiteral("durationMs")).toInt();
    m.autoPlay = o.value(QStringLiteral("autoPlay")).toBool();
    m.loop = o.value(QStringLiteral("loop")).toBool();
    m.hideWhenStopped = o.value(QStringLiteral("hideWhenStopped")).toBool();
    m.volume = qBound(0, o.value(QStringLiteral("volume")).toInt(80), 100);
    return m;
}

QJsonObject lineStyle(const LineStyle &s)
{
    const auto end = [](const LineEnd &e) { return QJsonArray{e.type, e.width, e.length}; };
    return {{QStringLiteral("dash"), s.dash}, {QStringLiteral("cap"), s.cap}, {QStringLiteral("join"), s.join},
            {QStringLiteral("head"), end(s.head)}, {QStringLiteral("tail"), end(s.tail)}};
}

LineStyle lineStyle(const QJsonObject &o)
{
    const auto end = [](const QJsonValue &v) {
        const QJsonArray a = v.toArray();
        return LineEnd{a.at(0).toString(), a.at(1).toString(), a.at(2).toString()};
    };
    return {o.value(QStringLiteral("dash")).toString(), o.value(QStringLiteral("cap")).toString(),
            o.value(QStringLiteral("join")).toString(), end(o.value(QStringLiteral("head"))),
            end(o.value(QStringLiteral("tail")))};
}

QJsonObject textBody(const TextBodyStyle &s)
{
    return {{QStringLiteral("insets"), QJsonArray{s.insetLeft, s.insetTop, s.insetRight, s.insetBottom}},
            {QStringLiteral("anchor"), int(s.anchor)},
            {QStringLiteral("anchorCenter"), s.anchorCenter},
            {QStringLiteral("autoFit"), int(s.autoFit)},
            {QStringLiteral("fontScale"), s.fontScale},
            {QStringLiteral("lineSpaceReduction"), s.lineSpaceReduction},
            {QStringLiteral("wrap"), s.wrap},
            {QStringLiteral("vertical"), s.vertical}};
}

TextBodyStyle textBody(const QJsonObject &o)
{
    TextBodyStyle s;
    const QJsonArray insets = o.value(QStringLiteral("insets")).toArray();
    if (insets.size() == 4) {
        s.insetLeft = insets.at(0).toDouble();
        s.insetTop = insets.at(1).toDouble();
        s.insetRight = insets.at(2).toDouble();
        s.insetBottom = insets.at(3).toDouble();
    }
    s.anchor = TextAnchor(qBound(0, o.value(QStringLiteral("anchor")).toInt(), int(TextAnchor::Distributed)));
    s.anchorCenter = o.value(QStringLiteral("anchorCenter")).toBool();
    s.autoFit = AutoFit(qBound(0, o.value(QStringLiteral("autoFit")).toInt(), int(AutoFit::Shape)));
    s.fontScale = o.value(QStringLiteral("fontScale")).toDouble(100);
    s.lineSpaceReduction = o.value(QStringLiteral("lineSpaceReduction")).toDouble();
    s.wrap = o.value(QStringLiteral("wrap")).toBool(true);
    s.vertical = o.value(QStringLiteral("vertical")).toString();
    return s;
}

QJsonObject table(const Table &t)
{
    QJsonArray rows;
    for (const TableRow &row : t.rows) {
        QJsonArray cells;
        for (const TableCell &c : row.cells) {
            cells.append(QJsonObject{
                {QStringLiteral("paragraphs"), paragraphs(c.paragraphs)},
                {QStringLiteral("margins"), QJsonArray{c.marginLeft, c.marginRight, c.marginTop, c.marginBottom}},
                {QStringLiteral("anchor"), int(c.anchor)},
                {QStringLiteral("fill"), color(c.fill)},
                {QStringLiteral("noFill"), c.noFill},
                {QStringLiteral("fillStyle"), fillStyle(c.fillStyle)},
                {QStringLiteral("borders"), QJsonArray{border(c.left), border(c.right), border(c.top), border(c.bottom)}},
                {QStringLiteral("span"), QJsonArray{c.gridSpan, c.rowSpan, c.hMerge, c.vMerge}},
            });
        }
        rows.append(QJsonObject{{QStringLiteral("height"), row.height}, {QStringLiteral("cells"), cells}});
    }
    const TableLook &l = t.look;
    return {{QStringLiteral("columns"), reals(t.columns)},
            {QStringLiteral("rows"), rows},
            {QStringLiteral("styleId"), t.styleId},
            {QStringLiteral("flags"), QJsonArray{t.firstRow, t.firstColumn, t.lastRow, t.lastColumn, t.bandRows, t.bandColumns}},
            {QStringLiteral("look"), QJsonArray{l.known, region(l.whole), region(l.band1H), region(l.band2H),
                                                region(l.band1V), region(l.band2V), region(l.firstRow),
                                                region(l.lastRow), region(l.firstCol), region(l.lastCol)}}};
}

Table table(const QJsonObject &o)
{
    Table t;
    t.columns = reals(o.value(QStringLiteral("columns")).toArray());
    for (const QJsonValue &rv : o.value(QStringLiteral("rows")).toArray()) {
        const QJsonObject ro = rv.toObject();
        TableRow row;
        row.height = ro.value(QStringLiteral("height")).toDouble();
        for (const QJsonValue &cv : ro.value(QStringLiteral("cells")).toArray()) {
            const QJsonObject co = cv.toObject();
            TableCell c;
            c.paragraphs = paragraphs(co.value(QStringLiteral("paragraphs")).toArray());
            const QJsonArray m = co.value(QStringLiteral("margins")).toArray();
            if (m.size() == 4) {
                c.marginLeft = m.at(0).toDouble();
                c.marginRight = m.at(1).toDouble();
                c.marginTop = m.at(2).toDouble();
                c.marginBottom = m.at(3).toDouble();
            }
            c.anchor = TextAnchor(qBound(0, co.value(QStringLiteral("anchor")).toInt(), int(TextAnchor::Distributed)));
            c.fill = color(co.value(QStringLiteral("fill")));
            c.noFill = co.value(QStringLiteral("noFill")).toBool();
            c.fillStyle = fillStyle(co.value(QStringLiteral("fillStyle")).toObject());
            const QJsonArray b = co.value(QStringLiteral("borders")).toArray();
            if (b.size() == 4) {
                c.left = border(b.at(0).toObject());
                c.right = border(b.at(1).toObject());
                c.top = border(b.at(2).toObject());
                c.bottom = border(b.at(3).toObject());
            }
            const QJsonArray span = co.value(QStringLiteral("span")).toArray();
            if (span.size() == 4) {
                c.gridSpan = qMax(1, span.at(0).toInt(1));
                c.rowSpan = qMax(1, span.at(1).toInt(1));
                c.hMerge = span.at(2).toBool();
                c.vMerge = span.at(3).toBool();
            }
            row.cells.append(c);
        }
        t.rows.append(row);
    }
    t.styleId = o.value(QStringLiteral("styleId")).toString();
    const QJsonArray f = o.value(QStringLiteral("flags")).toArray();
    if (f.size() == 6) {
        t.firstRow = f.at(0).toBool();
        t.firstColumn = f.at(1).toBool();
        t.lastRow = f.at(2).toBool();
        t.lastColumn = f.at(3).toBool();
        t.bandRows = f.at(4).toBool();
        t.bandColumns = f.at(5).toBool();
    }
    const QJsonArray l = o.value(QStringLiteral("look")).toArray();
    if (l.size() == 10) {
        t.look.known = l.at(0).toBool();
        TableRegion *regions[] = {&t.look.whole,  &t.look.band1H,   &t.look.band2H,  &t.look.band1V, &t.look.band2V,
                                  &t.look.firstRow, &t.look.lastRow, &t.look.firstCol, &t.look.lastCol};
        for (int i = 0; i < 9; ++i) {
            *regions[i] = region(l.at(i + 1).toObject());
        }
    }
    return t;
}

QJsonObject chart(const ChartData &c)
{
    QJsonArray series;
    for (const ChartSeries &s : c.series) {
        QJsonArray points;
        for (const QColor &p : s.pointColors) {
            points.append(color(p));
        }
        series.append(QJsonObject{{QStringLiteral("name"), s.name},
                                  {QStringLiteral("categories"), QJsonArray::fromStringList(s.categories)},
                                  {QStringLiteral("values"), reals(s.values)},
                                  {QStringLiteral("xValues"), reals(s.xValues)},
                                  {QStringLiteral("color"), color(s.color)},
                                  {QStringLiteral("pointColors"), points},
                                  {QStringLiteral("plot"), s.plot},
                                  {QStringLiteral("sourceIdx"), s.sourceIdx}});
    }
    QJsonArray palette;
    for (const QColor &p : c.palette) {
        palette.append(color(p));
    }
    // M82 (D-091): the plots, the category levels and the categories' source.
    QJsonArray plots;
    for (const ChartPlot &p : c.plots) {
        plots.append(QJsonArray{p.type, p.grouping});
    }
    QJsonArray levels;
    for (const QStringList &level : c.categoryLevels) {
        levels.append(QJsonArray::fromStringList(level));
    }
    QJsonArray sources;
    for (const int source : c.categorySource) {
        sources.append(source);
    }
    return {{QStringLiteral("type"), c.type},         {QStringLiteral("grouping"), c.grouping},
            {QStringLiteral("title"), c.title},       {QStringLiteral("legend"), c.legend},
            {QStringLiteral("legendPosition"), c.legendPosition}, {QStringLiteral("series"), series},
            {QStringLiteral("palette"), palette},     {QStringLiteral("plots"), plots},
            {QStringLiteral("categoryLevels"), levels}, {QStringLiteral("categorySource"), sources}};
}

ChartData chart(const QJsonObject &o)
{
    ChartData c;
    c.type = o.value(QStringLiteral("type")).toString();
    c.grouping = o.value(QStringLiteral("grouping")).toString();
    c.title = o.value(QStringLiteral("title")).toString();
    c.legend = o.value(QStringLiteral("legend")).toBool();
    c.legendPosition = o.value(QStringLiteral("legendPosition")).toString();
    for (const QJsonValue &sv : o.value(QStringLiteral("series")).toArray()) {
        const QJsonObject so = sv.toObject();
        ChartSeries s;
        s.name = so.value(QStringLiteral("name")).toString();
        for (const QJsonValue &cat : so.value(QStringLiteral("categories")).toArray()) {
            s.categories.append(cat.toString());
        }
        s.values = reals(so.value(QStringLiteral("values")).toArray());
        s.xValues = reals(so.value(QStringLiteral("xValues")).toArray());
        s.color = color(so.value(QStringLiteral("color")));
        for (const QJsonValue &p : so.value(QStringLiteral("pointColors")).toArray()) {
            s.pointColors.append(color(p));
        }
        s.plot = so.value(QStringLiteral("plot")).toInt(0);
        s.sourceIdx = so.value(QStringLiteral("sourceIdx")).toInt(-1);
        c.series.append(s);
    }
    for (const QJsonValue &p : o.value(QStringLiteral("palette")).toArray()) {
        c.palette.append(color(p));
    }
    for (const QJsonValue &p : o.value(QStringLiteral("plots")).toArray()) {
        const QJsonArray pair = p.toArray();
        c.plots.append(ChartPlot{pair.at(0).toString(), pair.at(1).toString()});
    }
    for (const QJsonValue &level : o.value(QStringLiteral("categoryLevels")).toArray()) {
        QStringList labels;
        for (const QJsonValue &label : level.toArray()) {
            labels.append(label.toString());
        }
        c.categoryLevels.append(labels);
    }
    for (const QJsonValue &source : o.value(QStringLiteral("categorySource")).toArray()) {
        c.categorySource.append(source.toInt(-1));
    }
    return c;
}

} // namespace QindaOffice::Show::Json
