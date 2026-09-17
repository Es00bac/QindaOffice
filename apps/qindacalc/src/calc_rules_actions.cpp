// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_rules_actions.h"

#include "calc_rule_dialogs.h"
#include "spreadsheet_document.h"
#include "spreadsheet_view.h"

#include "document_window.h"
#include "standard_actions.h"

#include "sheet.h"
#include "workbook.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QIcon>
#include <QInputDialog>
#include <QListWidget>
#include <QMenu>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace QindaOffice::Apps::QindaCalc {

using CalcEng::CellAddr;
using CalcEng::CellRange;

namespace {

QAction *make(QObject *parent, const char *id, const QString &text,
              const QKeySequence &shortcut = {}, const char *iconName = nullptr)
{
    auto *action = new QAction(text, parent);
    action->setObjectName(QString::fromLatin1(id));
    if (iconName != nullptr) {
        action->setIcon(QIcon::fromTheme(QString::fromLatin1(iconName)));
    }
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    return action;
}

} // namespace

CalcRuleActions::CalcRuleActions(Shell::DocumentWindow *window, SpreadsheetDocument *document,
                                 SpreadsheetView *view)
    : QObject(window)
    , m_window(window)
    , m_document(document)
    , m_view(view)
{
    createActions();
    connect(m_view, &SpreadsheetView::selectionChanged, this,
            [this](const CellRange &) { syncFromCursor(); });
    connect(m_view, &SpreadsheetView::filterButtonClicked, this,
            &CalcRuleActions::showFilterMenu);
    syncFromCursor();
}

void CalcRuleActions::createActions()
{
    using namespace Shell::ActionId;
    m_condFormat = make(this, kCalcCondFormat, tr("Con&ditional Formatting…"), {},
                        "format-fill-color");
    m_validation = make(this, kCalcValidation, tr("&Validity…"));
    m_filter = make(this, kCalcAutoFilter, tr("&AutoFilter"),
                    QKeySequence(QStringLiteral("Ctrl+Shift+L")), "view-filter");
    m_filter->setCheckable(true);
    m_comment = make(this, kCalcComment, tr("Co&mment…"),
                     QKeySequence(QStringLiteral("Shift+F2")), "view-conversation-balloon");
    m_deleteComment = make(this, kCalcDeleteComment, tr("Delete Co&mment"));

    connect(m_condFormat, &QAction::triggered, this, &CalcRuleActions::editCondFormats);
    connect(m_validation, &QAction::triggered, this, &CalcRuleActions::editValidation);
    connect(m_filter, &QAction::triggered, this, [this](bool on) {
        if (!m_syncing) {
            toggleFilter(on);
        }
    });
    connect(m_comment, &QAction::triggered, this, &CalcRuleActions::editComment);
    connect(m_deleteComment, &QAction::triggered, this, &CalcRuleActions::deleteComment);
}

QString CalcRuleActions::displayTextAt(const CellAddr &addr) const
{
    return m_document->displayTextAt(addr);
}

CellRange CalcRuleActions::blockAroundSelection() const
{
    const CellRange selection = m_view->selectionRange();
    if (selection.cellCount() > 1) {
        return selection;
    }
    const CalcEng::Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    const auto filled = [&sheet](qint32 row, qint32 col) {
        const CalcEng::Cell *cell = sheet.cellAt({row, col});
        return cell != nullptr && !isBlank(cell->value);
    };
    const CellAddr cursor = m_view->cursorCell();
    if (!filled(cursor.row, cursor.col)) {
        return selection;
    }
    CellRange block{cursor, cursor};
    while (block.topLeft.row > 0 && filled(block.topLeft.row - 1, cursor.col)) {
        --block.topLeft.row;
    }
    while (filled(block.bottomRight.row + 1, cursor.col)) {
        ++block.bottomRight.row;
    }
    while (block.topLeft.col > 0 && filled(cursor.row, block.topLeft.col - 1)) {
        --block.topLeft.col;
    }
    while (filled(cursor.row, block.bottomRight.col + 1)) {
        ++block.bottomRight.col;
    }
    return block;
}

void CalcRuleActions::editCondFormats()
{
    const CalcEng::Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    CondFormatDialog dialog(sheet.condFormats(), m_view->selectionRange(), m_window);
    if (dialog.exec() == QDialog::Accepted) {
        // The whole list comes back, so adding three rules and removing one
        // is ONE undo step — which is what the user did.
        m_document->setCondFormats(dialog.rules());
        m_view->refreshAll();
    }
}

void CalcRuleActions::editValidation()
{
    const CalcEng::Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    const CellRange selection = m_view->selectionRange();
    const CalcEng::ValidationRule *existing = sheet.validationAt(m_view->cursorCell());
    ValidationDialog dialog(existing, selection, m_window);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QList<CalcEng::ValidationRule> rules;
    for (const CalcEng::ValidationRule &rule : sheet.validations()) {
        // A new rule replaces any rule whose range is exactly the one being
        // edited; overlapping ranges elsewhere are left alone, because
        // validationAt() takes the first match and silently reshuffling
        // other people's rules would be worse than leaving them.
        if (!(rule.range == selection)) {
            rules.append(rule);
        }
    }
    if (!dialog.clearsRule()) {
        rules.append(dialog.rule());
    }
    m_document->setValidations(rules);
    syncFromCursor();
}

void CalcRuleActions::toggleFilter(bool on)
{
    CalcEng::AutoFilter filter;
    if (on) {
        filter.range = blockAroundSelection();
        filter.active = filter.range.isValid()
            && filter.range.bottomRight.row > filter.range.topLeft.row;
        if (!filter.active) {
            // Nothing to filter: refuse rather than leave a filter whose
            // header row is also its only row.
            m_syncing = true;
            m_filter->setChecked(false);
            m_syncing = false;
            return;
        }
    }
    m_document->setAutoFilter(filter);
    m_view->refreshAll();
}

void CalcRuleActions::showFilterMenu(qint32 column, const QPoint &globalPos)
{
    const CalcEng::Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    CalcEng::AutoFilter filter = sheet.autoFilter();
    if (!filter.active || !filter.range.isValid()) {
        return;
    }
    const QStringList values = sheet.filterValuesFor(
        column, [this](const CellAddr &addr) { return displayTextAt(addr); });
    const QStringList allowed =
        filter.allowed.contains(column) ? filter.allowed.value(column) : values;

    // AGENT-NOTE: a plain QMenu of checkable actions closes on the first
    // click, so picking three values would take three trips. The dropdown
    // is therefore one QWidgetAction holding a checkable list plus its own
    // OK button — the shape every spreadsheet's filter uses.
    QMenu menu(m_window);
    menu.setObjectName(QStringLiteral("calcFilterMenu"));
    auto *panel = new QWidget(&menu);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);
    auto *selectAll = new QCheckBox(tr("Select all"), panel);
    selectAll->setObjectName(QStringLiteral("calcFilterSelectAll"));
    selectAll->setTristate(true);
    layout->addWidget(selectAll);
    auto *list = new QListWidget(panel);
    list->setObjectName(QStringLiteral("calcFilterList"));
    list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const QString &value : values) {
        auto *item = new QListWidgetItem(value.isEmpty() ? tr("(empty)") : value, list);
        item->setData(Qt::UserRole, value);
        item->setCheckState(allowed.contains(value) ? Qt::Checked : Qt::Unchecked);
    }
    list->setMinimumSize(180, 200);
    layout->addWidget(list, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, panel);
    buttons->setObjectName(QStringLiteral("calcFilterButtons"));
    layout->addWidget(buttons);

    const auto syncSelectAll = [list, selectAll] {
        int checked = 0;
        for (int i = 0; i < list->count(); ++i) {
            checked += list->item(i)->checkState() == Qt::Checked ? 1 : 0;
        }
        const QSignalBlocker blocker(selectAll);
        selectAll->setCheckState(checked == 0 ? Qt::Unchecked
                                              : (checked == list->count() ? Qt::Checked
                                                                          : Qt::PartiallyChecked));
    };
    syncSelectAll();
    connect(list, &QListWidget::itemChanged, panel, [syncSelectAll] { syncSelectAll(); });
    connect(selectAll, &QCheckBox::clicked, panel, [list](bool on) {
        for (int i = 0; i < list->count(); ++i) {
            list->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        }
    });

    auto *holder = new QWidgetAction(&menu);
    holder->setDefaultWidget(panel);
    menu.addAction(holder);
    bool accepted = false;
    connect(buttons, &QDialogButtonBox::accepted, &menu, [&accepted, &menu] {
        accepted = true;
        menu.close();
    });
    connect(buttons, &QDialogButtonBox::rejected, &menu, &QMenu::close);
    menu.exec(globalPos);
    if (!accepted) {
        return;
    }
    QStringList kept;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->checkState() == Qt::Checked) {
            kept.append(list->item(i)->data(Qt::UserRole).toString());
        }
    }
    if (kept.size() == values.size()) {
        // Everything allowed is the same as no filter on this column; storing
        // it would leave the column's button looking filtered forever.
        filter.allowed.remove(column);
    } else {
        filter.allowed.insert(column, kept);
    }
    m_document->setAutoFilter(filter);
    m_view->refreshAll();
}

void CalcRuleActions::editComment()
{
    const CellAddr cursor = m_view->cursorCell();
    const CalcEng::Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    const CalcEng::CellComment *existing = sheet.commentAt(cursor);
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        m_window, tr("Cell Comment"),
        tr("Comment on %1:").arg(CalcEng::addrText(cursor)),
        existing != nullptr ? existing->text : QString(), &ok);
    if (!ok) {
        return;
    }
    CalcEng::CellComment comment;
    comment.author = existing != nullptr ? existing->author
                                        : QString::fromLocal8Bit(qgetenv("USER"));
    comment.text = text.trimmed();
    m_document->setComment(cursor, comment);
    m_view->refreshAll();
    syncFromCursor();
}

void CalcRuleActions::deleteComment()
{
    m_document->setComment(m_view->cursorCell(), CalcEng::CellComment{});
    m_view->refreshAll();
    syncFromCursor();
}

void CalcRuleActions::syncFromCursor()
{
    m_syncing = true;
    const CalcEng::Sheet &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
    m_filter->setChecked(sheet.autoFilter().active);
    m_deleteComment->setEnabled(sheet.commentAt(m_view->cursorCell()) != nullptr);
    m_syncing = false;
}

void CalcRuleActions::populateFormatMenu(QMenu *menu)
{
    menu->addAction(m_condFormat);
}

void CalcRuleActions::populateDataMenu(QMenu *menu)
{
    menu->addAction(m_filter);
    menu->addAction(m_validation);
}

void CalcRuleActions::populateContextMenu(QMenu *menu)
{
    menu->addAction(m_comment);
    menu->addAction(m_deleteComment);
    menu->addSeparator();
    menu->addAction(m_condFormat);
    menu->addAction(m_validation);
}

QList<QAction *> CalcRuleActions::stripActions() const
{
    return {m_filter, m_condFormat, m_comment};
}

} // namespace QindaOffice::Apps::QindaCalc
