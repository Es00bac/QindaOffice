// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_tools.h"

#include "calc_host.h"
#include "spreadsheet_document.h"
#include "spreadsheet_item.h"

#include "chart_spec.h"
#include "workbook.h"

#include <QJsonObject>

#include <algorithm>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

CellAddr parseCell(const QString &text)
{
    const CellRange range = parseChartRange(text.trimmed());
    return range.isValid() && range.topLeft == range.bottomRight ? range.topLeft : CellAddr{};
}

QString rangeText(const CellRange &range)
{
    return range.topLeft == range.bottomRight
        ? addrText(range.topLeft)
        : addrText(range.topLeft) + QLatin1Char(':') + addrText(range.bottomRight);
}

} // namespace

QString CalcTools::snapshot(int budget)
{
    Workbook *book = m_document->workbook();
    const qint32 active = m_document->activeSheet();
    QStringList sheets;
    for (qint32 i = 0; i < book->sheetCount(); ++i) {
        sheets.append(book->sheetName(i) + (i == active ? QStringLiteral(" (active)") : QString()));
    }
    QString out = QStringLiteral("Sheets: %1. You edit the active sheet.\n").arg(sheets.join(QStringLiteral(", ")));
    if (const SpreadsheetItem *grid = m_host->grid()) {
        out += QStringLiteral("Cursor: %1. Selection: %2.\n")
                   .arg(addrText(grid->cursorCell()), rangeText(grid->selectionRange()));
    }
    QList<CellAddr> filled;
    for (auto it = book->sheetAt(active).cells().cbegin(); it != book->sheetAt(active).cells().cend(); ++it) {
        filled.append({qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)});
    }
    std::sort(filled.begin(), filled.end(), [](const CellAddr &a, const CellAddr &b) {
        return a.row != b.row ? a.row < b.row : a.col < b.col;
    });
    if (filled.isEmpty()) {
        return out + QStringLiteral("The active sheet is empty.\n");
    }
    out += QStringLiteral("Filled cells (address: input, and → result for formulas):\n");
    int shown = 0;
    qint32 row = -1;
    for (const CellAddr &addr : filled) {
        const QString input = m_document->inputTextAt(addr);
        if (input.isEmpty()) {
            continue;
        }
        if (++shown > kMaxSnapshotCells || out.size() > budget) {
            out += QStringLiteral("\n(… %1 more filled cells not shown)\n").arg(filled.size() - shown + 1);
            break;
        }
        out += addr.row != row ? QStringLiteral("\n") : QStringLiteral(" | ");
        row = addr.row;
        out += addrText(addr) + QStringLiteral(": ") + input;
        if (input.startsWith(QLatin1Char('='))) {
            out += QStringLiteral(" → ") + m_document->displayTextAt(addr);
        }
    }
    return out + QLatin1Char('\n');
}

QString CalcTools::operationGuide() const
{
    return QStringLiteral(
        "Cells use A1 addresses on the active sheet. An input is what a user would type: a "
        "number, text, a date, or a formula starting with = (e.g. =SUM(B2:B9)). \"\" empties "
        "a cell.\n"
        "- {\"op\":\"set_cells\",\"cells\":{\"A1\":\"Total\",\"B1\":\"=SUM(B2:B9)\"}}\n"
        "- {\"op\":\"set_range\",\"start\":\"A2\",\"rows\":[[\"Jan\",120],[\"Feb\",95]]} — a "
        "block of rows starting at a cell (good for tables).\n"
        "- {\"op\":\"clear\",\"range\":\"D2:D40\"}\n"
        "Prefer formulas over computed numbers so the sheet stays live.");
}

Ai::EditOutcome CalcTools::apply(const QJsonArray &edits)
{
    Ai::EditOutcome outcome;
    if (m_guard) {
        const QString reason = m_guard();
        if (!reason.isEmpty()) {
            outcome.errors.append(reason);
            return outcome;
        }
    }
    QList<QPair<CellAddr, QString>> inputs;
    const auto text = [](const QJsonValue &value) {
        return value.isDouble() ? QString::number(value.toDouble(), 'g', 15)
             : value.isBool()   ? (value.toBool() ? QStringLiteral("TRUE") : QStringLiteral("FALSE"))
                                : value.toString();
    };
    for (const QJsonValue &value : edits) {
        const QJsonObject edit = value.toObject();
        const QString op = edit.value(QStringLiteral("op")).toString();
        const qsizetype before = inputs.size();
        if (op == QLatin1String("set_cells")) {
            const QJsonObject cells = edit.value(QStringLiteral("cells")).toObject();
            for (auto it = cells.begin(); it != cells.end(); ++it) {
                const CellAddr addr = parseCell(it.key());
                if (!addr.isValid()) {
                    outcome.errors.append(QObject::tr("“%1” is not a cell address").arg(it.key()));
                    continue;
                }
                inputs.append({addr, text(it.value())});
            }
        } else if (op == QLatin1String("set_range")) {
            const CellAddr start = parseCell(edit.value(QStringLiteral("start")).toString());
            if (!start.isValid()) {
                outcome.errors.append(QObject::tr("set_range: bad start cell"));
                continue;
            }
            const QJsonArray rows = edit.value(QStringLiteral("rows")).toArray();
            for (qsizetype r = 0; r < rows.size(); ++r) {
                const QJsonArray row = rows.at(r).toArray();
                for (qsizetype c = 0; c < row.size(); ++c) {
                    inputs.append({{start.row + qint32(r), start.col + qint32(c)}, text(row.at(c))});
                }
            }
        } else if (op == QLatin1String("clear")) {
            const CellRange range = parseChartRange(edit.value(QStringLiteral("range")).toString());
            if (!range.isValid() || range.cellCount() > kMaxEditCells) {
                outcome.errors.append(QObject::tr("clear: bad or too large range"));
                continue;
            }
            for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
                for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
                    inputs.append({{r, c}, QString()});
                }
            }
        } else {
            outcome.errors.append(QObject::tr("unknown operation “%1”").arg(op));
            continue;
        }
        if (inputs.size() > kMaxEditCells) {
            outcome.errors.append(QObject::tr("too many cells in one reply; nothing was changed"));
            outcome.changes.clear();
            return outcome;
        }
        const qsizetype count = inputs.size() - before;
        if (count > 0) {
            outcome.changes.append(op == QLatin1String("clear")
                                       ? QObject::tr("cleared %1").arg(edit.value(QStringLiteral("range")).toString())
                                       : QObject::tr("set %n cell(s)", "", int(count)));
        }
    }
    if (!inputs.isEmpty()) {
        // AGENT-CONTRACT (CalcTools and CalcAgent): built-in and external
        // assistants use the same mutation path, including validation. A
        // rule failure rejects the entire batch before one undo step exists.
        const auto &sheet = m_document->workbook()->sheetAt(m_document->activeSheet());
        for (const auto &[address, input] : inputs) {
            if (address.row >= 1048576 || address.col >= 16384) {
                outcome.errors.append(QObject::tr("Cell address is outside worksheet bounds; nothing was changed"));
            } else if (const auto *rule = sheet.validationAt(address);
                       rule != nullptr && !validationAccepts(*rule, input)) {
                outcome.errors.append(addrText(address) + QStringLiteral(": ")
                                      + validationDescription(*rule));
            } else if (m_document->workbook()->isCellLocked(m_document->activeSheet(), address)) {
                // Protection binds the assistants like the keyboard (M66).
                outcome.errors.append(addrText(address) + QStringLiteral(": ")
                                      + QObject::tr("the cell is locked (the sheet is protected)"));
            } else {
                continue;
            }
            outcome.changes.clear();
            return outcome;
        }
        m_document->applyInputs(inputs, QObject::tr("Assistant Edit"));
        if (SpreadsheetItem *grid = m_host->grid()) {
            grid->refreshAll();
        }
    }
    outcome.applied = inputs.isEmpty() ? 0 : int(outcome.changes.size());
    return outcome;
}

void CalcTools::undo()
{
    m_document->undoStack()->undo();
    if (SpreadsheetItem *grid = m_host->grid()) {
        grid->refreshAll();
    }
}

} // namespace QindaOffice::Apps::QindaCalc
