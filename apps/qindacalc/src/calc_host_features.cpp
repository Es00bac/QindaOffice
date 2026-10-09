// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's M66 commands (D-075): Insert ▸ Link / Edit Link / Open Link /
// Remove Link (Ctrl+click on the grid follows a link too), Data ▸ Outline
// (Group, Ungroup, Show Detail, Hide Detail — the grid's outline gutter
// drives the same document operations), Format ▸ Hide and Unhide, Lock
// Cell and Hide Formula, and the Review menu's Protect / Unprotect Sheet
// and Workbook. One ActionEntry per command, shared by menus and ribbon.

#include "calc_host.h"

#include "spreadsheet_item.h"
#include "standard_actions.h"

#include "hyperlink.h"
#include "sheet.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QKeySequence>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

// The schemes a link may hand to the desktop. A file opens only when it is
// a document, never a program (Excel asks first; QindaCalc refuses).
bool safeToOpen(const QUrl &url)
{
    static const QStringList schemes{QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("mailto"),
                                     QStringLiteral("ftp"), QStringLiteral("ftps")};
    if (schemes.contains(url.scheme().toLower())) {
        return true;
    }
    if (url.isLocalFile()) {
        const QFileInfo file(url.toLocalFile());
        return file.exists() && !(file.isFile() && file.isExecutable());
    }
    return false;
}

} // namespace

OutlineAxis CalcHost::groupAxis() const
{
    return m_grid != nullptr && m_grid->selectionKind() == SpreadsheetItem::ColumnSelection ? OutlineAxis::Columns
                                                                                           : OutlineAxis::Rows;
}

void CalcHost::createFeatureActions()
{
    Quick::ActionModel *model = actions();
    const auto make = [model](const char *id, const QString &text, const QKeySequence &shortcut = {},
                              const char *icon = nullptr) {
        return model->add(QString::fromLatin1(id), text, shortcut,
                          icon != nullptr ? QString::fromLatin1(icon) : QString());
    };
    const auto refresh = [this] {
        refreshGrid();
        syncFromCursor();
    };
    // ---- links ----
    connect(make(ActionId::kInsertHyperlink, tr("&Link…"), QKeySequence(QStringLiteral("Ctrl+K")), "link"),
            &ActionEntry::triggered, this, [this] { emit linkDialogRequested(); });
    connect(make(ActionId::kCalcEditHyperlink, tr("&Edit Link…"), {}, "pen-line"), &ActionEntry::triggered, this,
            [this] { emit linkDialogRequested(); });
    connect(make(ActionId::kCalcOpenHyperlink, tr("&Open Link"), {}, "external-link"), &ActionEntry::triggered, this,
            [this] { openLinkAt(cursor().row, cursor().col); });
    connect(make(ActionId::kCalcRemoveHyperlink, tr("&Remove Link"), {}, "unlink"), &ActionEntry::triggered, this,
            [this, refresh] {
                m_document->removeHyperlinks(selection());
                refresh();
            });
    // ---- outline ----
    const auto span = [this](OutlineAxis axis) {
        const CellRange range = selection();
        return axis == OutlineAxis::Rows ? QPair<qint32, qint32>{range.topLeft.row, range.bottomRight.row}
                                         : QPair<qint32, qint32>{range.topLeft.col, range.bottomRight.col};
    };
    connect(make(ActionId::kCalcGroup, tr("&Group"), QKeySequence(QStringLiteral("Alt+Shift+Right")), "group"),
            &ActionEntry::triggered, this, [this, span, refresh] {
                const auto [first, last] = span(groupAxis());
                m_document->groupLines(groupAxis(), first, last, true);
                refresh();
            });
    connect(make(ActionId::kCalcUngroup, tr("&Ungroup"), QKeySequence(QStringLiteral("Alt+Shift+Left")), "ungroup"),
            &ActionEntry::triggered, this, [this, span, refresh] {
                const auto [first, last] = span(groupAxis());
                m_document->groupLines(groupAxis(), first, last, false);
                refresh();
            });
    const auto detail = [this, refresh](bool shown) {
        // The cursor's row group first, else its column group.
        if (!m_document->setDetailShown(OutlineAxis::Rows, cursor().row, shown)
            && !m_document->setDetailShown(OutlineAxis::Columns, cursor().col, shown)) {
            showStatusMessage(tr("There is no group at the cursor."), 6000);
        }
        refresh();
    };
    connect(make(ActionId::kCalcShowDetail, tr("&Show Detail"), {}, "list-plus"), &ActionEntry::triggered, this,
            [detail] { detail(true); });
    connect(make(ActionId::kCalcHideDetail, tr("&Hide Detail"), {}, "list-minus"), &ActionEntry::triggered, this,
            [detail] { detail(false); });
    const auto hide = [this, span, refresh](OutlineAxis axis, bool hidden) {
        const auto [first, last] = span(axis);
        m_document->setLinesHidden(axis, first, last, hidden);
        refresh();
    };
    connect(make(ActionId::kCalcHideRows, tr("Hide &Rows"), {}, "eye-off"), &ActionEntry::triggered, this,
            [hide] { hide(OutlineAxis::Rows, true); });
    connect(make(ActionId::kCalcUnhideRows, tr("&Unhide Rows"), {}, "eye"), &ActionEntry::triggered, this,
            [hide] { hide(OutlineAxis::Rows, false); });
    connect(make(ActionId::kCalcHideColumns, tr("Hide &Columns"), {}, "eye-off"), &ActionEntry::triggered, this,
            [hide] { hide(OutlineAxis::Columns, true); });
    connect(make(ActionId::kCalcUnhideColumns, tr("U&nhide Columns"), {}, "eye"), &ActionEntry::triggered, this,
            [hide] { hide(OutlineAxis::Columns, false); });
    // ---- protection ----
    connect(make(ActionId::kCalcProtectSheet, tr("&Protect Sheet…"), {}, "lock"), &ActionEntry::triggered, this,
            [this] { emit protectSheetRequested(); });
    connect(make(ActionId::kCalcUnprotectSheet, tr("&Unprotect Sheet…"), {}, "lock-open"), &ActionEntry::triggered,
            this, [this, refresh] {
                if (sheet().protection().hasPassword()) {
                    emit unprotectRequested(false);
                    return;
                }
                m_document->unprotectSheet(QString());
                refresh();
            });
    connect(make(ActionId::kCalcProtectWorkbook, tr("Protect &Workbook…"), {}, "key-round"), &ActionEntry::triggered,
            this, [this] { emit protectWorkbookRequested(); });
    connect(make(ActionId::kCalcUnprotectWorkbook, tr("Unprotect W&orkbook…"), {}, "lock-open"),
            &ActionEntry::triggered, this, [this, refresh] {
                if (m_document->workbook()->protection.hasPassword()) {
                    emit unprotectRequested(true);
                    return;
                }
                m_document->unprotectWorkbook(QString());
                emit sheetsChanged();
                refresh();
            });
    const auto toggleStyle = [this, refresh](bool hiddenFlag, bool on) {
        if (m_syncing) {
            return;
        }
        StylePatch patch;
        (hiddenFlag ? patch.hiddenFormula : patch.locked) = on;
        m_document->restyleRange(selection(), patch, hiddenFlag ? tr("Hide formula") : tr("Lock cells"));
        refresh();
    };
    ActionEntry *lock = make(ActionId::kCalcLockCell, tr("&Lock Cell"), {}, "lock");
    lock->setCheckable(true);
    connect(lock, &ActionEntry::toggled, this, [toggleStyle](bool on) { toggleStyle(false, on); });
    ActionEntry *hideFormula = make(ActionId::kCalcHideFormula, tr("Hide &Formula"), {}, "braces");
    hideFormula->setCheckable(true);
    connect(hideFormula, &ActionEntry::toggled, this, [toggleStyle](bool on) { toggleStyle(true, on); });
}

void CalcHost::insertFeatureMenus()
{
    const auto a = [](const char *id) { return MenuNode::action(QString::fromLatin1(id)); };
    Quick::MenuTree *tree = menuTree();
    for (const MenuNode &node : {MenuNode::separator(), a(ActionId::kInsertHyperlink), a(ActionId::kCalcEditHyperlink),
                                 a(ActionId::kCalcOpenHyperlink), a(ActionId::kCalcRemoveHyperlink)}) {
        tree->appendTo(QStringLiteral("calcInsertMenu"), node);
    }
    tree->appendTo(QStringLiteral("calcFormatMenu"), MenuNode::separator());
    tree->appendTo(QStringLiteral("calcFormatMenu"),
                   MenuNode::submenu(QStringLiteral("calcHideUnhideMenu"), tr("&Hide and Unhide"),
                                     {a(ActionId::kCalcHideRows), a(ActionId::kCalcUnhideRows),
                                      a(ActionId::kCalcHideColumns), a(ActionId::kCalcUnhideColumns)}));
    tree->appendTo(QStringLiteral("calcFormatMenu"), a(ActionId::kCalcLockCell));
    tree->appendTo(QStringLiteral("calcFormatMenu"), a(ActionId::kCalcHideFormula));
    tree->appendTo(QStringLiteral("calcDataMenu"), MenuNode::separator());
    tree->appendTo(QStringLiteral("calcDataMenu"),
                   MenuNode::submenu(QStringLiteral("calcOutlineMenu"), tr("&Outline"),
                                     {a(ActionId::kCalcGroup), a(ActionId::kCalcUngroup), MenuNode::separator(),
                                      a(ActionId::kCalcShowDetail), a(ActionId::kCalcHideDetail)}));
    tree->insertMenuBefore(QStringLiteral("aiMenu"),
                           MenuNode::submenu(QStringLiteral("calcReviewMenu"), tr("&Review"),
                                             {a(ActionId::kCalcProtectSheet), a(ActionId::kCalcUnprotectSheet),
                                              MenuNode::separator(), a(ActionId::kCalcProtectWorkbook),
                                              a(ActionId::kCalcUnprotectWorkbook)}));
}

void CalcHost::syncFeaturesFromCursor()
{
    m_syncing = true;
    Quick::ActionModel *model = actions();
    const Workbook *wb = m_document->workbook();
    const qint32 active = m_document->activeSheet();
    const Sheet &current = sheet();
    const CellAddr at = cursor();
    const Hyperlink *link = current.hyperlinkAt(at);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcEditHyperlink), link != nullptr);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcRemoveHyperlink), link != nullptr);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcOpenHyperlink),
                      link != nullptr || !hyperlinkFormulaTarget(*wb, active, at).isEmpty());
    const bool rowGroup = groupAt(current.outline(), OutlineAxis::Rows, at.row).has_value();
    const bool colGroup = groupAt(current.outline(), OutlineAxis::Columns, at.col).has_value();
    model->setEnabled(QString::fromLatin1(ActionId::kCalcShowDetail), rowGroup || colGroup);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcHideDetail), rowGroup || colGroup);
    const bool sheetLocked = current.protection().enabled;
    model->setEnabled(QString::fromLatin1(ActionId::kCalcProtectSheet), !sheetLocked);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcUnprotectSheet), sheetLocked);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcProtectWorkbook), !wb->protection.lockStructure);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcUnprotectWorkbook), wb->protection.isProtected());
    const CellStyle *style = wb->style(current.styleIdAt(at));
    model->setChecked(QString::fromLatin1(ActionId::kCalcLockCell), style == nullptr || style->locked);
    model->setChecked(QString::fromLatin1(ActionId::kCalcHideFormula), style != nullptr && style->hiddenFormula);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcLockCell), !sheetLocked);
    model->setEnabled(QString::fromLatin1(ActionId::kCalcHideFormula), !sheetLocked);
    m_syncing = false;
}

QVariantMap CalcHost::linkAtCursor() const
{
    QVariantMap out;
    QStringList sheets = sheetNames();
    out.insert(QStringLiteral("sheets"), sheets);
    const CellAddr at = cursor();
    const Hyperlink *link = sheet().hyperlinkAt(at);
    out.insert(QStringLiteral("hasLink"), link != nullptr);
    out.insert(QStringLiteral("address"), link != nullptr ? link->address : QString());
    out.insert(QStringLiteral("location"), link != nullptr ? link->location : QString());
    out.insert(QStringLiteral("tooltip"), link != nullptr ? link->tooltip : QString());
    out.insert(QStringLiteral("display"), m_document->displayTextAt(at));
    // A place in this workbook, split for the dialog's sheet and cell.
    QString locationSheet = sheet().name();
    QString locationCell;
    if (link != nullptr && !link->location.isEmpty()) {
        const ParseResult parsed = parseFormula(link->location);
        if (parsed.root && parsed.root->kind == Node::Kind::Ref) {
            RangeRef ref = parsed.root->ref;
            locationSheet = ref.sheet.isEmpty() ? locationSheet : unquotedSheetName(ref.sheet);
            ref.sheet.clear();
            locationCell = rangeRefText(ref);
        } else {
            locationCell = link->location; // a defined name
        }
    }
    out.insert(QStringLiteral("locationSheet"), locationSheet);
    out.insert(QStringLiteral("locationCell"), locationCell);
    return out;
}

void CalcHost::applyLink(const QVariantMap &values)
{
    Hyperlink link;
    const QString address = values.value(QStringLiteral("address")).toString().trimmed();
    if (!address.isEmpty()) {
        setHyperlinkTarget(&link, address);
    } else {
        link.location = values.value(QStringLiteral("location")).toString().trimmed();
        // The dialog's sheet and cell: a defined name stands alone.
        const QString sheetName = values.value(QStringLiteral("sheet")).toString();
        const QString cell = values.value(QStringLiteral("cell")).toString().trimmed();
        if (link.location.isEmpty() && !cell.isEmpty()) {
            const ParseResult parsed = parseFormula(cell);
            const bool reference = parsed.root && parsed.root->kind == Node::Kind::Ref;
            link.location = reference && !sheetName.isEmpty() ? quotedSheetName(sheetName) + QLatin1Char('!') + cell : cell;
        }
    }
    link.display = values.value(QStringLiteral("display")).toString();
    link.tooltip = values.value(QStringLiteral("tooltip")).toString();
    if (link.address.isEmpty() && link.location.isEmpty()) {
        showStatusMessage(tr("A link needs an address or a place in this workbook."), 8000);
        return;
    }
    if (!link.location.isEmpty()
        && !resolveLinkLocation(*m_document->workbook(), m_document->activeSheet(), link.location)) {
        showStatusMessage(tr("“%1” is not a place in this workbook.").arg(link.location), 8000);
        return;
    }
    m_document->setHyperlink(selection(), link);
    refreshGrid();
    syncFromCursor();
}

bool CalcHost::openLinkAt(int row, int col)
{
    const Workbook *wb = m_document->workbook();
    const qint32 active = m_document->activeSheet();
    const CellAddr at{qint32(row), qint32(col)};
    QString target;
    QString location;
    if (const Hyperlink *link = sheet().hyperlinkAt(at)) {
        (link->address.isEmpty() ? location : target) = link->address.isEmpty() ? link->location : link->address;
    } else {
        target = hyperlinkFormulaTarget(*wb, active, at);
        if (target.startsWith(QLatin1Char('#'))) {
            location = target.mid(1);
            target.clear();
        }
    }
    if (!location.isEmpty()) {
        const auto place = resolveLinkLocation(*wb, active, location);
        if (!place || wb->sheetAt(place->sheet).isHidden()) {
            showStatusMessage(tr("The link's place “%1” is not a visible place in this workbook.").arg(location), 8000);
            return true;
        }
        if (place->sheet != active) {
            setActiveSheet(int(viewSheetIndex(place->sheet)));
        }
        if (m_grid != nullptr) {
            m_grid->setCursorCell(place->range.topLeft);
            if (place->range.topLeft != place->range.bottomRight) {
                m_grid->setCursorCell(place->range.bottomRight, true);
            }
        }
        return true;
    }
    if (target.isEmpty()) {
        return false;
    }
    // A bare file name is the file next to this workbook, as in Excel.
    QUrl url(target);
    if (url.scheme().isEmpty() || (url.scheme().size() == 1 && target.contains(QLatin1Char(':')))) {
        const QFileInfo here(m_document->canonicalPath());
        url = QUrl::fromLocalFile(QFileInfo(here.dir(), target).absoluteFilePath());
    }
    if (!url.isValid() || !safeToOpen(url)) {
        showStatusMessage(tr("QindaCalc does not open “%1” from a link.").arg(target), 8000);
        return true;
    }
    if (!(m_openUrl ? m_openUrl(url) : QDesktopServices::openUrl(url))) {
        showStatusMessage(tr("Could not open %1").arg(url.toDisplayString()), 8000);
    }
    return true;
}

void CalcHost::protectSheetWith(const QVariantMap &options)
{
    // The dialog says what stays ALLOWED; the model stores what is locked.
    SheetProtection protection;
    const auto allowed = [&options](const char *key, bool fallback) {
        return options.value(QString::fromLatin1(key), fallback).toBool();
    };
    protection.selectLockedCells = !allowed("selectLockedCells", true);
    protection.selectUnlockedCells = !allowed("selectUnlockedCells", true);
    protection.formatCells = !allowed("formatCells", false);
    protection.formatColumns = !allowed("formatColumns", false);
    protection.formatRows = !allowed("formatRows", false);
    protection.insertColumns = !allowed("insertColumns", false);
    protection.insertRows = !allowed("insertRows", false);
    protection.insertHyperlinks = !allowed("insertHyperlinks", false);
    protection.deleteColumns = !allowed("deleteColumns", false);
    protection.deleteRows = !allowed("deleteRows", false);
    protection.sort = !allowed("sort", false);
    protection.autoFilter = !allowed("autoFilter", false);
    protection.objects = !allowed("objects", false);
    protection.scenarios = !allowed("scenarios", false);
    m_document->protectSheet(protection, options.value(QStringLiteral("password")).toString());
    refreshGrid();
    syncFromCursor();
}

bool CalcHost::unprotectSheetWith(const QString &password)
{
    const bool done = m_document->unprotectSheet(password);
    syncFromCursor();
    return done;
}

void CalcHost::protectWorkbookWith(const QString &password)
{
    m_document->protectWorkbook(password);
    syncFromCursor();
}

bool CalcHost::unprotectWorkbookWith(const QString &password)
{
    const bool done = m_document->unprotectWorkbook(password);
    syncFromCursor();
    return done;
}

void CalcHost::showOutlineLevel(bool rows, int level)
{
    m_document->showOutlineLevel(rows ? OutlineAxis::Rows : OutlineAxis::Columns, level);
    refreshGrid();
    syncFromCursor();
}

void CalcHost::toggleOutlineGroup(bool rows, int line, int level)
{
    const OutlineAxis axis = rows ? OutlineAxis::Rows : OutlineAxis::Columns;
    for (const OutlineGroup &group : outlineGroups(sheet().outline(), axis)) {
        const qint32 button = group.summary >= 0 ? group.summary : group.first;
        if (button == line && group.level == level) {
            m_document->setGroupShown(axis, group, group.collapsed);
            break;
        }
    }
    refreshGrid();
    syncFromCursor();
}

} // namespace QindaOffice::Apps::QindaCalc
