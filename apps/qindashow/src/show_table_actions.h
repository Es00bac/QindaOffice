// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow's table commands (M67, D-076): Insert ▸ Table, the Table
// menu, and the contextual Table Design / Table Layout ribbon tabs —
// rows and columns in and out, merge and split, shading and borders, the
// table style options, distribute rows and columns. Every command acts on
// the selected table: on its selected cells (SlideCanvasItem::
// cellSelection), on the whole table when none are selected; each is ONE
// undo step (SlideCanvasItem::editShape → ShapeEditCommand). The model
// work is show_model's TableEdit; this class only chooses the cells.
//
// AGENT-CONTRACT: the ids below are the dbusmenu activation identity of
// these commands (AGENTS.md rule 5) — app-scoped literals, never renamed,
// never reused; tests and the ribbon layout (ShowRibbonLayout.qml) name
// them too.

#include "action_model.h"
#include "table_edit.h"

#include <QColor>
#include <QObject>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

namespace TableActionId {
inline const char kInsertTable[] = "showInsertTableAction";
inline const char kRowAbove[] = "showTableInsertRowAboveAction";
inline const char kRowBelow[] = "showTableInsertRowBelowAction";
inline const char kColumnLeft[] = "showTableInsertColumnLeftAction";
inline const char kColumnRight[] = "showTableInsertColumnRightAction";
inline const char kDeleteRows[] = "showTableDeleteRowsAction";
inline const char kDeleteColumns[] = "showTableDeleteColumnsAction";
inline const char kMerge[] = "showTableMergeCellsAction";
inline const char kSplit[] = "showTableSplitCellsAction";
inline const char kShading[] = "showTableShadingAction";
inline const char kDistributeRows[] = "showTableDistributeRowsAction";
inline const char kDistributeColumns[] = "showTableDistributeColumnsAction";
} // namespace TableActionId

class ShowTableActions final : public QObject {
    Q_OBJECT
public:
    explicit ShowTableActions(ShowHost *host);

    [[nodiscard]] QList<Quick::MenuNode> tableMenuItems() const;
    void syncFromSelection();

    // The dialogs' answers (ShowHost relays them).
    void insertTable(int rows, int columns);
    void split(int columns, int rows);
    void applyShading(const QColor &color);
    // The line Borders draw with (the ribbon's pen colour; 1 pt).
    void setPenColor(const QColor &color) { m_pen.color = color; }
    void applyBorders(ShowModel::TableEdit::Borders which);

signals:
    void insertTableRequested();
    // Defaults and limits for the Split Cells dialog.
    void splitRequested(int columns, int rows, int maxColumns, int maxRows);
    void shadingRequested(const QColor &current);

private:
    void createActions();
    // The selected table's cells the commands act on: the selection, else
    // the whole table; invalid without a table.
    [[nodiscard]] ShowModel::TableRange target() const;
    bool edit(const QString &label, const std::function<bool(ShowModel::Table &)> &mutate);
    void insertRows(bool above);
    void insertColumns(bool left);
    void deleteRows();
    void deleteColumns();
    void merge();
    void distribute(bool rows);

    ShowHost *m_host;
    QList<Quick::ActionEntry *> m_needTable; // enabled with a table selected
    QHash<QString, Quick::ActionEntry *> m_entries;
    QList<std::pair<Quick::ActionEntry *, ShowModel::TableEdit::StyleOption>> m_options;
    QList<std::pair<Quick::ActionEntry *, ShowModel::TableEdit::Borders>> m_borders;
    ShowModel::CellBorder m_pen{true, QColor(Qt::black), 1.0, QString()};
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Show
