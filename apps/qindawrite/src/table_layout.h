// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's Table Layout tab beyond rows, columns and merges (M70, D-079):
// Select Cell / Row / Column / Table, Delete Table, the nine cell
// alignments (vertical alignment with paragraph alignment), AutoFit
// (contents, window, fixed), Distribute Rows / Columns, Repeat Header Rows,
// Sort, Convert to Text, Insert ▸ Table ▸ Convert Text to Table, and the
// Table Properties dialog (table, row, column, cell). Commands enable only
// inside a table (Convert Text to Table only outside one); each is one
// undo step through office_docx's Word model (docx_table_format.h).

#include "action_model.h"

#include <QObject>
#include <QVariantMap>

class QTextTable;

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class TableLayoutController final : public QObject {
    Q_OBJECT
public:
    TableLayoutController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                          QObject *parent);

    // The Table menu's additions; Insert ▸ Convert Text to Table.
    [[nodiscard]] QList<Quick::MenuNode> tableMenuItems() const;
    [[nodiscard]] QString textToTableId() const;
    // Ribbon groups (ids in Word's order).
    [[nodiscard]] QStringList selectIds() const;
    [[nodiscard]] QStringList alignIds() const;
    [[nodiscard]] QStringList autoFitIds() const;

    // Table Properties. Question and answer: {table: {widthType ("auto",
    // "dxa", "pct"), width (twips or percent), alignment ("left",
    // "center", "right"), indent (twips), layout ("autofit", "fixed"),
    // margins {top, left, bottom, right} (twips)}, row: {first, count,
    // height (twips, 0 = automatic), exact, cantSplit, header},
    // column: {first, count, width (twips)}, cell: {valign ("top",
    // "center", "bottom"), margins {…} or null for the table's}}. The
    // answer's `changed` lists the sections the user touched.
    Q_INVOKABLE QVariantMap propertiesInfo() const;
    Q_INVOKABLE bool applyProperties(const QVariantMap &answer);

    // Sort: {columns: [labels], column, header (a guess)}; answer
    // {column, type ("text", "number", "date"), descending, header}.
    // An empty string, or why the rows cannot be sorted.
    Q_INVOKABLE QVariantMap sortInfo() const;
    Q_INVOKABLE QString applySort(const QVariantMap &answer);

    // Convert Text to Table: {columns (a guess for the separator),
    // separator ("tab", "comma", "paragraph", or one character)}.
    Q_INVOKABLE QVariantMap textToTableInfo(const QString &separator) const;
    Q_INVOKABLE bool applyTextToTable(const QVariantMap &answer);

signals:
    void propertiesDialogRequested();
    void sortDialogRequested();
    void textToTableDialogRequested();
    void message(const QString &text);

private:
    void createActions(Quick::ActionModel *model);
    void select(int what);
    void deleteTable();
    void align(int index);
    void autoFit(int mode);
    void distribute(bool rows);
    void toggleHeader(bool on);
    void convertToText();
    void syncFromCursor();
    [[nodiscard]] QTextTable *table() const;

    PageTextControl *m_control;
    WriteDocument *m_document;
    QList<Quick::ActionEntry *> m_select;
    QList<Quick::ActionEntry *> m_align;
    QList<Quick::ActionEntry *> m_autoFit;
    Quick::ActionEntry *m_deleteTable = nullptr;
    Quick::ActionEntry *m_distributeRows = nullptr;
    Quick::ActionEntry *m_distributeColumns = nullptr;
    Quick::ActionEntry *m_header = nullptr;
    Quick::ActionEntry *m_sort = nullptr;
    Quick::ActionEntry *m_toText = nullptr;
    Quick::ActionEntry *m_properties = nullptr;
    Quick::ActionEntry *m_textToTable = nullptr;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Write
