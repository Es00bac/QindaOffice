// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaWrite table operations (M14): row/column insert+delete, cell
// merge+split as persistent named actions (action discipline, AGENTS.md
// rule 5), shared between the Table menu and the formatting toolbar's
// table section. Every operation works on the QTextTable under the
// cursor and no-ops (disabled action) outside one.

#include <QAction>
#include <QList>

class QObject;
class QMenu;
class QToolBar;
class QTextTable;
class QTextTableCell;

namespace QindaOffice::Apps::Write {

class PageTextView;
class WriteDocument;

struct TableActions {
    QAction *insertRowAbove = nullptr;
    QAction *insertRowBelow = nullptr;
    QAction *insertColumnLeft = nullptr;
    QAction *insertColumnRight = nullptr;
    QAction *deleteRow = nullptr;
    QAction *deleteColumn = nullptr;
    QAction *mergeCells = nullptr;
    QAction *splitCell = nullptr;
    [[nodiscard]] QList<QAction *> all() const;
};

class TableController final : public QObject {
    Q_OBJECT
public:
    TableController(PageTextView *view, WriteDocument *document, QObject *parent);

    [[nodiscard]] const TableActions &actions() const { return m_actions; }
    void populateTableMenu(QMenu *menu);
    // Adds the table section after the toolbar's insert buttons.
    void appendToToolBar(QToolBar *toolBar);

private:
    void insertRows(int count, bool above);
    void insertColumns(int count, bool left);
    void deleteRows();
    void deleteColumns();
    void mergeCells();
    void splitCell();
    void syncFromCursor();
    void keepCursorInTable(QTextTable *table, const QTextTableCell &was);

    PageTextView *m_view;
    WriteDocument *m_document;
    TableActions m_actions;
};

} // namespace QindaOffice::Apps::Write
