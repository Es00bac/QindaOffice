// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DrawingML charts shared by Word and Show (M93, D-101). This is the
// public boundary: consumers never construct a Presentation or reach into
// the pptx reader/writer. All values are owned, synchronous Core/Gui data.
// Read keeps the producer's caches; write patches only edited data/kinds
// and updates the embedded xlsx through calc_io. An untouched chart should
// be carried by RelationshipCopier instead, to retain its exact bytes.
#include "show_chart.h"
#include "ooxml_copier.h"
#include "ooxml_theme.h"
#include <QVariantMap>

class QPainter;
namespace QindaOffice::Show::ChartIO {
[[nodiscard]] ChartData read(const std::shared_ptr<const Ooxml::Package> &source,
                             const QString &part, const Ooxml::Theme &theme);
[[nodiscard]] QVariantMap toMap(const ChartData &chart);
[[nodiscard]] ChartData fromMap(const QVariantMap &map);
void paint(QPainter *painter, const QRectF &rect, const ChartData &chart);
// Returns the new chart PART name, empty on error. directory is "word"
// or "ppt". source/part may be empty for a newly inserted chart. The
// caller adds its own relationship. External workbook links remain links.
[[nodiscard]] QString write(Ooxml::PackageWriter &writer,
                             const std::shared_ptr<const Ooxml::Package> &source,
                             const QString &part, const ChartData &chart,
                             const ChartData &previous, const QString &directory,
                             QString *error = nullptr);
} // namespace QindaOffice::Show::ChartIO
