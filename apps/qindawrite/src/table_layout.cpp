// SPDX-License-Identifier: GPL-3.0-or-later
#include "table_layout.h"

#include "docx_table_format.h"
#include "menu_tree.h"
#include "page_text_control.h"
#include "table_data.h"
#include "write_document.h"

#include <QTextBlock>
#include <QTextTable>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

const struct {
    const char *id;
    const char *text;
    const char *icon;
} kSelect[] = {{"writeTableSelectCellAction", QT_TR_NOOP("Select &Cell"), "text-select"},
               {"writeTableSelectRowAction", QT_TR_NOOP("Select &Row"), "rows-2"},
               {"writeTableSelectColumnAction", QT_TR_NOOP("Select Co&lumn"), "columns-2"},
               {"writeTableSelectTableAction", QT_TR_NOOP("Select &Table"), "table"}};

// Word's alignment grid: vertical (top, middle, bottom) × horizontal.
const struct {
    const char *id;
    const char *text;
    const char *icon;
} kAlign[] = {{"writeTableAlignTopLeftAction", QT_TR_NOOP("Align Top &Left"), "align-start-vertical"},
              {"writeTableAlignTopCenterAction", QT_TR_NOOP("Align Top &Center"), "align-center-vertical"},
              {"writeTableAlignTopRightAction", QT_TR_NOOP("Align Top &Right"), "align-end-vertical"},
              {"writeTableAlignMiddleLeftAction", QT_TR_NOOP("Align Center L&eft"), "align-start-horizontal"},
              {"writeTableAlignMiddleCenterAction", QT_TR_NOOP("Align &Center"), "align-center-horizontal"},
              {"writeTableAlignMiddleRightAction", QT_TR_NOOP("Align Center Ri&ght"), "align-end-horizontal"},
              {"writeTableAlignBottomLeftAction", QT_TR_NOOP("Align &Bottom Left"), "text-align-start"},
              {"writeTableAlignBottomCenterAction", QT_TR_NOOP("Align Bottom Ce&nter"), "text-align-center"},
              {"writeTableAlignBottomRightAction", QT_TR_NOOP("Align Bottom Righ&t"), "text-align-end"}};

const struct {
    const char *id;
    const char *text;
    const char *icon;
} kAutoFit[] = {{"writeTableAutoFitContentsAction", QT_TR_NOOP("AutoFit &Contents"), "move-horizontal"},
                {"writeTableAutoFitWindowAction", QT_TR_NOOP("AutoFit &Window"), "maximize-2"},
                {"writeTableFixedWidthAction", QT_TR_NOOP("&Fixed Column Width"), "columns-3"}};

QString valignName(QTextCharFormat::VerticalAlignment v)
{
    return v == QTextCharFormat::AlignMiddle ? QStringLiteral("center")
        : v == QTextCharFormat::AlignBottom  ? QStringLiteral("bottom")
                                             : QStringLiteral("top");
}

QVariantMap marginsMap(const Docx::CellMargins &m)
{
    return {{QStringLiteral("top"), m.top}, {QStringLiteral("left"), m.left},
            {QStringLiteral("bottom"), m.bottom}, {QStringLiteral("right"), m.right}};
}

Docx::CellMargins marginsOf(const QVariantMap &m)
{
    return {m.value(QStringLiteral("top"), -1).toInt(), m.value(QStringLiteral("left"), -1).toInt(),
            m.value(QStringLiteral("bottom"), -1).toInt(), m.value(QStringLiteral("right"), -1).toInt()};
}

} // namespace

TableLayoutController::TableLayoutController(PageTextControl *control, WriteDocument *document,
                                             Quick::ActionModel *model, QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    createActions(model);
    connect(m_control, &PageTextControl::cursorPositionChanged, this, [this] { syncFromCursor(); });
    connect(m_control, &PageTextControl::documentChanged, this, [this] { syncFromCursor(); });
    syncFromCursor();
}

void TableLayoutController::createActions(Quick::ActionModel *model)
{
    const auto make = [model](const char *id, const QString &text, const char *icon) {
        return model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
    };
    for (int i = 0; i < int(std::size(kSelect)); ++i) {
        m_select.append(make(kSelect[i].id, tr(kSelect[i].text), kSelect[i].icon));
        connect(m_select.last(), &ActionEntry::triggered, this, [this, i] { select(i); });
    }
    for (int i = 0; i < int(std::size(kAlign)); ++i) {
        m_align.append(make(kAlign[i].id, tr(kAlign[i].text), kAlign[i].icon));
        connect(m_align.last(), &ActionEntry::triggered, this, [this, i] { align(i); });
    }
    for (int i = 0; i < int(std::size(kAutoFit)); ++i) {
        m_autoFit.append(make(kAutoFit[i].id, tr(kAutoFit[i].text), kAutoFit[i].icon));
        connect(m_autoFit.last(), &ActionEntry::triggered, this, [this, i] { autoFit(i); });
    }
    m_deleteTable = make("writeTableDeleteTableAction", tr("&Delete Table"), "trash-2");
    m_distributeRows = make("writeTableDistributeRowsAction", tr("Distribute &Rows"), "align-vertical-distribute-center");
    m_distributeColumns = make("writeTableDistributeColumnsAction", tr("Distribute Colu&mns"),
                               "align-horizontal-distribute-center");
    m_header = make("writeTableRepeatHeaderAction", tr("Repeat &Header Rows"), "repeat");
    m_header->setCheckable(true);
    m_sort = make("writeTableSortAction", tr("&Sort…"), "arrow-down-z-a");
    m_toText = make("writeTableToTextAction", tr("Convert to Te&xt"), "text");
    m_properties = make("writeTablePropertiesAction", tr("Table &Properties…"), "settings-2");
    m_textToTable = make("writeTextToTableAction", tr("Convert Text to &Table…"), "table-2");
    connect(m_deleteTable, &ActionEntry::triggered, this, [this] { deleteTable(); });
    connect(m_distributeRows, &ActionEntry::triggered, this, [this] { distribute(true); });
    connect(m_distributeColumns, &ActionEntry::triggered, this, [this] { distribute(false); });
    connect(m_header, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            toggleHeader(on);
        }
    });
    connect(m_sort, &ActionEntry::triggered, this, &TableLayoutController::sortDialogRequested);
    connect(m_toText, &ActionEntry::triggered, this, [this] { convertToText(); });
    connect(m_properties, &ActionEntry::triggered, this, &TableLayoutController::propertiesDialogRequested);
    connect(m_textToTable, &ActionEntry::triggered, this, &TableLayoutController::textToTableDialogRequested);
}

QList<MenuNode> TableLayoutController::tableMenuItems() const
{
    const auto a = [](const ActionEntry *e) { return MenuNode::action(e->id()); };
    QList<MenuNode> select;
    for (const ActionEntry *e : m_select) {
        select.append(a(e));
    }
    QList<MenuNode> align;
    for (const ActionEntry *e : m_align) {
        align.append(a(e));
    }
    QList<MenuNode> fit;
    for (const ActionEntry *e : m_autoFit) {
        fit.append(a(e));
    }
    return {MenuNode::submenu(QStringLiteral("writeTableSelectMenu"), tr("Se&lect"), select),
            a(m_deleteTable),
            MenuNode::separator(),
            MenuNode::submenu(QStringLiteral("writeTableAutoFitMenu"), tr("&AutoFit"), fit),
            a(m_distributeRows),
            a(m_distributeColumns),
            MenuNode::submenu(QStringLiteral("writeTableAlignMenu"), tr("Cell A&lignment"), align),
            MenuNode::separator(),
            a(m_header),
            a(m_sort),
            a(m_toText),
            MenuNode::separator(),
            a(m_properties)};
}

QString TableLayoutController::textToTableId() const
{
    return m_textToTable->id();
}

QStringList TableLayoutController::selectIds() const
{
    QStringList ids;
    for (const ActionEntry *e : m_select) {
        ids.append(e->id());
    }
    return ids;
}

QStringList TableLayoutController::alignIds() const
{
    QStringList ids;
    for (const ActionEntry *e : m_align) {
        ids.append(e->id());
    }
    return ids;
}

QStringList TableLayoutController::autoFitIds() const
{
    QStringList ids;
    for (const ActionEntry *e : m_autoFit) {
        ids.append(e->id());
    }
    return ids;
}

QTextTable *TableLayoutController::table() const
{
    return m_control->textCursor().currentTable();
}

void TableLayoutController::select(int what)
{
    QTextTable *t = table();
    if (t == nullptr) {
        return;
    }
    const Docx::CellRange range = Docx::selectedCells(m_control->textCursor());
    if (!range.isValid()) {
        return; // the caret on the table's own boundary
    }
    int top = range.row;
    int left = range.column;
    int bottom = range.row + range.rows - 1;
    int right = range.column + range.columns - 1;
    if (what == 1 || what == 3) {
        left = 0;
        right = t->columns() - 1;
    }
    if (what == 2 || what == 3) {
        top = 0;
        bottom = t->rows() - 1;
    }
    QTextCursor cursor = t->cellAt(top, left).firstCursorPosition();
    cursor.setPosition(t->cellAt(bottom, right).lastCursorPosition().position(), QTextCursor::KeepAnchor);
    m_control->setTextCursor(cursor);
}

void TableLayoutController::deleteTable()
{
    QTextTable *t = table();
    if (t == nullptr) {
        return;
    }
    QTextCursor cursor(m_control->document());
    cursor.beginEditBlock();
    cursor.setPosition(t->firstPosition() - 1);
    cursor.setPosition(t->lastPosition() + 1, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    m_control->setTextCursor(cursor);
    m_document->setDirty(true);
}

void TableLayoutController::align(int index)
{
    QTextTable *t = table();
    if (t == nullptr) {
        return;
    }
    static constexpr QTextCharFormat::VerticalAlignment kVertical[] = {
        QTextCharFormat::AlignTop, QTextCharFormat::AlignMiddle, QTextCharFormat::AlignBottom};
    static constexpr Qt::Alignment kHorizontal[] = {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight};
    const Docx::CellRange range = Docx::selectedCells(m_control->textCursor());
    QTextCursor edit = m_control->textCursor();
    edit.beginEditBlock();
    Docx::applyCellVerticalAlignment(t, range, kVertical[index / 3]);
    for (const QTextTableCell &cell : Docx::cellsIn(t, range)) {
        for (auto it = cell.begin(); !it.atEnd(); ++it) {
            if (it.currentFrame() == nullptr) {
                QTextBlockFormat f = it.currentBlock().blockFormat();
                f.setAlignment(kHorizontal[index % 3]);
                QTextCursor(it.currentBlock()).setBlockFormat(f);
            }
        }
    }
    edit.endEditBlock();
    m_document->setDirty(true);
}

void TableLayoutController::autoFit(int mode)
{
    if (QTextTable *t = table()) {
        QTextCursor edit = m_control->textCursor();
        edit.beginEditBlock();
        Docx::autoFit(t, Docx::AutoFit(mode), m_document->contentWidth());
        edit.endEditBlock();
        m_document->setDirty(true);
    }
}

void TableLayoutController::distribute(bool rows)
{
    QTextTable *t = table();
    if (t == nullptr) {
        return;
    }
    Docx::CellRange range = Docx::selectedCells(m_control->textCursor());
    if ((rows && range.rows < 2) || (!rows && range.columns < 2)) {
        range = Docx::wholeTable(t); // one cell: the whole table, as Word does
    }
    QTextCursor edit = m_control->textCursor();
    edit.beginEditBlock();
    if (rows) {
        Docx::distributeRows(t, range);
    } else {
        Docx::distributeColumns(t, range, m_document->contentWidth());
    }
    edit.endEditBlock();
    m_document->setDirty(true);
}

void TableLayoutController::toggleHeader(bool on)
{
    QTextTable *t = table();
    if (t == nullptr) {
        return;
    }
    // Word repeats leading rows only: on marks every row down to the
    // selection's last, off unmarks from the selection's first.
    const Docx::CellRange range = Docx::selectedCells(m_control->textCursor());
    QTextTableFormat f = t->format();
    f.setHeaderRowCount(on ? qMax(f.headerRowCount(), range.row + range.rows) : qMin(f.headerRowCount(), range.row));
    QTextCursor edit = m_control->textCursor();
    edit.beginEditBlock();
    t->setFormat(f);
    edit.endEditBlock();
    m_document->setDirty(true);
}

void TableLayoutController::convertToText()
{
    if (QTextTable *t = table()) {
        QTextCursor edit(m_control->document());
        edit.beginEditBlock();
        const QTextCursor at = tableToText(t);
        edit.endEditBlock();
        m_control->setTextCursor(at);
        m_document->setDirty(true);
    }
}

void TableLayoutController::syncFromCursor()
{
    QTextTable *t = table();
    const bool in = t != nullptr;
    for (ActionEntry *e : m_select + m_align + m_autoFit) {
        e->setEnabled(in);
    }
    for (ActionEntry *e : {m_deleteTable, m_header, m_sort, m_toText, m_properties}) {
        e->setEnabled(in);
    }
    m_distributeRows->setEnabled(in && t->rows() > 1);
    m_distributeColumns->setEnabled(in && t->columns() > 1);
    m_textToTable->setEnabled(!in);
    m_syncing = true;
    m_header->setChecked(in && Docx::selectedCells(m_control->textCursor()).row < t->format().headerRowCount());
    m_syncing = false;
}

QVariantMap TableLayoutController::propertiesInfo() const
{
    const QTextTable *t = table();
    if (t == nullptr) {
        return {};
    }
    const Docx::CellRange range = Docx::selectedCells(m_control->textCursor());
    const Docx::TableProperties p = Docx::tableProperties(t);
    const Docx::RowProperties row = Docx::rowProperties(t, range.row);
    const QList<int> widths = Docx::columnWidths(t, m_document->contentWidth());
    const QTextTableCellFormat cell = t->cellAt(range.row, range.column).format().toTableCellFormat();
    const Docx::CellMargins own = Docx::cellMargins(t, range.row, range.column);
    const Qt::Alignment a = p.alignment;
    return {
        {QStringLiteral("table"),
         QVariantMap{{QStringLiteral("widthType"), p.width.type},
                     {QStringLiteral("width"), p.width.type == u"pct" ? p.width.value / 50.0 : qreal(p.width.value)},
                     {QStringLiteral("alignment"), a == Qt::AlignHCenter ? QStringLiteral("center")
                                                       : a == Qt::AlignRight ? QStringLiteral("right")
                                                                             : QStringLiteral("left")},
                     {QStringLiteral("indent"), p.indent},
                     {QStringLiteral("layout"), p.layout},
                     {QStringLiteral("margins"), marginsMap(p.margins)}}},
        {QStringLiteral("row"),
         QVariantMap{{QStringLiteral("first"), range.row},
                     {QStringLiteral("count"), range.rows},
                     {QStringLiteral("height"), row.height},
                     {QStringLiteral("exact"), row.exact},
                     {QStringLiteral("cantSplit"), row.cantSplit},
                     {QStringLiteral("header"), range.row < p.headerRows}}},
        {QStringLiteral("column"),
         QVariantMap{{QStringLiteral("first"), range.column},
                     {QStringLiteral("count"), range.columns},
                     {QStringLiteral("width"), widths.value(range.column)}}},
        {QStringLiteral("cell"),
         QVariantMap{{QStringLiteral("valign"), valignName(cell.verticalAlignment())},
                     {QStringLiteral("margins"), own.isEmpty() ? QVariant() : QVariant(marginsMap(own))}}},
    };
}

bool TableLayoutController::applyProperties(const QVariantMap &answer)
{
    QTextTable *t = table();
    const QStringList changed = answer.value(QStringLiteral("changed")).toStringList();
    if (t == nullptr || changed.isEmpty()) {
        return false;
    }
    const Docx::CellRange range = Docx::selectedCells(m_control->textCursor());
    QTextCursor edit = m_control->textCursor();
    edit.beginEditBlock();
    if (changed.contains(u"table")) {
        const QVariantMap m = answer.value(QStringLiteral("table")).toMap();
        Docx::TableProperties p = Docx::tableProperties(t);
        const QString type = m.value(QStringLiteral("widthType")).toString();
        const qreal width = m.value(QStringLiteral("width")).toReal();
        p.width = type == u"pct" ? Docx::WordWidth{type, qRound(width * 50)}
            : type == u"dxa"     ? Docx::WordWidth{type, qRound(width)}
                                 : Docx::WordWidth{QStringLiteral("auto"), 0};
        const QString align = m.value(QStringLiteral("alignment")).toString();
        p.alignment = align == u"center" ? Qt::AlignHCenter : align == u"right" ? Qt::AlignRight : Qt::AlignLeft;
        p.indent = m.value(QStringLiteral("indent")).toInt();
        p.layout = m.value(QStringLiteral("layout")).toString() == u"fixed" ? QStringLiteral("fixed") : QStringLiteral("autofit");
        p.margins = marginsOf(m.value(QStringLiteral("margins")).toMap());
        Docx::setTableProperties(t, p, m_document->contentWidth());
    }
    if (changed.contains(u"row")) {
        const QVariantMap m = answer.value(QStringLiteral("row")).toMap();
        for (int r = range.row; r < range.row + range.rows; ++r) {
            Docx::setRowProperties(t, r, {m.value(QStringLiteral("height")).toInt(), m.value(QStringLiteral("exact")).toBool(),
                                          m.value(QStringLiteral("cantSplit")).toBool()});
        }
        QTextTableFormat f = t->format();
        const int header = f.headerRowCount();
        f.setHeaderRowCount(m.value(QStringLiteral("header")).toBool() ? qMax(header, range.row + range.rows)
                                                                       : qMin(header, range.row));
        t->setFormat(f);
    }
    if (changed.contains(u"column")) {
        const int width = answer.value(QStringLiteral("column")).toMap().value(QStringLiteral("width")).toInt();
        for (int c = range.column; c < range.column + range.columns && width > 0; ++c) {
            Docx::setColumnWidth(t, c, width, m_document->contentWidth());
        }
    }
    if (changed.contains(u"cell")) {
        const QVariantMap m = answer.value(QStringLiteral("cell")).toMap();
        const QString v = m.value(QStringLiteral("valign")).toString();
        Docx::applyCellVerticalAlignment(t, range, v == u"center"   ? QTextCharFormat::AlignMiddle
                                                     : v == u"bottom" ? QTextCharFormat::AlignBottom
                                                                      : QTextCharFormat::AlignTop);
        Docx::setCellMargins(t, range, marginsOf(m.value(QStringLiteral("margins")).toMap()));
    }
    edit.endEditBlock();
    m_document->setDirty(true);
    syncFromCursor();
    return true;
}

QVariantMap TableLayoutController::sortInfo() const
{
    const QTextTable *t = table();
    if (t == nullptr) {
        return {};
    }
    // A header row Word would guess: repeated, or set in bold.
    const QTextCharFormat first = t->cellAt(0, 0).firstCursorPosition().charFormat();
    QTextCursor inFirst = t->cellAt(0, 0).firstCursorPosition();
    inFirst.movePosition(QTextCursor::NextCharacter);
    const bool header = t->format().headerRowCount() > 0 || inFirst.charFormat().fontWeight() >= QFont::DemiBold
        || first.fontWeight() >= QFont::DemiBold;
    QStringList columns;
    QStringList headings;
    for (int c = 0; c < t->columns(); ++c) {
        columns.append(tr("Column %1").arg(c + 1));
        QTextCursor cell = t->cellAt(0, c).firstCursorPosition();
        cell.setPosition(t->cellAt(0, c).lastCursorPosition().position(), QTextCursor::KeepAnchor);
        const QString text = cell.selectedText().simplified();
        headings.append(text.isEmpty() ? columns.last() : text);
    }
    return {{QStringLiteral("columns"), columns},
            {QStringLiteral("headings"), headings},
            {QStringLiteral("column"), Docx::selectedCells(m_control->textCursor()).column},
            {QStringLiteral("header"), header}};
}

QString TableLayoutController::applySort(const QVariantMap &answer)
{
    QTextTable *t = table();
    if (t == nullptr) {
        return tr("The cursor is not in a table.");
    }
    Docx::CellRange range = Docx::selectedCells(m_control->textCursor());
    if (range.rows < 2) {
        range = Docx::wholeTable(t); // Word sorts the whole table from one cell
    }
    SortOptions options;
    options.column = qBound(0, answer.value(QStringLiteral("column")).toInt(), t->columns() - 1);
    const QString type = answer.value(QStringLiteral("type")).toString();
    options.type = type == u"number" ? SortOptions::Number : type == u"date" ? SortOptions::Date : SortOptions::Text;
    options.descending = answer.value(QStringLiteral("descending")).toBool();
    options.header = answer.value(QStringLiteral("header")).toBool() && range.row == 0;
    QTextCursor edit = m_control->textCursor();
    edit.beginEditBlock();
    const QString problem = sortRows(t, range.row, range.rows, options);
    edit.endEditBlock();
    if (!problem.isEmpty()) {
        emit message(problem);
    } else {
        m_document->setDirty(true);
    }
    return problem;
}

QVariantMap TableLayoutController::textToTableInfo(const QString &separator) const
{
    const QChar s = separator == u"comma" ? QChar(u',')
        : separator == u"paragraph"       ? QChar::ParagraphSeparator
        : separator == u"tab" || separator.isEmpty() ? QChar(u'\t')
                                                     : separator.at(0);
    return {{QStringLiteral("columns"), s == QChar::ParagraphSeparator ? 1 : fieldCount(m_control->textCursor(), s)},
            {QStringLiteral("separator"), separator.isEmpty() ? QStringLiteral("tab") : separator}};
}

bool TableLayoutController::applyTextToTable(const QVariantMap &answer)
{
    const QString separator = answer.value(QStringLiteral("separator")).toString();
    const QChar s = separator == u"comma" ? QChar(u',')
        : separator == u"paragraph"       ? QChar::ParagraphSeparator
        : separator == u"tab" || separator.isEmpty() ? QChar(u'\t')
                                                     : separator.at(0);
    QTextCursor cursor = m_control->textCursor();
    QTextCursor edit(m_control->document());
    edit.beginEditBlock();
    QTextTable *made = textToTable(cursor, answer.value(QStringLiteral("columns"), 1).toInt(), s, m_document->contentWidth());
    edit.endEditBlock();
    if (made == nullptr) {
        return false;
    }
    m_control->setTextCursor(cursor);
    m_document->setDirty(true);
    return true;
}

} // namespace QindaOffice::Apps::Write
