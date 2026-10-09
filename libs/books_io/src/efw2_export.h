// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The W-2 file for the Social Security Administration (M71, D-081): EFW2,
// "Specifications for Filing Forms W-2 Electronically", Publication
// 42-007, Tax Year 2026 (Version 2, 9/14/2026) — fixed 512-byte records,
// each followed by CR/LF: RA (submitter), RE (employer), one RW per
// employee (+ an RO when a SIMPLE deferral needs code S), RT (totals), RU
// (RO totals, with any RO) and RF (final).
//
// QindaBooks writes the file; the owner checks it with SSA's free AccuWage
// Online and uploads it through Business Services Online (BSO) — QindaBooks
// never e-files.
//
// AGENT-GUARD (identity numbers): the file carries full SSNs and the EIN
// by its nature; it is written only where the user chose, never logged,
// and its errors name the employee, never the number.

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace QindaOffice::Books {

class CompanyFile;

struct Efw2Options {
    // Refuse identity numbers SSA rejects (an SSN beginning 666 or 9, an
    // EIN beginning 00, 07, 08, 09, 17 … 89) and missing submitter data.
    // Off only for the layout tests, whose numbers are fake on purpose.
    bool validate = true;
};

struct Efw2File {
    bool ok = false;
    QString error;
    QByteArray bytes;        // the file, ASCII
    int employees = 0;
    QStringList warnings;
};

[[nodiscard]] Efw2File buildEfw2(CompanyFile &file, int year, const Efw2Options &options = {});
bool writeEfw2(CompanyFile &file, int year, const QString &path, QString *error = nullptr);

// "S-XXXXXX-Y" with a Luhn check digit over the six digits (EFW2 4.2.6).
[[nodiscard]] bool validSignaturePin(const QString &pin);

} // namespace QindaOffice::Books
