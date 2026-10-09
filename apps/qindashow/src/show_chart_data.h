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
//
// Ownership: the host owns the editor; the editor owns its grid model and
// the cell source behind it (replaced on every load()).

#include "chart_edit.h"

#include <qindatk/editable_grid_model.h>

#include <QObject>
#include <QPointer>
#include <QStringList>
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
};

} // namespace QindaOffice::Apps::Show
