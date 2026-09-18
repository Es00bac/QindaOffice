// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaWrite table operations (M14): row/column insert+delete, cell
// merge+split as persistent entries (rule 5), shared between the Table
// menu and the format strip's table section. Every operation works on the
// QTextTable under the cursor and no-ops (disabled entry) outside one.

#include "action_model.h"

#include <QList>
#include <QObject>

class QTextTable;
class QTextTableCell;

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

struct TableActions {
    Quick::ActionEntry *insertRowAbove = nullptr;
    Quick::ActionEntry *insertRowBelow = nullptr;
    Quick::ActionEntry *insertColumnLeft = nullptr;
    Quick::ActionEntry *insertColumnRight = nullptr;
    Quick::ActionEntry *deleteRow = nullptr;
    Quick::ActionEntry *deleteColumn = nullptr;
    Quick::ActionEntry *mergeCells = nullptr;
    Quick::ActionEntry *splitCell = nullptr;
    [[nodiscard]] QList<Quick::ActionEntry *> all() const;
};

class TableController final : public QObject {
    Q_OBJECT
public:
    TableController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                    QObject *parent);

    [[nodiscard]] const TableActions &actions() const { return m_actions; }
    [[nodiscard]] QList<Quick::MenuNode> tableMenuItems() const;
    [[nodiscard]] QStringList stripIds() const;

private:
    void insertRows(int count, bool above);
    void insertColumns(int count, bool left);
    void deleteRows();
    void deleteColumns();
    void mergeCells();
    void splitCell();
    void syncFromCursor();
    void keepCursorInTable(QTextTable *table, const QTextTableCell &was);

    PageTextControl *m_control;
    WriteDocument *m_document;
    TableActions m_actions;
};

} // namespace QindaOffice::Apps::Write
