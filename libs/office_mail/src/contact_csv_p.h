// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the CSV contact reader (M64): the column layout found in the
// header row (contact_csv.cpp) and the row → Contact mapping
// (contact_csv_row.cpp). Not part of the library's public surface; see
// contact_csv.h.

#include "contact_csv.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>

namespace QindaOffice::Mail::ContactCsv::Detail {

// Column indices per field, -1 when the header has no such column.
struct Layout {
    QList<QPair<QString Contact::*, QList<int>>> scalars;
    QList<QPair<int, QString>> phones;
    int primaryPhone = -1;
    struct Email {
        int address = -1, type = -1, display = -1;
    };
    QList<Email> emails;
    struct Address {
        QString type;
        QList<int> street;
        int city = -1, region = -1, postal = -1, country = -1, poBox = -1;
    };
    QList<Address> addresses;
    QList<QPair<int, QString>> urls;
    QList<int> birthday, anniversary, groups;
    int categories = -1;
    int photo = -1;
    QMap<QString, QMap<int, QHash<QString, int>>> google; // kind → number → field → column
    QList<QPair<int, QString>> unknown;                    // column → X- property name
    int recognized = 0;
};

struct Stats {
    int exchange = 0;
    int invalidEmails = 0;
};

Layout layoutFor(const QStringList &header);
Contact contactFromRow(const QStringList &row, const Layout &layout, Stats &stats);

} // namespace QindaOffice::Mail::ContactCsv::Detail
