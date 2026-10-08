// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService (M63, D-071): customer statements — on screen, as PDF and
// to QindaMail through the invoice hand-off — and the company logo the
// printed forms carry. Paper and preview go through BooksHost's print
// source ("statement"), the same books_io painter as the PDF.
#include "books_service.h"

#include "company_file.h"
#include "form_renderer.h"
#include "lists.h"
#include "sales.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageReader>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

// A logo larger than this is refused: it rides in every copy and backup
// of the company file and on every PDF.
constexpr qint64 kMaxLogoBytes = 4 * 1024 * 1024;

// The statement period a caller leaves open: the fiscal year to date.
void resolvePeriod(CompanyFile &file, const QDate &today, QDate *from, QDate *to)
{
    if (!to->isValid()) {
        *to = today;
    }
    if (!from->isValid()) {
        *from = file.settings().fiscalYearStart(*to);
    }
}

} // namespace

QVariantMap BooksService::statement(qint64 customerId, const QString &fromIso, const QString &toIso) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const std::optional<Party> customer = Lists(*c).party(customerId);
    if (!customer || customer->kind != PartyKind::Customer) {
        return Format::failure(tr("Choose a customer."));
    }
    QDate from = Format::date(fromIso);
    QDate to = Format::date(toIso);
    resolvePeriod(*c, todayDate(), &from, &to);
    if (to < from) {
        return Format::failure(tr("The period starts after it ends."));
    }
    const CustomerStatement s = Sales(*c).statement(customerId, from, to);
    const MoneyFormat money = moneyFormat();
    QVariantList lines;
    for (const StatementLine &l : s.lines) {
        lines << QVariantMap{{QStringLiteral("date"), Format::isoDate(l.date)},
                             {QStringLiteral("docId"), l.docId},
                             {QStringLiteral("kind"), Format::kindKey(l.kind)},
                             {QStringLiteral("kindLabel"), Format::kindLabel(l.kind)},
                             {QStringLiteral("number"), l.number},
                             {QStringLiteral("description"), l.description},
                             {QStringLiteral("amount"), l.amount.minor()},
                             {QStringLiteral("amountText"), money.text(l.amount.minor())},
                             {QStringLiteral("balance"), l.balance.minor()},
                             {QStringLiteral("balanceText"), money.text(l.balance.minor())}};
    }
    QVariantList aging;
    for (const auto &[label, amount] :
         {std::pair{tr("Current"), s.aging.current}, std::pair{tr("1–30 days"), s.aging.days1to30},
          std::pair{tr("31–60 days"), s.aging.days31to60}, std::pair{tr("61–90 days"), s.aging.days61to90},
          std::pair{tr("Over 90 days"), s.aging.over90}}) {
        aging << QVariantMap{{QStringLiteral("label"), label},
                             {QStringLiteral("amount"), amount.minor()},
                             {QStringLiteral("amountText"), money.text(amount.minor())}};
    }
    QVariantMap out = Format::success(customerId);
    out.insert(QStringLiteral("customerId"), customerId);
    out.insert(QStringLiteral("customerName"), customer->displayName);
    out.insert(QStringLiteral("email"), customer->email);
    out.insert(QStringLiteral("from"), Format::isoDate(from));
    out.insert(QStringLiteral("to"), Format::isoDate(to));
    out.insert(QStringLiteral("opening"), s.opening.minor());
    out.insert(QStringLiteral("openingText"), money.text(s.opening.minor()));
    out.insert(QStringLiteral("closing"), s.closing.minor());
    out.insert(QStringLiteral("closingText"), money.text(s.closing.minor()));
    out.insert(QStringLiteral("lines"), lines);
    out.insert(QStringLiteral("aging"), aging);
    return out;
}

QVariantMap BooksService::exportStatementPdf(qint64 customerId, const QString &fromIso, const QString &toIso,
                                             const QUrl &fileUrl)
{
    const QVariantMap s = statement(customerId, fromIso, toIso);
    if (!s.value(QStringLiteral("ok")).toBool()) {
        return s;
    }
    const QString path = localPath(fileUrl);
    QString error;
    if (!FormRenderer::writeStatementPdf(*file(), customerId, Format::date(s.value(QStringLiteral("from"))),
                                         Format::date(s.value(QStringLiteral("to"))), path, &error)) {
        return Format::failure(error);
    }
    QVariantMap out = Format::success(customerId);
    out.insert(QStringLiteral("path"), path);
    return out;
}

QVariantMap BooksService::emailStatement(qint64 customerId, const QString &fromIso, const QString &toIso)
{
    const QVariantMap s = statement(customerId, fromIso, toIso);
    if (!s.value(QStringLiteral("ok")).toBool()) {
        return s;
    }
    CompanyFile *c = file();
    const QDate from = Format::date(s.value(QStringLiteral("from")));
    const QDate to = Format::date(s.value(QStringLiteral("to")));
    const QString name = s.value(QStringLiteral("customerName")).toString();
    const QString path = outboxPath(QStringLiteral("Statement-%1-%2").arg(name, Format::isoDate(to)));
    QString error;
    if (!FormRenderer::writeStatementPdf(*c, customerId, from, to, path, &error)) {
        return Format::failure(error);
    }
    const QString company = c->settings().displayName();
    const MoneyFormat money = moneyFormat();
    QString body = tr("Hello %1,\n\nYour statement from %2 for %3 to %4 is attached. The balance due is %5.\n\n")
                       .arg(name, company, money.locale.toString(from, QLocale::LongFormat),
                            money.locale.toString(to, QLocale::LongFormat), s.value(QStringLiteral("closingText")).toString());
    if (!c->settings().paymentInstructions.isEmpty()) {
        body += c->settings().paymentInstructions + QStringLiteral("\n\n");
    }
    body += tr("Thank you,\n%1").arg(company);
    return mailWithAttachment(customerId, path, s.value(QStringLiteral("email")).toString().trimmed(), name,
                              tr("Statement from %1").arg(company), body);
}

// ---- the company logo ------------------------------------------------------------

QVariantMap BooksService::setCompanyLogo(const QUrl &fileUrl)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const QString path = localPath(fileUrl);
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        return Format::failure(tr("%1 could not be read: %2").arg(path, in.errorString()));
    }
    if (in.size() > kMaxLogoBytes) {
        return Format::failure(tr("The picture is larger than 4 MB; save a smaller copy first."));
    }
    const QByteArray bytes = in.readAll();
    // AGENT-GUARD: the forms decode the logo with QImage::fromData
    // (form_renderer.cpp); accept exactly what will print.
    if (QImage::fromData(bytes).isNull()) {
        return Format::failure(tr("%1 is not a picture QindaBooks can print (PNG or JPEG).").arg(path));
    }
    CompanySettings s = c->settings();
    s.logo = bytes;
    return Format::result(c->setSettings(s));
}

QVariantMap BooksService::removeCompanyLogo()
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    CompanySettings s = c->settings();
    s.logo.clear();
    return Format::result(c->setSettings(s));
}

QString BooksService::companyLogo() const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return QString();
    }
    const QByteArray logo = c->settings().logo;
    if (logo.isEmpty()) {
        return QString();
    }
    QBuffer buffer;
    buffer.setData(logo);
    buffer.open(QIODevice::ReadOnly);
    QByteArray format = QImageReader::imageFormat(&buffer);
    if (format.isEmpty()) {
        format = "png";
    }
    return QStringLiteral("data:image/%1;base64,%2")
        .arg(QString::fromLatin1(format == "jpg" ? QByteArray("jpeg") : format), QString::fromLatin1(logo.toBase64()));
}

} // namespace QindaOffice::Apps::Books
