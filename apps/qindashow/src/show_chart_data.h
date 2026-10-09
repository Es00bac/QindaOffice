// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Edit Data for a chart (M67, D-076): the selected chart's data sheet in
// QindaTK's editable grid — the first row the series names, the first
// column the category labels (x values for a scatter chart), the values
// between, as PowerPoint's chart sheet lays them out — and its kind, among
// the kinds QindaShow draws. apply() writes both back as ONE undo step
// (SlideCanvasItem::editShape); the pptx writer then rewrites the chart
// part's caches and formulas and its embedded workbook (show_model's
// chart_edit.h, chart_workbook.h), so PowerPoint's own Edit Data shows the
// same numbers.
// M82 (D-091): a combo chart shows one kind per plot (the kinds with
// axes), and every row and column remembers the category or series it was
// loaded from, so a deleted or inserted one leaves the others' colours,
// plots, data labels and category groups where they belong.
//
// Ownership: the host owns the editor; the editor owns its grid model and
// the cell source behind it (replaced on every load()).

#include "chart_edit.h"

#include <qindatk/editable_grid_model.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Apps::Show {

class SlideCanvasItem;

class ChartDataEditor final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the QindaShow host.")
    Q_PROPERTY(QindaTK::EditableGridModel *grid READ grid CONSTANT)
    Q_PROPERTY(QStringList kindNames READ kindNames CONSTANT)
    Q_PROPERTY(int kindIndex READ kindIndex WRITE setKindIndex NOTIFY kindChanged)
    Q_PROPERTY(int categoryCount READ categoryCount NOTIFY shapeChanged)
    Q_PROPERTY(int seriesCount READ seriesCount NOTIFY shapeChanged)
    // M82: a combo chart's plots [{label — its series' names, comboIndex}]
    // (empty: one plot) and the kinds a plot may take.
    Q_PROPERTY(QVariantList plots READ plots NOTIFY kindChanged)
    Q_PROPERTY(QStringList comboKindNames READ comboKindNames CONSTANT)
public:
    ChartDataEditor(SlideCanvasItem *canvas, QObject *parent);

    // Loads the selected chart; false when the selection is not one chart
    // with data.
    bool load();
    [[nodiscard]] QindaTK::EditableGridModel *grid() const { return m_grid; }
    [[nodiscard]] QStringList kindNames() const;
    [[nodiscard]] int kindIndex() const { return m_kind; }
    void setKindIndex(int index);
    [[nodiscard]] int categoryCount() const;
    [[nodiscard]] int seriesCount() const;
    [[nodiscard]] QVariantList plots() const;
    [[nodiscard]] QStringList comboKindNames() const;
    // Plot `plot` of a combo chart made comboKindNames()[index].
    Q_INVOKABLE void setPlotKind(int plot, int index);

    Q_INVOKABLE void addCategory();
    Q_INVOKABLE void addSeries();
    // Grid rows / columns (row 0 and column 0 are the headers).
    Q_INVOKABLE void removeCategory(int row);
    Q_INVOKABLE void removeSeries(int column);
    // Writes the sheet and the kind into the chart; false when nothing
    // changed or the chart is gone.
    Q_INVOKABLE bool apply();

    // The sheet as it stands (what apply() writes).
    [[nodiscard]] ShowModel::ChartEdit::ChartTable table() const;

signals:
    void kindChanged();
    void shapeChanged();

private:
    void setCell(int row, int column, const QString &text);

    SlideCanvasItem *m_canvas;
    QindaTK::EditableGridModel *m_grid;
    QPointer<QindaTK::GridCellSource> m_source;
    QString m_shapeId;
    int m_kind = 0;
    int m_loadedKind = 0;
    // M82: where each grid row / column came from (by the grid's ids).
    QHash<QString, int> m_rowSource;
    QHash<QString, int> m_columnSource;
    // A combo chart's plots: their kinds (kinds() indices) and labels.
    QList<int> m_plotKinds;
    QList<int> m_loadedPlotKinds;
    QStringList m_plotLabels;
};

} // namespace QindaOffice::Apps::Show
