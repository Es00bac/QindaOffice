// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: money in and out (receive payment, pay bills), the open
// documents they apply to, printed forms as PDF, e-mailing an invoice
// through QindaMail, and recurring invoices.
#include "books_service.h"

#include "company_file.h"
#include "documents.h"
#include "form_renderer.h"
#include "lists.h"
#include "purchases.h"
#include "recurring.h"
#include "sales.h"

#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

QString frequencyKey(Frequency f)
{
    switch (f) {
    case Frequency::Daily: return QStringLiteral("daily");
    case Frequency::Weekly: return QStringLiteral("weekly");
    case Frequency::Monthly: return QStringLiteral("monthly");
    case Frequency::Yearly: return QStringLiteral("yearly");
    }
    return QStringLiteral("monthly");
}

Frequency frequencyFrom(const QString &key)
{
    if (key == QLatin1String("daily")) {
        return Frequency::Daily;
    }
    if (key == QLatin1String("weekly")) {
        return Frequency::Weekly;
    }
    return key == QLatin1String("yearly") ? Frequency::Yearly : Frequency::Monthly;
}

bool launchDetached(const QString &program, const QStringList &arguments)
{
    return QProcess::startDetached(program, arguments);
}

} // namespace

QVariantMap BooksService::receivePayment(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const MoneyFormat money = moneyFormat();
    ReceivePaymentInput in;
    in.customerId = Format::id(map.value(QStringLiteral("customerId")));
    in.date = Format::date(map.value(QStringLiteral("date")));
    if (!in.date.isValid()) {
        in.date = todayDate();
    }
    in.depositAccountId = Format::id(map.value(QStringLiteral("depositAccountId")));
    in.reference = map.value(QStringLiteral("reference")).toString().trimmed();
    in.memo = map.value(QStringLiteral("memo")).toString();
    if (in.memo.isEmpty()) {
        in.memo = map.value(QStringLiteral("method")).toString(); // "Check", "Card", …
    }
    QString error;
    qint64 applied = 0;
    for (const QVariant &v : map.value(QStringLiteral("applications")).toList()) {
        const QVariantMap a = v.toMap();
        const std::optional<qint64> minor = money.parse(a.value(QStringLiteral("amount")), &error);
        if (!minor) {
            return Format::failure(error);
        }
        if (*minor != 0) {
            in.applications << PaymentApplication{Format::id(a.value(QStringLiteral("invoiceId"))), c->money(*minor)};
            applied += *minor;
        }
    }
    const QVariant amount = map.value(QStringLiteral("amount"));
    std::optional<qint64> total = applied;
    if (amount.isValid() && !amount.toString().trimmed().isEmpty()) {
        total = money.parse(amount, &error);
        if (!total) {
            return Format::failure(error);
        }
    }
    in.amount = c->money(*total);
    return Format::result(Sales(*c).receivePayment(in));
}

QVariantMap BooksService::payBills(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const MoneyFormat money = moneyFormat();
    QDate date = Format::date(map.value(QStringLiteral("date")));
    if (!date.isValid()) {
        date = todayDate();
    }
    QList<PaymentApplication> payments;
    QString error;
    for (const QVariant &v : map.value(QStringLiteral("bills")).toList()) {
        const QVariantMap b = v.toMap();
        const std::optional<qint64> minor = money.parse(b.value(QStringLiteral("amount")), &error);
        if (!minor) {
            return Format::failure(error);
        }
        if (*minor != 0) {
            payments << PaymentApplication{Format::id(b.value(QStringLiteral("billId"))), c->money(*minor)};
        }
    }
    if (payments.isEmpty()) {
        return Format::failure(tr("Choose at least one bill and the amount to pay."));
    }
    QList<qint64> ids;
    const Result r = Purchases(*c).payBills(date, Format::id(map.value(QStringLiteral("paymentAccountId"))), payments,
                                            map.value(QStringLiteral("reference")).toString().trimmed(), {}, &ids);
    QVariantMap out = Format::result(r);
    QVariantList idList;
    for (qint64 id : ids) {
        idList << id;
    }
    out.insert(QStringLiteral("ids"), idList);
    return out;
}

namespace {

QVariantMap openRow(const BusinessDocument &d, qint64 balance, const QString &partyName, DocumentStatus status,
                    const MoneyFormat &money, const QDate &today)
{
    return {{QStringLiteral("id"), d.id},
            {QStringLiteral("number"), d.number.isEmpty() ? d.reference : d.number},
            {QStringLiteral("partyId"), d.partyId},
            {QStringLiteral("partyName"), partyName},
            {QStringLiteral("date"), Format::isoDate(d.date)},
            {QStringLiteral("dueDate"), Format::isoDate(d.dueDate)},
            {QStringLiteral("status"), Format::statusKey(status)},
            {QStringLiteral("statusLabel"), Format::statusLabel(status)},
            {QStringLiteral("overdue"), d.dueDate.isValid() && d.dueDate < today},
            {QStringLiteral("total"), d.total.minor()},
            {QStringLiteral("totalText"), money.text(d.total.minor())},
            {QStringLiteral("balance"), balance},
            {QStringLiteral("balanceText"), money.text(balance)},
            {QStringLiteral("balancePlain"), money.plain(balance)}};
}

} // namespace

QVariantList BooksService::openInvoices(qint64 customerId) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    Documents documents(*c);
    const MoneyFormat money = moneyFormat();
    for (const BusinessDocument &d : Sales(*c).openInvoices(customerId)) {
        out << openRow(d, documents.openBalance(d.id).minor(), nameOfParty(d.partyId), documents.status(d, todayDate()),
                       money, todayDate());
    }
    return out;
}

QVariantList BooksService::unpaidBills(qint64 vendorId) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    Documents documents(*c);
    const MoneyFormat money = moneyFormat();
    for (const BusinessDocument &d : Purchases(*c).unpaidBills(vendorId)) {
        out << openRow(d, documents.openBalance(d.id).minor(), nameOfParty(d.partyId), documents.status(d, todayDate()),
                       money, todayDate());
    }
    return out;
}

QVariantMap BooksService::exportDocumentPdf(qint64 id, const QUrl &fileUrl)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const QString path = localPath(fileUrl);
    QString error;
    if (!FormRenderer::writeDocumentPdf(*c, id, path, &error)) {
        return Format::failure(error);
    }
    QVariantMap out = Format::success(id);
    out.insert(QStringLiteral("path"), path);
    return out;
}

QVariantMap BooksService::emailDocument(qint64 id)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const std::optional<BusinessDocument> doc = Documents(*c).load(id);
    if (!doc) {
        return Format::failure(tr("That document no longer exists."));
    }
    // The PDF goes to a stable per-user outbox (not a temp file that
    // vanishes before the mail is written).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/qindaoffice/books/outbox");
    QDir().mkpath(dir);
    QString base = doc->number.isEmpty() ? QStringLiteral("document-%1").arg(id) : doc->number;
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    const QString path = dir + QLatin1Char('/') + base + QStringLiteral(".pdf");
    QString error;
    if (!FormRenderer::writeDocumentPdf(*c, id, path, &error)) {
        return Format::failure(error);
    }
    const std::optional<Party> customer = Lists(*c).party(doc->partyId);
    const QString email = customer ? customer->email.trimmed() : QString();
    const QString company = c->settings().displayName();
    const QString kind = Format::kindLabel(doc->kind);
    const QString subject = tr("%1 %2 from %3").arg(kind, doc->number, company);
    const MoneyFormat money = moneyFormat();
    QString body = tr("Hello %1,\n\n%2 %3 for %4").arg(customer ? customer->displayName : QString(), kind,
                                                       doc->number, money.text(doc->total.minor()));
    if (doc->kind == DocKind::Invoice && doc->dueDate.isValid()) {
        body += tr(" is due on %1").arg(money.locale.toString(doc->dueDate, QLocale::LongFormat));
    }
    body += QStringLiteral(".\n\n");
    if (!c->settings().paymentInstructions.isEmpty()) {
        body += c->settings().paymentInstructions + QStringLiteral("\n\n");
    }
    body += tr("Thank you,\n%1").arg(company);
    // AGENT-NOTE (M47): QindaMail's mailto: parser (apps/qindamail,
    // draft_builder.cpp parseMailto) reads to/cc/bcc/subject/body only and
    // takes no attachment argument, so the PDF is NOT attached: we open
    // the draft and say where the PDF is (notify). When QindaMail grows an
    // attachment argument, pass `path` here and set `attached`.
    const QString mailto = QStringLiteral("mailto:") + QString::fromLatin1(QUrl::toPercentEncoding(email, "@+"))
        + QStringLiteral("?subject=") + QString::fromLatin1(QUrl::toPercentEncoding(subject))
        + QStringLiteral("&body=") + QString::fromLatin1(QUrl::toPercentEncoding(body));
    const Launcher launch = m_launcher ? m_launcher : Launcher(launchDetached);
    const bool launched = launch(QStringLiteral("qindamail"), {mailto});
    if (launched) {
        emit notify(tr("Message opened in QindaMail"),
                    tr("QindaMail cannot take attachments from another app yet — attach %1 to the message.").arg(path),
                    QStringLiteral("info"));
    } else {
        emit notify(tr("QindaMail did not start"), tr("The PDF is saved at %1.").arg(path), QStringLiteral("warning"));
    }
    if (email.isEmpty()) {
        emit notify(tr("No e-mail address"), tr("%1 has no e-mail address; fill in the To field.")
                                                 .arg(customer ? customer->displayName : tr("The customer")),
                    QStringLiteral("warning"));
    }
    QVariantMap out = launched ? Format::success(id) : Format::failure(tr("QindaMail could not be started."));
    out.insert(QStringLiteral("path"), path);
    out.insert(QStringLiteral("launched"), launched);
    out.insert(QStringLiteral("attached"), false);
    out.insert(QStringLiteral("mailto"), mailto);
    out.insert(QStringLiteral("to"), email);
    out.insert(QStringLiteral("subject"), subject);
    return out;
}

// ---- recurring invoices ----------------------------------------------------------

QVariantList BooksService::recurringTemplates() const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    Recurring recurring(*c);
    for (const RecurringTemplate &t : recurring.templates()) {
        const QDate next = Recurring::occurrence(t.schedule, t.generated);
        out << QVariantMap{{QStringLiteral("id"), t.id},
                           {QStringLiteral("name"), t.name},
                           {QStringLiteral("customerId"), t.body.partyId},
                           {QStringLiteral("customerName"), nameOfParty(t.body.partyId)},
                           {QStringLiteral("frequency"), frequencyKey(t.schedule.frequency)},
                           {QStringLiteral("interval"), t.schedule.interval},
                           {QStringLiteral("dayOfMonth"), t.schedule.dayOfMonth},
                           {QStringLiteral("start"), Format::isoDate(t.schedule.start)},
                           {QStringLiteral("end"), Format::isoDate(t.schedule.end)},
                           {QStringLiteral("maxOccurrences"), t.schedule.maxOccurrences},
                           {QStringLiteral("nextDate"), Format::isoDate(next)},
                           {QStringLiteral("generated"), t.generated},
                           {QStringLiteral("postOpen"), t.postOpen},
                           {QStringLiteral("active"), t.active},
                           {QStringLiteral("due"), t.active ? recurring.dueDates(t.id, todayDate()).size() : 0},
                           {QStringLiteral("total"), t.body.total.minor()},
                           {QStringLiteral("totalText"), money.text(t.body.total.minor())}};
    }
    return out;
}

QVariantMap BooksService::saveRecurring(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    Recurring recurring(*c);
    const qint64 id = Format::id(map.value(QStringLiteral("id")));
    if (id != 0) {
        // AGENT-NOTE: the engine keeps a schedule immutable once it has
        // generated (occurrence k is a function of the start); a template
        // is paused/resumed here, and a different schedule is a new one.
        if (!map.contains(QStringLiteral("active"))) {
            return Format::failure(tr("A schedule cannot be changed; pause it and create a new one."));
        }
        return Format::result(recurring.setActive(id, map.value(QStringLiteral("active")).toBool()));
    }
    QVariantMap body = map.value(QStringLiteral("document")).toMap();
    body.insert(QStringLiteral("kind"), QStringLiteral("invoice"));
    BusinessDocument doc;
    QString error;
    if (!documentFromMap(body, &doc, &error)) {
        return Format::failure(error);
    }
    RecurringSchedule schedule;
    schedule.frequency = frequencyFrom(map.value(QStringLiteral("frequency")).toString());
    schedule.interval = qMax(1, map.value(QStringLiteral("interval"), 1).toInt());
    schedule.dayOfMonth = map.value(QStringLiteral("dayOfMonth")).toInt();
    schedule.start = Format::date(map.value(QStringLiteral("start")));
    if (!schedule.start.isValid()) {
        schedule.start = todayDate();
    }
    schedule.end = Format::date(map.value(QStringLiteral("end")));
    schedule.maxOccurrences = map.value(QStringLiteral("maxOccurrences")).toInt();
    return Format::result(recurring.create(map.value(QStringLiteral("name")).toString().trimmed(), doc, schedule,
                                           map.value(QStringLiteral("postOpen"), true).toBool()));
}

int BooksService::generateDueRecurring()
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return 0;
    }
    QList<qint64> created;
    const Result r = Recurring(*c).generateDue(todayDate(), &created);
    if (!r.ok) {
        emit notify(tr("Recurring invoices"), r.error, QStringLiteral("danger"));
    } else if (!created.isEmpty()) {
        emit notify(tr("Recurring invoices"), tr("Created %n invoice(s).", nullptr, int(created.size())),
                    QStringLiteral("success"));
    }
    return int(created.size());
}

int BooksService::dueRecurringCount() const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return 0;
    }
    Recurring recurring(*c);
    int due = 0;
    for (const RecurringTemplate &t : recurring.templates()) {
        if (t.active) {
            due += int(recurring.dueDates(t.id, todayDate()).size());
        }
    }
    return due;
}

} // namespace QindaOffice::Apps::Books
