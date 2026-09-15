// SPDX-License-Identifier: GPL-3.0-or-later
#include "spreadsheet_document.h"

#include "csv.h"

#include <QFile>
#include "xlsx_bridge.h"

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

SpreadsheetDocument::SpreadsheetDocument()
{
    m_workbook.addSheet(tr("Sheet1"));
}

bool SpreadsheetDocument::load(const QString &path, QString *error)
{
    CalcEng::Workbook fresh;
    bool ok = false;
    if (path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive)) {
        ok = CalcEng::IO::readXlsxFile(path, &fresh, error);
    } else if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)
               || path.endsWith(QLatin1String(".tsv"), Qt::CaseInsensitive)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        const Csv::Table table = Csv::parse(file.readAll());
        fresh.addSheet(tr("Sheet1"));
        for (int r = 0; r < table.rows.size(); ++r) {
            for (int c = 0; c < table.rows.at(r).size(); ++c) {
                fresh.setCellInput(0, {r, c}, table.cellAt(r, c));
            }
        }
        ok = true;
    } else {
        if (error != nullptr) {
            *error = tr("Unsupported format (ods/xls arrive via the LibreOffice bridge in a later pass).");
        }
        return false;
    }
    if (!ok) {
        return false;
    }
    m_workbook = std::move(fresh);
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    emit contentChanged();
    return true;
}

bool SpreadsheetDocument::save(const QString &path, QString *error)
{
    bool ok = false;
    if (path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive)) {
        ok = CalcEng::IO::writeXlsxFile(m_workbook, path, error);
    } else if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
        const CalcEng::Sheet &sheet = m_workbook.sheetAt(m_activeSheet);
        Csv::Table table;
        qint32 maxRow = 0;
        qint32 maxCol = 0;
        for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
            maxRow = qMax(maxRow, qint32(it.key() & 0xffffffffu));
            maxCol = qMax(maxCol, qint32(it.key() >> 32));
        }
        for (qint32 r = 0; r <= maxRow; ++r) {
            QStringList row;
            for (qint32 c = 0; c <= maxCol; ++c) {
                const CalcEng::Cell *cell = sheet.cellAt({r, c});
                row.append(cell == nullptr ? QString() : CalcEng::toText(cell->value));
            }
            table.rows.append(row);
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return false;
        }
        file.write(Csv::serialize(table));
        ok = true;
    } else {
        if (error != nullptr) {
            *error = tr("Unsupported save format.");
        }
        return false;
    }
    if (!ok) {
        return false;
    }
    setCanonicalPath(path);
    refreshByteRevision();
    setDirty(false);
    return true;
}

QStringList SpreadsheetDocument::mimeTypes() const
{
    return {QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"),
            QStringLiteral("text/csv")};
}

QByteArray SpreadsheetDocument::recoverySnapshot() const
{
    return {}; // xlsx-in-memory snapshot arrives with the journal work; M3
               // documents the gap honestly (shell skips empty snapshots).
}

bool SpreadsheetDocument::loadRecovery(const QByteArray &snapshot, QString *error)
{
    Q_UNUSED(snapshot);
    if (error != nullptr) {
        *error = tr("Recovery snapshots are not available for spreadsheets yet.");
    }
    return false;
}

void SpreadsheetDocument::applyInput(const CalcEng::CellAddr &addr, const QString &input)
{
    m_workbook.setCellInput(m_activeSheet, addr, input);
    setDirty(true);
    emit contentChanged();
}

} // namespace QindaOffice::Apps::QindaCalc
