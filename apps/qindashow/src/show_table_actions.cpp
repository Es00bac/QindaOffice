// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_table_actions.h"

#include "menu_tree.h"
#include "pptx_reader.h"
#include "show_document.h"
#include "show_host.h"
#include "slide_canvas_item.h"

#include <QUuid>

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;
using ShowModel::TableRange;
namespace TableEdit = ShowModel::TableEdit;

namespace {

struct OptionEntry {
    const char *id;
    const char *text;
    TableEdit::StyleOption option;
};

const OptionEntry kOptions[] = {
    {"showTableHeaderRowAction", QT_TR_NOOP("&Header Row"), TableEdit::StyleOption::HeaderRow},
    {"showTableTotalRowAction", QT_TR_NOOP("&Total Row"), TableEdit::StyleOption::TotalRow},
    {"showTableBandedRowsAction", QT_TR_NOOP("&Banded Rows"), TableEdit::StyleOption::BandedRows},
    {"showTableFirstColumnAction", QT_TR_NOOP("&First Column"), TableEdit::StyleOption::FirstColumn},
    {"showTableLastColumnAction", QT_TR_NOOP("&Last Column"), TableEdit::StyleOption::LastColumn},
    {"showTableBandedColumnsAction", QT_TR_NOOP("Banded &Columns"), TableEdit::StyleOption::BandedColumns},
};

struct BorderEntry {
    const char *id;
    const char *text;
    const char *icon;
    TableEdit::Borders borders;
};

const BorderEntry kBorders[] = {
    {"showTableBordersAllAction", QT_TR_NOOP("&All Borders"), "grid-3x3", TableEdit::Borders::All},
    {"showTableBordersOutsideAction", QT_TR_NOOP("&Outside Borders"), "square", TableEdit::Borders::Outside},
    {"showTableBordersInsideAction", QT_TR_NOOP("&Inside Borders"), "grid-2x2", TableEdit::Borders::Inside},
    {"showTableBordersTopAction", QT_TR_NOOP("&Top Border"), "panel-top", TableEdit::Borders::Top},
    {"showTableBordersBottomAction", QT_TR_NOOP("&Bottom Border"), "panel-bottom", TableEdit::Borders::Bottom},
    {"showTableBordersLeftAction", QT_TR_NOOP("&Left Border"), "panel-left", TableEdit::Borders::Left},
    {"showTableBordersRightAction", QT_TR_NOOP("&Right Border"), "panel-right", TableEdit::Borders::Right},
    {"showTableBordersInsideHorizontalAction", QT_TR_NOOP("Inside &Horizontal Border"),
     "square-centerline-dashed-horizontal", TableEdit::Borders::InsideHorizontal},
    {"showTableBordersInsideVerticalAction", QT_TR_NOOP("Inside &Vertical Border"), "square-centerline-dashed-vertical",
     TableEdit::Borders::InsideVertical},
    {"showTableBordersNoneAction", QT_TR_NOOP("&No Border"), "square-dashed", TableEdit::Borders::None},
};

MenuNode a(const char *id)
{
    return MenuNode::action(QString::fromLatin1(id));
}

} // namespace

ShowTableActions::ShowTableActions(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    createActions();
    SlideCanvasItem *canvas = host->canvas();
    connect(canvas, &SlideCanvasItem::selectionChanged, this, [this] { syncFromSelection(); });
    connect(canvas, &SlideCanvasItem::cellSelectionChanged, this, [this] { syncFromSelection(); });
    connect(canvas, &SlideCanvasItem::slideEdited, this, [this] { syncFromSelection(); });
    syncFromSelection();
}

void ShowTableActions::createActions()
{
    using namespace TableActionId;
    Quick::ActionModel *model = m_host->actions();
    const auto make = [this, model](const char *id, const QString &text, const char *icon, bool needsTable = true) {
        ActionEntry *entry = model->add(QString::fromLatin1(id), text, {}, icon != nullptr ? QString::fromLatin1(icon) : QString());
        m_entries.insert(entry->id(), entry);
        if (needsTable) {
            m_needTable.append(entry);
        }
        return entry;
    };
    connect(make(kInsertTable, tr("&Table…"), "table", false), &ActionEntry::triggered, this, &ShowTableActions::insertTableRequested);
    connect(make(kRowAbove, tr("Insert Row &Above"), "between-horizontal-start"), &ActionEntry::triggered, this,
            [this] { insertRows(true); });
    connect(make(kRowBelow, tr("Insert Row &Below"), "between-horizontal-end"), &ActionEntry::triggered, this,
            [this] { insertRows(false); });
    connect(make(kColumnLeft, tr("Insert Column &Left"), "between-vertical-start"), &ActionEntry::triggered, this,
            [this] { insertColumns(true); });
    connect(make(kColumnRight, tr("Insert Column &Right"), "between-vertical-end"), &ActionEntry::triggered, this,
            [this] { insertColumns(false); });
    connect(make(kDeleteRows, tr("&Delete Rows"), "list-minus"), &ActionEntry::triggered, this, [this] { deleteRows(); });
    connect(make(kDeleteColumns, tr("Delete &Columns"), "columns-2"), &ActionEntry::triggered, this, [this] { deleteColumns(); });
    connect(make(kMerge, tr("&Merge Cells"), "table-cells-merge"), &ActionEntry::triggered, this, [this] { merge(); });
    connect(make(kSplit, tr("S&plit Cells…"), "table-cells-split"), &ActionEntry::triggered, this, [this] {
        const TableRange range = target();
        if (!range.isValid()) {
            return;
        }
        const bool merged = range.rows() > 1 || range.columns() > 1;
        emit splitRequested(merged ? range.columns() : 2, merged ? range.rows() : 1, merged ? range.columns() : 20,
                            merged ? range.rows() : 20);
    });
    connect(make(kShading, tr("Cell &Shading…"), "paint-bucket"), &ActionEntry::triggered, this, [this] {
        const QString id = m_host->canvas()->tableShapeId();
        const ShowModel::Shape *shape = id.isEmpty() ? nullptr : m_host->canvas()->slide()->findShape(id);
        const TableRange range = target();
        QColor current(Qt::white);
        if (shape != nullptr && range.isValid()) {
            const ShowModel::TableCell &cell = shape->table.rows.at(range.top).cells.at(range.left);
            current = cell.fill.isValid() ? cell.fill : shape->table.regionAt(range.top, range.left).fill;
        }
        emit shadingRequested(current.isValid() ? current : QColor(Qt::white));
    });
    connect(make(kDistributeRows, tr("Distribute &Rows"), "align-vertical-distribute-center"), &ActionEntry::triggered, this,
            [this] { distribute(true); });
    connect(make(kDistributeColumns, tr("Distribute Colu&mns"), "align-horizontal-distribute-center"), &ActionEntry::triggered,
            this, [this] { distribute(false); });
    for (const BorderEntry &entry : kBorders) {
        const TableEdit::Borders which = entry.borders;
        ActionEntry *action = make(entry.id, tr(entry.text), entry.icon);
        connect(action, &ActionEntry::triggered, this, [this, which] { applyBorders(which); });
        m_borders.append({action, which});
    }
    for (const OptionEntry &entry : kOptions) {
        ActionEntry *action = make(entry.id, tr(entry.text), nullptr);
        action->setCheckable(true);
        const TableEdit::StyleOption option = entry.option;
        connect(action, &ActionEntry::toggled, this, [this, option](bool on) {
            if (!m_syncing) {
                edit(tr("Table style options"), [option, on](ShowModel::Table &t) {
                    TableEdit::setStyleOption(t, option, on);
                    return true;
                });
            }
        });
        m_options.append({action, option});
    }
}

TableRange ShowTableActions::target() const
{
    SlideCanvasItem *canvas = m_host->canvas();
    const QString id = canvas->tableShapeId();
    const ShowModel::Shape *shape = id.isEmpty() ? nullptr : canvas->slide()->findShape(id);
    if (shape == nullptr) {
        return {-1, -1, -1, -1};
    }
    const TableRange cells = canvas->cellSelection();
    return cells.isValid() ? cells : TableRange{0, 0, int(shape->table.rows.size()) - 1, int(shape->table.columns.size()) - 1};
}

bool ShowTableActions::edit(const QString &label, const std::function<bool(ShowModel::Table &)> &mutate)
{
    const QString id = m_host->canvas()->tableShapeId();
    return !id.isEmpty() && m_host->canvas()->editShape(id, label, [&mutate](ShowModel::Shape &s) { return mutate(s.table); });
}

void ShowTableActions::insertRows(bool above)
{
    const TableRange range = target();
    const QString id = m_host->canvas()->tableShapeId();
    const int at = above ? range.top : range.bottom + 1;
    const int count = range.rows();
    if (range.isValid() && edit(above ? tr("Insert rows above") : tr("Insert rows below"), [&](ShowModel::Table &t) {
            return TableEdit::insertRows(t, at, count, above ? range.top : range.bottom);
        })) {
        m_host->canvas()->selectCells(id, {at, range.left, at + count - 1, range.right});
    }
}

void ShowTableActions::insertColumns(bool left)
{
    const TableRange range = target();
    const QString id = m_host->canvas()->tableShapeId();
    const int at = left ? range.left : range.right + 1;
    const int count = range.columns();
    if (range.isValid() && edit(left ? tr("Insert columns left") : tr("Insert columns right"), [&](ShowModel::Table &t) {
            return TableEdit::insertColumns(t, at, count, left ? range.left : range.right);
        })) {
        m_host->canvas()->selectCells(id, {range.top, at, range.bottom, at + count - 1});
    }
}

void ShowTableActions::deleteRows()
{
    SlideCanvasItem *canvas = m_host->canvas();
    const TableRange range = canvas->cellSelection();
    const QString id = canvas->tableShapeId();
    const ShowModel::Shape *shape = id.isEmpty() ? nullptr : canvas->slide()->findShape(id);
    if (shape == nullptr || !range.isValid()) {
        return;
    }
    if (range.rows() >= shape->table.rows.size()) {
        canvas->deleteSelected(); // every row: the table goes, as in PowerPoint
        return;
    }
    const int remaining = int(shape->table.rows.size()) - range.rows();
    if (edit(tr("Delete rows"), [&](ShowModel::Table &t) { return TableEdit::deleteRows(t, range.top, range.rows()); })) {
        const int row = qMin(range.top, remaining - 1);
        canvas->selectCells(id, {row, range.left, row, range.left});
    }
}

void ShowTableActions::deleteColumns()
{
    SlideCanvasItem *canvas = m_host->canvas();
    const TableRange range = canvas->cellSelection();
    const QString id = canvas->tableShapeId();
    const ShowModel::Shape *shape = id.isEmpty() ? nullptr : canvas->slide()->findShape(id);
    if (shape == nullptr || !range.isValid()) {
        return;
    }
    if (range.columns() >= shape->table.columns.size()) {
        canvas->deleteSelected();
        return;
    }
    const int remaining = int(shape->table.columns.size()) - range.columns();
    if (edit(tr("Delete columns"), [&](ShowModel::Table &t) { return TableEdit::deleteColumns(t, range.left, range.columns()); })) {
        const int column = qMin(range.left, remaining - 1);
        canvas->selectCells(id, {range.top, column, range.top, column});
    }
}

void ShowTableActions::merge()
{
    const TableRange range = m_host->canvas()->cellSelection();
    const QString id = m_host->canvas()->tableShapeId();
    if (range.isValid() && edit(tr("Merge cells"), [range](ShowModel::Table &t) { return TableEdit::merge(t, range); })) {
        m_host->canvas()->selectCells(id, range);
    }
}

void ShowTableActions::split(int columns, int rows)
{
    const TableRange range = target();
    const QString id = m_host->canvas()->tableShapeId();
    if (range.isValid() && edit(tr("Split cells"), [&](ShowModel::Table &t) { return TableEdit::split(t, range.top, range.left, columns, rows); })) {
        m_host->canvas()->selectCells(id, {range.top, range.left, range.top, range.left});
    }
}

void ShowTableActions::applyShading(const QColor &color)
{
    const TableRange range = target();
    if (range.isValid()) {
        edit(tr("Cell shading"), [&](ShowModel::Table &t) {
            TableEdit::setFill(t, range, color);
            return true;
        });
    }
}

void ShowTableActions::applyBorders(TableEdit::Borders which)
{
    const TableRange range = target();
    if (range.isValid()) {
        edit(tr("Borders"), [&](ShowModel::Table &t) {
            TableEdit::setBorders(t, range, which, m_pen);
            return true;
        });
    }
}

void ShowTableActions::distribute(bool rows)
{
    const TableRange range = target();
    const QString id = m_host->canvas()->tableShapeId();
    const ShowModel::Shape *shape = id.isEmpty() ? nullptr : m_host->canvas()->slide()->findShape(id);
    if (shape == nullptr || !range.isValid()) {
        return;
    }
    // Equal shares of what the rows are drawn at (text may have grown them).
    const ShowModel::TableGrid grid = ShowModel::tableGrid(shape->table, shape->rect.normalized());
    const QList<qreal> &edges = rows ? grid.ys : grid.xs;
    const int first = rows ? range.top : range.left;
    const int last = rows ? range.bottom : range.right;
    const qreal share = (edges.at(last + 1) - edges.at(first)) / (last - first + 1);
    edit(rows ? tr("Distribute rows") : tr("Distribute columns"), [&](ShowModel::Table &t) {
        for (int i = first; i <= last; ++i) {
            if (rows) {
                TableEdit::setRowHeight(t, i, share);
            } else {
                TableEdit::setColumnWidth(t, i, share);
            }
        }
        return true;
    });
}

void ShowTableActions::insertTable(int rows, int columns)
{
    SlideCanvasItem *canvas = m_host->canvas();
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    if (canvas->slide() == nullptr || rows < 1 || columns < 1) {
        return;
    }
    // PowerPoint's: the deck's default table style, rows 0.4 in tall
    // (370840 EMU), three quarters of the slide wide, centred.
    const ShowModel::NewTableStyle style = ShowModel::newTableStyle(*deck, *canvas->slide());
    const QSizeF slide = deck->slideSize;
    const qreal width = slide.width() * 0.75;
    ShowModel::Shape shape;
    shape.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    shape.kind = ShowModel::ShapeKind::Table;
    shape.fill = Qt::transparent;
    shape.strokeWidth = 0;
    shape.table = TableEdit::create(rows, columns, width, 370840.0 / 12700.0, style.styleId, style.look);
    shape.rect = QRectF((slide.width() - width) / 2, slide.height() * 0.28, width, 0);
    TableEdit::fitFrame(shape);
    canvas->insertShape(shape);
}

void ShowTableActions::syncFromSelection()
{
    m_syncing = true;
    SlideCanvasItem *canvas = m_host->canvas();
    const QString id = canvas->tableShapeId();
    const ShowModel::Shape *shape = id.isEmpty() ? nullptr : canvas->slide()->findShape(id);
    for (ActionEntry *entry : std::as_const(m_needTable)) {
        entry->setEnabled(shape != nullptr);
    }
    const TableRange cells = canvas->cellSelection();
    const bool oneBlock = shape != nullptr && cells.isValid() && TableEdit::blockAt(shape->table, cells.top, cells.left) == cells;
    m_entries.value(QString::fromLatin1(TableActionId::kMerge))->setEnabled(shape != nullptr && cells.isValid() && !oneBlock);
    m_entries.value(QString::fromLatin1(TableActionId::kSplit))->setEnabled(oneBlock);
    m_entries.value(QString::fromLatin1(TableActionId::kDeleteRows))->setEnabled(shape != nullptr && cells.isValid());
    m_entries.value(QString::fromLatin1(TableActionId::kDeleteColumns))->setEnabled(shape != nullptr && cells.isValid());
    m_entries.value(QString::fromLatin1(TableActionId::kInsertTable))->setEnabled(canvas->slide() != nullptr);
    for (const auto &[action, option] : std::as_const(m_options)) {
        action->setChecked(shape != nullptr && TableEdit::styleOption(shape->table, option));
    }
    m_syncing = false;
}

QList<MenuNode> ShowTableActions::tableMenuItems() const
{
    using namespace TableActionId;
    QList<MenuNode> borders;
    for (const auto &[action, which] : m_borders) {
        borders.append(MenuNode::action(action->id()));
    }
    QList<MenuNode> options;
    for (const auto &[action, option] : m_options) {
        options.append(MenuNode::action(action->id()));
    }
    // Insert ▸ Table names kInsertTable: a menu tree holds an id once (the
    // panel export's validator refuses duplicates).
    return {a(kRowAbove), a(kRowBelow), a(kColumnLeft), a(kColumnRight),
            MenuNode::separator(), a(kDeleteRows), a(kDeleteColumns), MenuNode::separator(), a(kMerge), a(kSplit),
            MenuNode::separator(), a(kShading),
            MenuNode::submenu(QStringLiteral("showTableBordersMenu"), tr("&Borders"), borders),
            MenuNode::submenu(QStringLiteral("showTableOptionsMenu"), tr("Style &Options"), options),
            MenuNode::separator(), a(kDistributeRows), a(kDistributeColumns)};
}

} // namespace QindaOffice::Apps::Show
