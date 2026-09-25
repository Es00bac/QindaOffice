// SPDX-License-Identifier: GPL-3.0-or-later
#include "recurring.h"

#include "company_file.h"
#include "sales.h"
#include "sql_util.h"

#include <QSqlQuery>

namespace QindaOffice::Books {

Recurring::Recurring(CompanyFile &file)
    : m_file(file)
{
}

QDate Recurring::occurrence(const RecurringSchedule &s, int index)
{
    if (!s.start.isValid() || index < 0 || s.interval < 1 || (s.maxOccurrences > 0 && index >= s.maxOccurrences)) {
        return {};
    }
    const qint64 step = qint64(index) * s.interval;
    QDate date;
    switch (s.frequency) {
    case Frequency::Daily:
        date = s.start.addDays(step);
        break;
    case Frequency::Weekly:
        date = s.start.addDays(step * 7);
        break;
    case Frequency::Monthly:
    case Frequency::Yearly: {
        const int months = int(s.frequency == Frequency::Monthly ? step : step * 12);
        const QDate first = QDate(s.start.year(), s.start.month(), 1).addMonths(months);
        const int wanted = s.dayOfMonth > 0 ? s.dayOfMonth : s.start.day();
        date = QDate(first.year(), first.month(), qMin(wanted, first.daysInMonth()));
        break;
    }
    }
    if (s.end.isValid() && date > s.end) {
        return {};
    }
    return date;
}

Result Recurring::create(const QString &name, BusinessDocument body, const RecurringSchedule &schedule, bool postOpen)
{
    if (name.trimmed().isEmpty() || !schedule.start.isValid() || schedule.interval < 1) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a schedule needs a name, a start date and an interval"));
    }
    if (body.lines.isEmpty() || body.partyId == 0) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("the template needs a customer and lines"));
    }
    Documents documents(m_file);
    body.id = 0;
    body.kind = DocKind::InvoiceTemplate;
    body.state = DocState::Open;
    body.date = schedule.start;
    body.number.clear();
    documents.fillFromItems(body, false);
    documents.finalize(body);
    SqlTransaction txn(m_file);
    if (const Result w = documents.write(body); !w.ok) {
        return w;
    }
    QString error;
    const qint64 id = Sql::insert(
        m_file.database(),
        QStringLiteral("INSERT INTO recurring_templates(name, doc_id, frequency, interval, day_of_month, start_date,"
                       " end_date, max_occurrences, generated, post_open, active) VALUES(?,?,?,?,?,?,?,?,0,?,1)"),
        {name.trimmed(), body.id, int(schedule.frequency), schedule.interval, schedule.dayOfMonth,
         Sql::isoDate(schedule.start), schedule.end.isValid() ? QVariant(Sql::isoDate(schedule.end)) : QVariant(),
         schedule.maxOccurrences, postOpen ? 1 : 0},
        &error);
    if (id == 0) {
        return Result::failure(ErrorCode::Storage, error);
    }
    Sql::exec(m_file.database(), QStringLiteral("UPDATE documents SET template_id = ? WHERE id = ?"), {id, body.id});
    m_file.audit(QStringLiteral("create"), QStringLiteral("recurring"), id, {}, name);
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(id);
}

Result Recurring::setActive(qint64 templateId, bool active)
{
    Sql::exec(m_file.database(), QStringLiteral("UPDATE recurring_templates SET active = ? WHERE id = ?"),
              {active ? 1 : 0, templateId});
    m_file.audit(active ? QStringLiteral("activate") : QStringLiteral("pause"), QStringLiteral("recurring"), templateId,
                 {}, {});
    emit m_file.changed();
    return Result::success(templateId);
}

std::optional<RecurringTemplate> Recurring::recurringTemplate(qint64 id) const
{
    RecurringTemplate t;
    qint64 docId = 0;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT id, name, doc_id, frequency, interval, day_of_month, start_date, end_date,"
                                   " max_occurrences, generated, post_open, active FROM recurring_templates WHERE id = ?"),
                 {id});
        if (!q.next()) {
            return std::nullopt;
        }
        t.id = q.value(0).toLongLong();
        t.name = q.value(1).toString();
        docId = q.value(2).toLongLong();
        t.schedule.frequency = Frequency(q.value(3).toInt());
        t.schedule.interval = q.value(4).toInt();
        t.schedule.dayOfMonth = q.value(5).toInt();
        t.schedule.start = Sql::fromIso(q.value(6));
        t.schedule.end = Sql::fromIso(q.value(7));
        t.schedule.maxOccurrences = q.value(8).toInt();
        t.generated = q.value(9).toInt();
        t.postOpen = q.value(10).toBool();
        t.active = q.value(11).toBool();
    }
    if (const std::optional<BusinessDocument> body = Documents(m_file).load(docId)) {
        t.body = *body;
    }
    return t;
}

QList<RecurringTemplate> Recurring::templates() const
{
    QList<qint64> ids;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT id FROM recurring_templates ORDER BY id"));
        while (q.next()) {
            ids << q.value(0).toLongLong();
        }
    }
    QList<RecurringTemplate> out;
    for (qint64 id : ids) {
        if (const std::optional<RecurringTemplate> t = recurringTemplate(id)) {
            out << *t;
        }
    }
    return out;
}

QList<QDate> Recurring::dueDates(qint64 templateId, const QDate &today) const
{
    const std::optional<RecurringTemplate> t = recurringTemplate(templateId);
    QList<QDate> out;
    if (!t || !t->active) {
        return out;
    }
    for (int k = t->generated;; ++k) {
        const QDate d = occurrence(t->schedule, k);
        if (!d.isValid() || d > today) {
            break;
        }
        out << d;
    }
    return out;
}

Result Recurring::generateDue(const QDate &today, QList<qint64> *created, const PostOptions &options)
{
    QList<qint64> ids;
    SqlTransaction txn(m_file);
    Sales sales(m_file);
    for (const RecurringTemplate &t : templates()) {
        const QList<QDate> due = dueDates(t.id, today);
        int generated = t.generated;
        for (const QDate &date : due) {
            BusinessDocument invoice = t.body;
            invoice.id = 0;
            invoice.kind = DocKind::Invoice;
            invoice.state = t.postOpen ? DocState::Open : DocState::Draft;
            invoice.date = date;
            invoice.dueDate = QDate();
            invoice.number.clear();
            invoice.templateId = t.id;
            for (DocumentLine &line : invoice.lines) {
                line.id = 0;
            }
            const Result r = sales.createInvoice(invoice, options);
            if (!r.ok) {
                return r;
            }
            ids << r.id;
            ++generated;
        }
        if (generated != t.generated) {
            Sql::exec(m_file.database(), QStringLiteral("UPDATE recurring_templates SET generated = ? WHERE id = ?"),
                      {generated, t.id});
        }
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    if (created != nullptr) {
        *created = ids;
    }
    return Result::success(ids.value(0));
}

} // namespace QindaOffice::Books
