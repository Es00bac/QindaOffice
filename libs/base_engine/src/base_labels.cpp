// SPDX-License-Identifier: GPL-3.0-or-later
// Mailing labels: the Avery catalog in points, label specs, each record's
// label lines compiled like query columns, and the placement on sheets.
// See base_labels.h for the contract.
#include "base_labels.h"

#include "base_expression.h"
#include "base_objects.h"
#include "base_schema.h"

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

LabelProduct label(const char *vendor, const char *product, const char *description, bool metric, qreal top,
                   qreal side, qreal verticalPitch, qreal horizontalPitch, qreal height, qreal width, int across,
                   int down)
{
    const qreal unit = metric ? 72.0 / 25.4 : 72.0; // millimetres or inches → points
    LabelProduct p;
    p.vendor = s(vendor);
    p.product = s(product);
    p.description = s(description);
    p.page = metric ? QSizeF(210 * unit, 297 * unit) : QSizeF(612, 792);
    p.top = top * unit;
    p.side = side * unit;
    p.verticalPitch = verticalPitch * unit;
    p.horizontalPitch = horizontalPitch * unit;
    p.height = height * unit;
    p.width = width * unit;
    p.across = across;
    p.down = down;
    return p;
}

// The line SQL with the source's fields resolved: `columns` are the
// source's (name, type, lookup) as "q" exposes them.
struct SourceField {
    QString name;
    FieldType type = FieldType::Text;
    LookupDef lookup;
};

Expr::Context lineContext(const QList<SourceField> &fields, const CurrencyFormat &currency,
                          const std::function<std::optional<FieldType>(const LookupDef &)> &displayType)
{
    Expr::Context ctx;
    ctx.currency = currency;
    ctx.parameters = nullptr; // a label asks for nothing of its own
    ctx.resolve = [fields, displayType](const QString &, const QString &name) -> std::optional<Expr::FieldRef> {
        for (const SourceField &f : fields) {
            if (f.name.compare(name, Qt::CaseInsensitive) != 0) {
                continue;
            }
            const QString column = s("\"q\".") + quoteIdentifier(f.name);
            if (f.lookup.isSet() && !f.lookup.displayField.isEmpty()) {
                // A lookup prints the related record's name.
                return Expr::FieldRef{s("(SELECT %1 FROM %2 WHERE %3 = %4)")
                                          .arg(quoteIdentifier(f.lookup.displayField), quoteIdentifier(f.lookup.table),
                                               quoteIdentifier(f.lookup.valueField), column),
                                      displayType(f.lookup).value_or(FieldType::Text)};
            }
            return Expr::FieldRef{column, f.type};
        }
        return std::nullopt;
    };
    return ctx;
}

} // namespace

QVariantMap LabelProduct::toMap() const
{
    return {{s("id"), id()},
            {s("vendor"), vendor},
            {s("product"), product},
            {s("description"), description},
            {s("label"), s("%1 %2 · %3 · %4 per sheet").arg(vendor, product, description).arg(count())},
            {s("across"), across},
            {s("down"), down},
            {s("widthPt"), width},
            {s("heightPt"), height}};
}

QList<LabelProduct> labelProducts()
{
    // AGENT-NOTE: Avery's sheets as Word's Label Options lists them (the
    // same numbers as office_docx's labelProducts()): top margin, side
    // margin, vertical and horizontal pitch, label height and width —
    // inches for US Letter, millimetres for A4 — then labels across, down.
    static const QList<LabelProduct> products{
        label("Avery US Letter", "5160", "Address Labels", false, 0.5, 0.1875, 1, 2.75, 1, 2.625, 3, 10),
        label("Avery US Letter", "5161", "Address Labels", false, 0.5, 0.15625, 1, 4.1875, 1, 4, 2, 10),
        label("Avery US Letter", "5162", "Address Labels", false, 0.8333, 0.15625, 1.3333, 4.1875, 1.3333, 4, 2, 7),
        label("Avery US Letter", "5163", "Shipping Labels", false, 0.5, 0.15625, 2, 4.1875, 2, 4, 2, 5),
        label("Avery US Letter", "5164", "Shipping Labels", false, 0.5, 0.15625, 3.3333, 4.1875, 3.3333, 4, 2, 3),
        label("Avery US Letter", "5167", "Return Address Labels", false, 0.5, 0.28125, 0.5, 2.0625, 0.5, 1.75, 4, 20),
        label("Avery US Letter", "5168", "Shipping Labels", false, 0.5, 0.5, 5, 4, 5, 3.5, 2, 2),
        label("Avery US Letter", "5195", "Return Address Labels", false, 0.5, 0.28125, 0.6667, 2.0625, 0.6667, 1.75, 4, 15),
        label("Avery US Letter", "5366", "File Folder Labels", false, 0.5, 0.53125, 0.6667, 4, 0.6667, 3.4375, 2, 15),
        label("Avery US Letter", "5371", "Business Cards", false, 0.5, 0.75, 2, 3.5, 2, 3.5, 2, 5),
        label("Avery US Letter", "8160", "Address Labels (inkjet)", false, 0.5, 0.1875, 1, 2.75, 1, 2.625, 3, 10),
        label("Avery US Letter", "8163", "Shipping Labels (inkjet)", false, 0.5, 0.15625, 2, 4.1875, 2, 4, 2, 5),
        label("Avery A4/A5", "L7159", "Address Labels", true, 12.9, 7.25, 33.9, 66.04, 33.9, 63.5, 3, 8),
        label("Avery A4/A5", "L7160", "Address Labels", true, 15.15, 7.25, 38.1, 66.04, 38.1, 63.5, 3, 7),
        label("Avery A4/A5", "L7161", "Address Labels", true, 8.7, 7.25, 46.6, 66.04, 46.6, 63.5, 3, 6),
        label("Avery A4/A5", "L7162", "Address Labels", true, 12.9, 4.65, 33.9, 101.6, 33.9, 99.1, 2, 8),
        label("Avery A4/A5", "L7163", "Address Labels", true, 15.15, 4.65, 38.1, 101.6, 38.1, 99.1, 2, 7),
        label("Avery A4/A5", "L7165", "Parcel Labels", true, 13.1, 4.65, 67.7, 101.6, 67.7, 99.1, 2, 4),
        label("Avery A4/A5", "L7173", "Shipping Labels", true, 6, 4.65, 57, 101.6, 57, 99.1, 2, 5),
        label("Avery A4/A5", "L7651", "Mini Labels", true, 10.7, 4.75, 21.2, 40.64, 21.2, 38.1, 5, 13),
        label("Avery A4/A5", "J8160", "Address Labels (inkjet)", true, 15.15, 7.25, 38.1, 66.04, 38.1, 63.5, 3, 7)};
    return products;
}

std::optional<LabelProduct> labelProduct(const QString &id)
{
    for (const LabelProduct &p : labelProducts()) {
        if (p.id() == id) {
            return p;
        }
    }
    return std::nullopt;
}

QVariantMap LabelSpec::toMap() const
{
    QVariantList order;
    for (const ReportSortKey &k : sort) {
        order.append(QVariantMap{{s("field"), k.field}, {s("descending"), k.descending}});
    }
    return {{s("version"), 1},
            {s("type"), s("labels")},
            {s("source"), QVariantMap{{s("kind"), sourceKind}, {s("name"), sourceName}}},
            {s("product"), productId},
            {s("lines"), lines},
            {s("sort"), order},
            {s("skip"), skip},
            {s("copies"), copies},
            {s("fontSize"), fontSize}};
}

LabelSpec LabelSpec::fromMap(const QVariantMap &map)
{
    LabelSpec spec;
    const QVariantMap source = map.value(s("source")).toMap();
    spec.sourceKind = source.value(s("kind"), s("table")).toString();
    spec.sourceName = source.value(s("name")).toString();
    spec.productId = map.value(s("product"), spec.productId).toString();
    spec.lines = map.value(s("lines")).toStringList();
    for (const QVariant &v : map.value(s("sort")).toList()) {
        const QVariantMap m = v.toMap();
        if (!m.value(s("field")).toString().isEmpty()) {
            spec.sort.append({m.value(s("field")).toString(), m.value(s("descending")).toBool()});
        }
    }
    spec.skip = qMax(0, map.value(s("skip")).toInt());
    spec.copies = qBound(1, map.value(s("copies"), 1).toInt(), 100);
    spec.fontSize = qBound(6.0, map.value(s("fontSize"), 10.0).toDouble(), 36.0);
    return spec;
}

QList<LabelPlacement> placeLabels(const LabelProduct &product, const QList<QStringList> &texts, int skip, int copies)
{
    QList<LabelPlacement> out;
    const int perSheet = qMax(1, product.count());
    int slot = qMax(0, skip);
    for (const QStringList &text : texts) {
        for (int copy = 0; copy < qMax(1, copies); ++copy, ++slot) {
            const int k = slot % perSheet;
            const int row = k / product.across;
            const int column = k % product.across;
            out.append({slot / perSheet,
                        QRectF(product.side + column * product.horizontalPitch, product.top + row * product.verticalPitch,
                               product.width, product.height),
                        text});
        }
    }
    return out;
}

QList<QueryParameter> labelParameters(const BaseFile &file, const LabelSpec &spec)
{
    if (spec.sourceKind != QLatin1String("query")) {
        return {};
    }
    const std::optional<SavedObject> q = ObjectStore::load(file, s("query"), spec.sourceName);
    return q ? compileQuery(file, QueryDesign::fromMap(q->definition)).parameters : QList<QueryParameter>();
}

LabelRun runLabels(const BaseFile &file, const LabelSpec &spec, const QVariantMap &parameters, int limit)
{
    LabelRun run;
    const std::optional<LabelProduct> product = labelProduct(spec.productId);
    if (!product) {
        run.error = s("Choose a label product (there is no %1).").arg(spec.productId);
        return run;
    }
    run.product = *product;
    // The source as "q": a table, or the query's own SELECT as a subquery.
    QList<SourceField> fields;
    QString from;
    QList<QueryParameter> declared;
    if (spec.sourceKind == QLatin1String("table")) {
        const std::optional<TableDef> def = readTable(file, spec.sourceName);
        if (!def) {
            run.error = s("There is no table named %1.").arg(spec.sourceName);
            return run;
        }
        for (const FieldDef &f : def->fields) {
            fields.append({f.name, f.type, f.lookup});
        }
        from = quoteIdentifier(def->name);
    } else {
        const std::optional<SavedObject> q = ObjectStore::load(file, s("query"), spec.sourceName);
        if (!q) {
            run.error = s("There is no query named %1.").arg(spec.sourceName);
            return run;
        }
        const CompiledQuery c = compileQuery(file, QueryDesign::fromMap(q->definition), parameters);
        if (!c.ok || c.action) {
            run.error = c.ok ? s("Labels need a select query; %1 changes data.").arg(spec.sourceName) : c.error;
            return run;
        }
        QString error;
        const Statement probe = file.db().prepare(c.sql, &error);
        for (int i = 0; i < probe.columnCount(); ++i) {
            SourceField f{probe.columnName(i), FieldType::Text, {}};
            for (const OutputColumn &o : c.columns) {
                if (o.name == f.name && o.typeKnown) {
                    f.type = o.type;
                }
            }
            if (const QString origin = probe.originTable(i); !origin.isEmpty()) {
                if (const std::optional<TableDef> t = readTable(file, origin)) {
                    if (const FieldDef *src = t->field(probe.originColumn(i))) {
                        f.type = src->type;
                        f.lookup = src->lookup;
                    }
                }
            }
            fields.append(f);
        }
        from = s("(%1)").arg(c.sql);
        declared = c.parameters;
        run.parameters = c.parameters;
    }
    const auto displayType = [&file](const LookupDef &lookup) -> std::optional<FieldType> {
        if (const std::optional<TableDef> t = readTable(file, lookup.table)) {
            if (const FieldDef *f = t->field(lookup.displayField)) {
                return f->type;
            }
        }
        return std::nullopt;
    };
    const CurrencyFormat currency = file.currency();
    const Expr::Context ctx = lineContext(fields, currency, displayType);
    QStringList select;
    QList<OutputColumn> hints;
    for (int i = 0; i < spec.lines.size(); ++i) {
        QString text = spec.lines.at(i).trimmed();
        if (text.startsWith(QLatin1Char('='))) {
            text = text.mid(1).trimmed(); // Access writes a control source "=…"
        }
        if (text.isEmpty()) {
            continue;
        }
        const Expr::Compiled line = Expr::expression(text, ctx);
        if (!line.ok() || line.aggregate) {
            run.error = s("Label line %1: %2").arg(i + 1).arg(line.ok() ? s("a label cannot total records.") : line.error);
            return run;
        }
        select.append(s("%1 AS %2").arg(line.sql, quoteIdentifier(s("l%1").arg(i + 1))));
        hints.append({s("l%1").arg(i + 1), line.type.value_or(FieldType::Text), line.type.has_value(), {}, {}});
    }
    if (select.isEmpty()) {
        run.error = s("Put at least one field on the label.");
        return run;
    }
    QStringList order;
    for (const ReportSortKey &k : spec.sort) {
        if (std::none_of(fields.cbegin(), fields.cend(), [&](const SourceField &f) {
                return f.name.compare(k.field, Qt::CaseInsensitive) == 0;
            })) {
            run.error = s("The labels sort by %1, which is not in %2.").arg(k.field, spec.sourceName);
            return run;
        }
        order.append(s("\"q\".%1%2").arg(quoteIdentifier(k.field), k.descending ? s(" DESC") : QString()));
    }
    const QString sql = s("SELECT %1\nFROM %2 AS \"q\"%3")
                            .arg(select.join(s(", ")), from,
                                 order.isEmpty() ? QString() : s("\nORDER BY ") + order.join(s(", ")));
    const QueryResult result = runSelect(file, sql, declared, parameters, limit + 1, hints);
    if (!result.ok) {
        run.error = result.error;
        return run;
    }
    QList<QStringList> texts;
    for (const QVariantList &row : result.rows) {
        if (texts.size() >= limit) {
            run.truncated = true;
            break;
        }
        QStringList lines;
        for (int i = 0; i < hints.size(); ++i) {
            const QString text = displayText(hints.at(i).type, row.value(i), currency).trimmed();
            if (!text.isEmpty()) {
                lines.append(text); // Can Shrink: an empty line closes up
            }
        }
        texts.append(lines);
    }
    run.records = int(texts.size());
    run.labels = placeLabels(*product, texts, spec.skip, spec.copies);
    run.pageCount = run.labels.isEmpty() ? 0 : run.labels.last().page + 1;
    run.ok = true;
    return run;
}

} // namespace QindaOffice::Base
