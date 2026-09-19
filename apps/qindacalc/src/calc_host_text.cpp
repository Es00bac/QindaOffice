// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's text side: find and replace over the active sheet (inputs
// and display text, row-major, wrapping), the New from Template submenu,
// the print paginator and charts. Split from calc_host.cpp for the line
// budget.

#include "calc_host.h"

#include "chart_series.h"
#include "spreadsheet_item.h"
#include "standard_actions.h"
#include "template_loader.h"

#include "chart_spec.h"
#include "sheet.h"

#include <QPainter>
#include <QPrinter>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;
using Quick::ActionEntry;
using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {
// Turns a template display name into a stable, valid id suffix.
QString sanitizedId(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        out.append(c.isLetterOrNumber() ? c : QLatin1Char('_'));
    }
    return out;
}
} // namespace

void CalcHost::createTextActions()
{
    for (const char *id : {ActionId::kEditFind, ActionId::kEditFindNext, ActionId::kEditFindPrevious,
                           ActionId::kEditReplace}) {
        action(QString::fromLatin1(id))->setEnabled(true);
    }
    connect(action(QString::fromLatin1(ActionId::kEditFind)), &ActionEntry::triggered, this,
            [this] { emit findRequested(false); });
    connect(action(QString::fromLatin1(ActionId::kEditReplace)), &ActionEntry::triggered, this,
            [this] { emit findRequested(true); });
    // Find Next/Previous re-open the bar; the bar itself answers them once
    // it is up (CalcWindow.qml wires the entries to its find bar).

    // New from Template: one entry per shipped template, generated ids
    // (calcTemplate_<name>) that stay stable (plan rule 4).
    const QList<Quick::TemplateEntry> templates = Quick::templatesFor(QStringLiteral("QindaCalc"));
    if (!templates.isEmpty()) {
        MenuNode submenu = MenuNode::submenu(QStringLiteral("fileTemplateMenu"), tr("New from &Template"));
        for (const Quick::TemplateEntry &entry : templates) {
            const QString id = QStringLiteral("calcTemplate_%1").arg(sanitizedId(entry.name));
            connect(actions()->add(id, entry.name), &ActionEntry::triggered, this,
                    [this, path = entry.path] { loadTemplate(path); });
            submenu.children.append(MenuNode::action(id));
        }
        menuTree()->insertInto(QStringLiteral("fileMenu"), QString::fromLatin1(ActionId::kFileCloseWindow),
                               submenu);
    }
    // Charts.
    ActionEntry *chart = actions()->add(QString::fromLatin1(ActionId::kInsertChart), tr("&Chart…"), {},
                                        QStringLiteral("chart-column"));
    chart->setEnabled(chartsAvailable());
    connect(chart, &ActionEntry::triggered, this, [this] {
        const CellAddr at = cursor();
        emit chartDialogRequested(QStringLiteral("%1%2:%3%4")
                                      .arg(colName(at.col), QString::number(at.row + 1),
                                           colName(at.col + 1), QString::number(at.row + 6)));
    });
}

// ---- templates ----------------------------------------------------------------------

void CalcHost::loadTemplate(const QString &path)
{
    // Loading a template replaces the workbook: ask first when the current
    // one has unsaved changes (same contract as closing).
    if (m_document->isDirty()) {
        m_pendingTemplate = path;
        emit templateConsentRequested(path);
        return;
    }
    loadTemplateResolved(path, QStringLiteral("discard"));
}

void CalcHost::loadTemplateResolved(const QString &path, const QString &choice)
{
    m_pendingTemplate.clear();
    if (choice == QLatin1String("cancel")) {
        return;
    }
    if (choice == QLatin1String("save")) {
        // A cancelled or failed save also cancels the template load.
        m_pendingTemplate = path;
        auto *once = new QObject(this);
        connect(this, &Quick::DocumentHost::saveFinished, once, [this, once, path](bool ok) {
            once->deleteLater();
            if (ok && m_pendingTemplate == path) {
                loadTemplateResolved(path, QStringLiteral("discard"));
            } else {
                m_pendingTemplate.clear();
            }
        });
        save();
        return;
    }
    QString error;
    if (!m_document->load(path, &error)) {
        // Fail honestly: a broken template must not look like a no-op click.
        showStatusMessage(tr("Template failed: %1").arg(error), 8000);
        return;
    }
    m_document->adoptPath(QString());
    m_document->setDirty(true);
    documentReplaced();
}

// ---- find and replace ---------------------------------------------------------------

QList<CellAddr> CalcHost::matches(const QString &needle, bool caseSensitive) const
{
    QList<CellAddr> hits;
    if (needle.isEmpty()) {
        return hits;
    }
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const Sheet &s = sheet();
    for (auto it = s.cells().constBegin(); it != s.cells().constEnd(); ++it) {
        const CellAddr addr{qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)};
        if (m_document->inputTextAt(addr).contains(needle, cs)
            || m_document->displayTextAt(addr).contains(needle, cs)) {
            hits.append(addr);
        }
    }
    std::sort(hits.begin(), hits.end(), [](const CellAddr &a, const CellAddr &b) {
        return a.row != b.row ? a.row < b.row : a.col < b.col;
    });
    return hits;
}

bool CalcHost::findNext(const QString &needle, bool caseSensitive)
{
    const QList<CellAddr> hits = matches(needle, caseSensitive);
    if (hits.isEmpty()) {
        showStatusMessage(tr("Not found: %1").arg(needle));
        return false;
    }
    const CellAddr at = cursor();
    CellAddr next = hits.first();
    for (const CellAddr &hit : hits) {
        if (hit.row > at.row || (hit.row == at.row && hit.col > at.col)) {
            next = hit;
            break;
        }
    }
    if (next == at) {
        showStatusMessage(tr("Search wrapped around the sheet"));
    }
    if (m_grid != nullptr) {
        m_grid->setCursorCell(next);
    }
    return true;
}

bool CalcHost::findPrevious(const QString &needle, bool caseSensitive)
{
    const QList<CellAddr> hits = matches(needle, caseSensitive);
    if (hits.isEmpty()) {
        showStatusMessage(tr("Not found: %1").arg(needle));
        return false;
    }
    const CellAddr at = cursor();
    CellAddr next = hits.last();
    for (auto it = hits.crbegin(); it != hits.crend(); ++it) {
        if (it->row < at.row || (it->row == at.row && it->col < at.col)) {
            next = *it;
            break;
        }
    }
    if (next == at) {
        showStatusMessage(tr("Search wrapped around the sheet"));
    }
    if (m_grid != nullptr) {
        m_grid->setCursorCell(next);
    }
    return true;
}

void CalcHost::replaceCurrent(const QString &needle, const QString &replacement, bool caseSensitive)
{
    if (needle.isEmpty()) {
        return;
    }
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    // AGENT-GUARD: replace in the INPUT text, never the displayed text.
    const CellAddr at = cursor();
    const QString input = m_document->inputTextAt(at);
    if (!input.contains(needle, cs)) {
        findNext(needle, caseSensitive); // not on a match: go to the next one
        return;
    }
    QString replaced = input;
    replaced.replace(needle, replacement, cs);
    m_document->applyInput(at, replaced);
    refreshGrid();
    showStatusMessage(tr("Replaced in %1").arg(addrText(at)));
    findNext(needle, caseSensitive);
}

int CalcHost::replaceAll(const QString &needle, const QString &replacement, bool caseSensitive)
{
    if (needle.isEmpty()) {
        return 0;
    }
    const Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QList<QPair<CellAddr, QString>> edits;
    for (const CellAddr &addr : matches(needle, caseSensitive)) {
        const QString input = m_document->inputTextAt(addr);
        if (!input.contains(needle, cs)) {
            continue; // matched on display text only
        }
        QString replaced = input;
        replaced.replace(needle, replacement, cs);
        edits.append({addr, replaced});
    }
    if (edits.isEmpty()) {
        showStatusMessage(tr("Not found: %1").arg(needle));
        return 0;
    }
    // One undo step for the whole sweep.
    m_document->applyInputs(edits, tr("Replace all"));
    refreshGrid();
    showStatusMessage(tr("Replaced %n occurrence(s)", nullptr, int(edits.size())));
    return int(edits.size());
}

// ---- print --------------------------------------------------------------------------

Quick::PrintRenderCallback CalcHost::printRenderCallback() const
{
    // Snapshot what to print now; the render runs synchronously inside the
    // print job, so the document cannot move under it. A print area set on
    // the sheet (from the file) wins over the used range.
    const SpreadsheetDocument *doc = m_document;
    const qint32 sheetIndex = m_document->activeSheet();
    const Sheet &sheet = m_document->workbook()->sheetAt(sheetIndex);
    const CellRange used = sheet.printArea().has_value()
        ? *sheet.printArea()
        : (m_grid != nullptr ? m_grid->usedRange() : CellRange{});
    const bool landscape = sheet.pageSetup().orientation == 1;
    return [doc, sheetIndex, used, landscape](QPrinter *printer, QPainter *painter) {
        if (!used.isValid() || sheetIndex >= doc->workbook()->sheetCount()) {
            return;
        }
        if (landscape && printer->pageLayout().orientation() != QPageLayout::Landscape) {
            printer->setPageOrientation(QPageLayout::Landscape);
        }
        const Sheet &s = doc->workbook()->sheetAt(sheetIndex);
        const QRectF page(0, 0, printer->width(), printer->height());
        // Paginate the used range by rows, fitting the columns to the page
        // width. A row never splits across pages.
        qreal totalWidth = 0;
        for (qint32 col = used.topLeft.col; col <= used.bottomRight.col; ++col) {
            totalWidth += s.columnWidth(col);
        }
        const qreal scale = totalWidth > 0 ? qMin<qreal>(1.0, page.width() / totalWidth) : 1.0;
        painter->save();
        painter->scale(scale, scale);
        const qreal pageHeight = page.height() / scale;
        qreal y = 0;
        for (qint32 row = used.topLeft.row; row <= used.bottomRight.row; ++row) {
            const qreal h = s.rowHeight(row);
            if (y > 0 && y + h > pageHeight) {
                printer->newPage();
                y = 0;
            }
            qreal x = 0;
            for (qint32 col = used.topLeft.col; col <= used.bottomRight.col; ++col) {
                const qreal w = s.columnWidth(col);
                const QRectF cell(x, y, w, h);
                painter->drawRect(cell);
                const QString text = doc->displayTextAt(sheetIndex, {row, col});
                if (!text.isEmpty()) {
                    const Cell *data = s.cellAt({row, col});
                    const Qt::Alignment align = Qt::AlignVCenter
                        | ((data != nullptr && (isNumber(data->value) || isBool(data->value)))
                               ? Qt::AlignRight
                               : Qt::AlignLeft);
                    painter->drawText(cell.adjusted(3, 0, -3, 0), int(align), text);
                }
                x += w;
            }
            y += h;
        }
        painter->restore();
    };
}

// ---- charts -------------------------------------------------------------------------

bool CalcHost::chartsAvailable() const
{
#ifdef QQO_WITH_CHARTS
    return true;
#else
    return false;
#endif
}

QVariantList CalcHost::charts() const
{
    QVariantList list;
    int index = 0;
    for (const ChartSpec &spec : m_document->workbook()->charts) {
        list.append(QVariantMap{{QStringLiteral("index"), index++},
                                {QStringLiteral("title"), spec.title},
                                {QStringLiteral("kind"), int(spec.kind)},
                                {QStringLiteral("rangeText"), spec.rangeText},
                                {QStringLiteral("anchorRow"), spec.anchorRow},
                                {QStringLiteral("anchorCol"), spec.anchorCol},
                                {QStringLiteral("widthPx"), spec.widthPx},
                                {QStringLiteral("heightPx"), spec.heightPx}});
    }
    return list;
}

void CalcHost::insertChartSpec(const QVariantMap &map)
{
    ChartSpec spec;
    spec.kind = ChartSpec::Kind(qBound(0, map.value(QStringLiteral("kind")).toInt(), int(ChartSpec::Kind::Scatter)));
    spec.title = map.value(QStringLiteral("title")).toString();
    spec.rangeText = map.value(QStringLiteral("rangeText")).toString().trimmed().toUpper();
    spec.headerH = map.value(QStringLiteral("headerH")).toBool();
    spec.headerV = map.value(QStringLiteral("headerV")).toBool();
    spec.sheet = m_document->activeSheet();
    spec.anchorRow = cursor().row;
    spec.anchorCol = cursor().col;
    // Route through the document: snapshot + undo command + dirty + notify.
    m_document->addChart(spec);
}

QVariantMap CalcHost::chartSeries(const QVariantMap &map) const
{
    ChartSpec spec;
    spec.kind = ChartSpec::Kind(qBound(0, map.value(QStringLiteral("kind")).toInt(), int(ChartSpec::Kind::Scatter)));
    spec.rangeText = map.value(QStringLiteral("rangeText")).toString().trimmed().toUpper();
    spec.headerH = map.value(QStringLiteral("headerH")).toBool();
    spec.headerV = map.value(QStringLiteral("headerV")).toBool();
    spec.sheet = map.contains(QStringLiteral("sheet")) ? qint32(map.value(QStringLiteral("sheet")).toInt())
                                                      : m_document->activeSheet();
    return chartSeriesData(spec, *m_document->workbook());
}

QVariantMap CalcHost::chartSeriesAt(int index) const
{
    const auto &charts = m_document->workbook()->charts;
    if (index < 0 || index >= charts.size()) {
        return {};
    }
    return chartSeriesData(charts.at(index), *m_document->workbook());
}

} // namespace QindaOffice::Apps::QindaCalc
