// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaCalc document: a calc_engine Workbook behind the suite's Core::
// Document interface (plan section 6.2). Loading/saving dispatches on the
// extension: .xlsx natively via calc_io, .csv/.tsv through the engine's
// RFC 4180 reader, and foreign formats (ods/xls...) through the headless
// LibreOffice bridge in a later pass (M3 documents the ods path).

#include "document.h"
#include "workbook.h"

namespace QindaOffice::Calc {
class Workbook;
}

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

class SpreadsheetDocument final : public Core::Document {
    Q_OBJECT
public:
    SpreadsheetDocument();

    [[nodiscard]] CalcEng::Workbook *workbook() { return &m_workbook; }
    [[nodiscard]] const CalcEng::Workbook *workbook() const { return &m_workbook; }

    bool load(const QString &path, QString *error = nullptr) override;
    bool save(const QString &path, QString *error = nullptr) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QByteArray recoverySnapshot() const override;
    bool loadRecovery(const QByteArray &snapshot, QString *error = nullptr) override;

    // UI-facing edits route through here so dirty/content signals stay
    // uniform for journaling and title updates.
    void applyInput(const CalcEng::CellAddr &addr, const QString &input);
    void setActiveSheet(qint32 index) { m_activeSheet = index; }
    [[nodiscard]] qint32 activeSheet() const { return m_activeSheet; }

private:
    CalcEng::Workbook m_workbook;
    qint32 m_activeSheet = 0;
};

} // namespace QindaOffice::Apps::QindaCalc
