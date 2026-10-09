// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: money in and out (receive payment, pay bills), the open
// documents they apply to, printed forms as PDF, and e-mailing a form
// through QindaMail (invoices here; statements use the same hand-off).
#include "books_service.h"

#include "check_print.h"
#include "company_file.h"
#include "documents.h"
#include "form_renderer.h"
#include "lists.h"
#include "purchases.h"
#include "sales.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

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
    // M68: `toPrint` puts the payments (by check, from a bank account) in
    // the print queue; printing gives them their numbers.
    const Result r = Purchases(*c).payBills(date, Format::id(map.value(QStringLiteral("paymentAccountId"))), payments,
                                            map.value(QStringLiteral("reference")).toString().trimmed(), {}, &ids,
                                            map.value(QStringLiteral("toPrint")).toBool());
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
    // M63: a check (or bank-paid bill payment) saves as the check it prints.
    const bool written = isPrintableCheck(*c, id) ? CheckRenderer::writeChecksPdf(*c, {id}, path, &error)
                                                  : FormRenderer::writeDocumentPdf(*c, id, path, &error);
    if (!written) {
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
    const QString path = outboxPath(doc->number.isEmpty() ? QStringLiteral("document-%1").arg(id) : doc->number);
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
    const bool order = doc->kind == DocKind::PurchaseOrder; // M68: to the vendor
    if (doc->kind == DocKind::Invoice && doc->dueDate.isValid()) {
        body += tr(" is due on %1").arg(money.locale.toString(doc->dueDate, QLocale::LongFormat));
    } else if (order && doc->dueDate.isValid()) {
        body += tr(", expected by %1").arg(money.locale.toString(doc->dueDate, QLocale::LongFormat));
    }
    body += QStringLiteral(".\n\n");
    // M48: the invoice's Stripe payment link while it charges the balance due.
    if (const QString online = payOnlineLine(id); !online.isEmpty()) {
        body += online + QStringLiteral("\n\n");
    }
    if (!order && !c->settings().paymentInstructions.isEmpty()) {
        body += c->settings().paymentInstructions + QStringLiteral("\n\n");
    }
    body += tr("Thank you,\n%1").arg(company);
    return mailWithAttachment(id, path, email,
                              customer ? customer->displayName : order ? tr("The vendor") : tr("The customer"), subject,
                              body);
}

QString BooksService::outboxPath(const QString &base)
{
    // The PDF goes to a stable per-user outbox (not a temp file that
    // vanishes before the mail is written).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/qindaoffice/books/outbox");
    QDir().mkpath(dir);
    QString name = base;
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    return dir + QLatin1Char('/') + name + QStringLiteral(".pdf");
}

QVariantMap BooksService::mailWithAttachment(qint64 id, const QString &path, const QString &email, const QString &who,
                                             const QString &subject, const QString &body)
{
    // AGENT-CONTRACT (apps/qindamail main.cpp, launch_arguments.h
    // foldAttachArguments): `qindamail --attach <path> mailto:…` opens a
    // composer draft with the file imported into its attachment store. The
    // mailto: URL itself never carries an attachment (RFC 6068 §3).
    const QString mailto = QStringLiteral("mailto:") + QString::fromLatin1(QUrl::toPercentEncoding(email, "@+"))
        + QStringLiteral("?subject=") + QString::fromLatin1(QUrl::toPercentEncoding(subject))
        + QStringLiteral("&body=") + QString::fromLatin1(QUrl::toPercentEncoding(body));
    const Launcher launch = m_launcher ? m_launcher : Launcher(launchDetached);
    const bool launched = launch(QStringLiteral("qindamail"), {QStringLiteral("--attach"), path, mailto});
    if (launched) {
        emit notify(tr("Message opened in QindaMail"), tr("%1 is attached.").arg(QFileInfo(path).fileName()),
                    QStringLiteral("info"));
    } else {
        emit notify(tr("QindaMail did not start"), tr("The PDF is saved at %1.").arg(path), QStringLiteral("warning"));
    }
    if (email.isEmpty()) {
        emit notify(tr("No e-mail address"), tr("%1 has no e-mail address; fill in the To field.").arg(who),
                    QStringLiteral("warning"));
    }
    QVariantMap out = launched ? Format::success(id) : Format::failure(tr("QindaMail could not be started."));
    out.insert(QStringLiteral("path"), path);
    out.insert(QStringLiteral("launched"), launched);
    out.insert(QStringLiteral("attached"), launched);
    out.insert(QStringLiteral("mailto"), mailto);
    out.insert(QStringLiteral("to"), email);
    out.insert(QStringLiteral("subject"), subject);
    return out;
}

} // namespace QindaOffice::Apps::Books
