// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Mailing labels (M80, D-089 §7): Access's Label Wizard — one record per
// label on a sheet of Avery labels, each label a few lines built from the
// record's fields ("[name]", "[city] & \", \" & [region] & \" \" &
// [postal_code]").
//
// AGENT-CONTRACT (with apps/qindabase label_sheet.cpp, which paints these
// placements for the window, the printer and PDF alike, and with the saved
// report definition, base_objects.h):
//  * A label report is a report object whose definition has "type":
//    "labels" and {"source":{kind,name}, "product":"<vendor>|<number>",
//    "lines":[expression], "sort":[{field, descending}], "skip":n,
//    "copies":n, "fontSize":pt} (LabelSpec::toMap).
//  * Each line is an Access expression over the source's fields, compiled
//    like a query column (base_expression.h): literals quoted, a lookup
//    field reads as the related record's name, an unknown [name] is an
//    error. A line whose text comes out empty is left out (Access's Can
//    Shrink), so a missing second address line closes up.
//  * Geometry is in points (1/72 in) from the page's top-left corner, with
//    Avery's published numbers — the catalog Word's Label Options lists and
//    office_docx's docx_envelopes.cpp carries in its own units; base_engine
//    is Qt Core only, so it keeps its own copy.
//  * Labels fill a sheet left to right, then top to bottom; `skip` leaves
//    the first labels of the first sheet empty (a sheet already used),
//    `copies` puts each record on that many labels in a row.

#include "base_report.h"

#include <QRectF>
#include <QSizeF>

namespace QindaOffice::Base {

struct LabelProduct {
    QString vendor;      // "Avery US Letter", "Avery A4/A5"
    QString product;     // "5160"
    QString description; // "Address Labels"
    QSizeF page;         // points
    qreal top = 0;       // top margin, points
    qreal side = 0;      // left margin, points
    qreal verticalPitch = 0;
    qreal horizontalPitch = 0;
    qreal height = 0;
    qreal width = 0;
    int across = 1;
    int down = 1;
    [[nodiscard]] int count() const { return across * down; }
    [[nodiscard]] QString id() const { return vendor + QLatin1Char('|') + product; }
    [[nodiscard]] QVariantMap toMap() const;
};

[[nodiscard]] QList<LabelProduct> labelProducts();
[[nodiscard]] std::optional<LabelProduct> labelProduct(const QString &id);

struct LabelSpec {
    QString sourceKind = QStringLiteral("table");
    QString sourceName;
    QString productId = QStringLiteral("Avery US Letter|5160");
    QStringList lines;
    QList<ReportSortKey> sort;
    int skip = 0;
    int copies = 1;
    qreal fontSize = 10; // points
    [[nodiscard]] QVariantMap toMap() const; // with "type":"labels" and "version":1
    static LabelSpec fromMap(const QVariantMap &map);
};

struct LabelPlacement {
    int page = 0;
    QRectF rect; // points
    QStringList lines;
};

struct LabelRun {
    bool ok = false;
    QString error;
    LabelProduct product;
    QList<LabelPlacement> labels;
    int pageCount = 0;
    int records = 0;
    bool truncated = false; // the source had more than the limit
    QList<QueryParameter> parameters;
};

// Every record's label text (the source's parameters answered by
// `parameters`), placed on sheets.
[[nodiscard]] LabelRun runLabels(const BaseFile &file, const LabelSpec &spec, const QVariantMap &parameters,
                                 int limit = kReportRowLimit);
// The parameters the label source asks for.
[[nodiscard]] QList<QueryParameter> labelParameters(const BaseFile &file, const LabelSpec &spec);
// Where each label goes: `texts` in order, after `skip` empty labels, each
// `copies` times.
[[nodiscard]] QList<LabelPlacement> placeLabels(const LabelProduct &product, const QList<QStringList> &texts, int skip,
                                                int copies);

} // namespace QindaOffice::Base
