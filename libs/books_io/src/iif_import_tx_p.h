// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to the IIF import's transaction half: one TRNS block as checked
// rows (iif_import_transactions.cpp builds them) and the document builders
// (iif_import_documents.cpp).

#include "iif_import_p.h"

#include <QDate>

namespace QindaOffice::Books::IifInternal {

struct TxRow {
    int line = 0;
    QString accountName;
    qint64 accountId = 0;
    qint64 amount = 0; // + debit, − credit, minor units (IIF's own sign)
    QString name;
    qint64 partyId = 0;
    qint64 classId = 0;
    qint64 itemId = 0;
    QString memo;
};

struct TxPlan {
    int order = 0;     // position in the file
    QString typeText;  // TRNSTYPE as written
    QString type;      // IifReader::normalizedType
    QDate date;
    QDate due;
    QString number;    // DOCNUM
    QString memo;      // the TRNS row's memo
    int firstLine = 0;
    int lastLine = 0;
    QList<TxRow> rows; // rows[0] is the TRNS row
    QString refusal;   // non-empty: not posted
};

// Posts `plan` as its document when the rows have that document's shape;
// the ids of what was created go to `docs`. False leaves the caller to roll
// back and post a journal entry instead. The caller holds a savepoint.
bool postDocument(Context &ctx, const TxPlan &plan, QList<qint64> *docs);

} // namespace QindaOffice::Books::IifInternal
