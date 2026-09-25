// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Recurring invoices: a template document (kind InvoiceTemplate, never
// posts) + a schedule; generateDue(today) creates every occurrence dated
// on or before `today` that has not been generated yet.
//
// AGENT-CONTRACT (determinism): occurrence(k) is a pure function of the
// schedule and k — computed from the START date, never by stepping from
// the previous occurrence — so a monthly schedule starting Jan 31 gives
// Jan 31, Feb 28/29, Mar 31, Apr 30 … and never drifts to the 28th. The
// template stores how many occurrences it generated; running
// generateDue() twice with the same `today` creates nothing the second
// time. The engine never reads the clock for this: the app passes today.

#include "books_types.h"
#include "documents.h"
#include "journal.h"

#include <QDate>
#include <QList>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

enum class Frequency { Daily = 1, Weekly = 2, Monthly = 3, Yearly = 4 };

struct RecurringSchedule {
    Frequency frequency = Frequency::Monthly;
    int interval = 1;       // every N periods
    int dayOfMonth = 0;     // monthly/yearly: 1..31 (clamped); 0 = start's day
    QDate start;
    QDate end;              // invalid = open-ended
    int maxOccurrences = 0; // 0 = unlimited
};

struct RecurringTemplate {
    qint64 id = 0;
    QString name;
    BusinessDocument body; // kind InvoiceTemplate
    RecurringSchedule schedule;
    bool postOpen = true;  // false: generated invoices are drafts
    int generated = 0;
    bool active = true;
};

class Recurring {
public:
    explicit Recurring(CompanyFile &file);

    // `body` is an invoice's content (customer, lines, discount, tax);
    // its date is ignored.
    Result create(const QString &name, BusinessDocument body, const RecurringSchedule &schedule,
                  bool postOpen = true);
    Result setActive(qint64 templateId, bool active);
    [[nodiscard]] QList<RecurringTemplate> templates() const;
    [[nodiscard]] std::optional<RecurringTemplate> recurringTemplate(qint64 id) const;

    // The k-th (0-based) occurrence date; invalid past end/max.
    [[nodiscard]] static QDate occurrence(const RecurringSchedule &schedule, int index);
    // Occurrences not generated yet and dated ≤ today.
    [[nodiscard]] QList<QDate> dueDates(qint64 templateId, const QDate &today) const;
    // Generates every due occurrence of every active template, oldest
    // first. Created invoice ids go to `created`.
    Result generateDue(const QDate &today, QList<qint64> *created = nullptr,
                       const PostOptions &options = {});

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
