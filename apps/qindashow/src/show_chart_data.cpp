// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_chart_data.h"

#include "slide_canvas_item.h"

#include <QCoreApplication>

#include <cmath>

namespace QindaOffice::Apps::Show {

namespace {

namespace ChartEdit = ShowModel::ChartEdit;

// The sheet as loaded: values by row and column id. The grid keeps the
// user's edits over it (its override layer); the headers read bold and the
// values right-aligned, as in Excel.
class ChartSheetSource final : public QindaTK::GridCellSource, public QindaTK::GridCellSourceExtension {
public:
    using QindaTK::GridCellSource::GridCellSource;

    QHash<QString, QString> values;
    QString firstRow;
    QString firstColumn;

    [[nodiscard]] static QString key(const QString &rowId, const QString &columnId)
    {
        return rowId + QChar(0x1f) + columnId;
    }
    QVariant value(const QString &rowId, const QString &columnId) const override
    {
        return values.value(key(rowId, columnId));
    }
    QVariant editValue(const QString &rowId, const QString &columnId) const override
    {
        return value(rowId, columnId);
    }
    QVariantMap cellStyle(const QString &rowId, const QString &columnId) const override
    {
        if (rowId == firstRow || columnId == firstColumn) {
            return {{QStringLiteral("bold"), true}};
        }
        return {{QStringLiteral("horizontalAlignment"), QStringLiteral("right")}, {QStringLiteral("numeric"), true}};
    }
    bool commitChanges(const QString &, const QVariantList &, QString *) override
    {
        return false; // the grid keeps the edits (sourceCommits is off)
    }
};

} // namespace

ChartDataEditor::ChartDataEditor(SlideCanvasItem *canvas, QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
    , m_grid(new QindaTK::EditableGridModel(this))
{
    m_grid->setObjectName(QStringLiteral("chartDataGrid"));
}

QStringList ChartDataEditor::kindNames() const
{
    QStringList names;
    for (const ChartEdit::ChartKind &kind : ChartEdit::kinds()) {
        names.append(QCoreApplication::translate("ChartEdit", kind.label));
    }
    return names;
}

void ChartDataEditor::setKindIndex(int index)
{
    if (index >= 0 && index < ChartEdit::kinds().size() && index != m_kind) {
        m_kind = index;
        emit kindChanged();
    }
}

QStringList ChartDataEditor::comboKindNames() const
{
    QStringList names;
    for (int i = 0; i < ChartEdit::kinds().size(); ++i) {
        if (ChartEdit::comboKind(i)) {
            names.append(QCoreApplication::translate("ChartEdit", ChartEdit::kinds().at(i).label));
        }
    }
    return names;
}

QVariantList ChartDataEditor::plots() const
{
    // The combo list's index of each plot's kind.
    QList<int> comboKinds;
    for (int i = 0; i < ChartEdit::kinds().size(); ++i) {
        if (ChartEdit::comboKind(i)) {
            comboKinds.append(i);
        }
    }
    QVariantList out;
    for (int p = 0; p < m_plotKinds.size(); ++p) {
        out.append(QVariantMap{{QStringLiteral("label"), m_plotLabels.value(p)},
                               {QStringLiteral("comboIndex"), qMax(0, int(comboKinds.indexOf(m_plotKinds.at(p))))}});
    }
    return out;
}

void ChartDataEditor::setPlotKind(int plot, int index)
{
    QList<int> comboKinds;
    for (int i = 0; i < ChartEdit::kinds().size(); ++i) {
        if (ChartEdit::comboKind(i)) {
            comboKinds.append(i);
        }
    }
    if (plot >= 0 && plot < m_plotKinds.size() && index >= 0 && index < comboKinds.size() && m_plotKinds.at(plot) != comboKinds.at(index)) {
        m_plotKinds[plot] = comboKinds.at(index);
        emit kindChanged();
    }
}

int ChartDataEditor::categoryCount() const
{
    return qMax(0, m_grid->rows() - 1);
}

int ChartDataEditor::seriesCount() const
{
    return qMax(0, m_grid->columns() - 1);
}

bool ChartDataEditor::load()
{
    const QString id = m_canvas->selectedShapeIds().size() == 1 ? m_canvas->selectedShapeId() : QString();
    const ShowModel::Shape *shape = !id.isEmpty() && m_canvas->slide() != nullptr ? m_canvas->slide()->findShape(id) : nullptr;
    if (shape == nullptr || shape->kind != ShowModel::ShapeKind::Chart || shape->chart.isEmpty()) {
        return false;
    }
    m_shapeId = shape->id;
    const ChartEdit::ChartTable table = ChartEdit::tableOf(shape->chart);
    // A kind QindaShow does not name (a 3-D standard bar, a radar drawn as
    // lines) shows as the closest one, and stays itself unless changed.
    m_kind = ChartEdit::kindIndex(shape->chart);
    if (m_kind < 0) {
        m_kind = 0;
        for (int i = 0; i < ChartEdit::kinds().size(); ++i) {
            if (ChartEdit::kinds().at(i).type == shape->chart.type) {
                m_kind = i;
                break;
            }
        }
    }
    m_loadedKind = m_kind;
    // A combo chart: each plot's kind, named by its series.
    m_plotKinds.clear();
    m_plotLabels.clear();
    if (shape->chart.plots.size() > 1) {
        for (int p = 0; p < shape->chart.plots.size(); ++p) {
            QStringList names;
            for (const ShowModel::ChartSeries &series : shape->chart.series) {
                if (series.plot == p) {
                    names.append(series.name);
                }
            }
            m_plotKinds.append(qMax(0, ChartEdit::plotKindIndex(shape->chart, p)));
            // The plot is named by its series, as PowerPoint's Combo list
            // names each row (the label column is narrow).
            m_plotLabels.append(names.join(QStringLiteral(", ")));
        }
    }
    m_loadedPlotKinds = m_plotKinds;
    auto *source = new ChartSheetSource(this);
    QindaTK::GridCellSource *old = m_source;
    m_grid->setSource(source);
    m_source = source;
    delete old;
    m_grid->setDimensions(int(table.categories.size()) + 1, int(table.series.size()) + 1);
    source->firstRow = m_grid->rowIdAt(0);
    source->firstColumn = m_grid->columnIdAt(0);
    m_rowSource.clear();
    m_columnSource.clear();
    for (int s = 0; s < table.series.size(); ++s) {
        source->values.insert(ChartSheetSource::key(source->firstRow, m_grid->columnIdAt(s + 1)), table.series.at(s));
        m_columnSource.insert(m_grid->columnIdAt(s + 1), s);
    }
    for (int k = 0; k < table.categories.size(); ++k) {
        const QString row = m_grid->rowIdAt(k + 1);
        m_rowSource.insert(row, k);
        source->values.insert(ChartSheetSource::key(row, source->firstColumn), table.categories.at(k));
        for (int s = 0; s < table.series.size(); ++s) {
            source->values.insert(ChartSheetSource::key(row, m_grid->columnIdAt(s + 1)),
                                  ChartEdit::valueText(table.values.at(s).value(k, std::nan(""))));
        }
    }
    m_grid->setFrozenRows(1);
    m_grid->setFrozenColumns(1);
    m_grid->notifySourceChanged(0, m_grid->rows(), 0, m_grid->columns());
    emit kindChanged();
    emit shapeChanged();
    return true;
}

void ChartDataEditor::setCell(int row, int column, const QString &text)
{
    if (m_grid->beginEdit(tr("Edit data"))) {
        m_grid->stageCell(m_grid->rowIdAt(row), m_grid->columnIdAt(column), text);
        m_grid->commitEdit();
    }
}

void ChartDataEditor::addCategory()
{
    const int row = m_grid->rows();
    if (m_grid->insertGeneratedRowsAt(row, 1)) {
        setCell(row, 0, tr("Category %1").arg(row));
        emit shapeChanged();
    }
}

void ChartDataEditor::addSeries()
{
    const int column = m_grid->columns();
    if (m_grid->insertGeneratedColumnsAt(column, 1)) {
        setCell(0, column, tr("Series %1").arg(column));
        emit shapeChanged();
    }
}

void ChartDataEditor::removeCategory(int row)
{
    if (row >= 1 && m_grid->rows() > 2 && m_grid->removeRowsAt(row, 1)) {
        emit shapeChanged();
    }
}

void ChartDataEditor::removeSeries(int column)
{
    if (column >= 1 && m_grid->columns() > 2 && m_grid->removeColumnsAt(column, 1)) {
        emit shapeChanged();
    }
}

ChartEdit::ChartTable ChartDataEditor::table() const
{
    ChartEdit::ChartTable table;
    const QString header = m_grid->rowIdAt(0);
    const QString labels = m_grid->columnIdAt(0);
    for (int c = 1; c < m_grid->columns(); ++c) {
        table.series.append(m_grid->cellValue(header, m_grid->columnIdAt(c)).toString());
        table.values.append(QList<qreal>());
        table.seriesSource.append(m_columnSource.value(m_grid->columnIdAt(c), -1));
    }
    for (int r = 1; r < m_grid->rows(); ++r) {
        const QString row = m_grid->rowIdAt(r);
        table.categories.append(m_grid->cellValue(row, labels).toString());
        table.categorySource.append(m_rowSource.value(row, -1));
        for (int c = 1; c < m_grid->columns(); ++c) {
            table.values[c - 1].append(ChartEdit::parseValue(m_grid->cellValue(row, m_grid->columnIdAt(c)).toString()));
        }
    }
    return table;
}

bool ChartDataEditor::apply()
{
    if (m_shapeId.isEmpty()) {
        return false;
    }
    const ChartEdit::ChartTable sheet = table();
    const int kind = m_kind;
    const bool kindChanged = m_kind != m_loadedKind && m_plotKinds.isEmpty();
    const QList<int> plotKinds = m_plotKinds;
    const QList<int> loadedPlotKinds = m_loadedPlotKinds;
    const bool done = m_canvas->editShape(m_shapeId, tr("Edit chart data"), [&](ShowModel::Shape &shape) {
        if (shape.kind != ShowModel::ShapeKind::Chart) {
            return false;
        }
        if (kindChanged) {
            ChartEdit::setKind(shape.chart, kind);
        }
        for (int p = 0; p < plotKinds.size(); ++p) {
            if (plotKinds.at(p) != loadedPlotKinds.value(p, -1)) {
                ChartEdit::setPlotKind(shape.chart, p, plotKinds.at(p));
            }
        }
        ChartEdit::setTable(shape.chart, sheet);
        return true;
    });
    m_loadedKind = m_kind;
    m_loadedPlotKinds = m_plotKinds;
    return done;
}

} // namespace QindaOffice::Apps::Show
