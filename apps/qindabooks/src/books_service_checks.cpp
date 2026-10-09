// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: the "to be printed" check queue and per-printer check
// alignment (M68, D-077; check_queue.h, check_alignment.h). Printing itself
// goes through BooksHost's "checks" and "alignment" print sources, so the
// paper is the same painter as every form.
#include "books_service.h"

#include "check_print.h"
#include "check_queue.h"
#include "payroll_ledger.h"
#include "company_file.h"
#include "page_output.h"

#include <QPrinterInfo>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

QVariantMap alignmentMap(const CheckAlignment &a)
{
    return {{QStringLiteral("name"), a.printer},
            {QStringLiteral("printer"), a.printer},
            {QStringLiteral("x"), a.x},
            {QStringLiteral("y"), a.y},
            {QStringLiteral("unit"), a.unitKey()},
            {QStringLiteral("description"), a.describe()}};
}

QList<qint64> idsOf(const QVariantList &list)
{
    QList<qint64> ids;
    for (const QVariant &v : list) {
        ids << Format::id(v);
    }
    return ids;
}

// The payee a queued check is written to: its party, or (M71) the
// employee a paycheck pays.
QString payeeOf(CompanyFile &file, const QueuedCheck &q, const QString &partyName)
{
    if (q.kind == DocKind::Paycheck) {
        if (const std::optional<PaycheckRecord> r = PayrollLedger(file).paycheck(q.docId)) {
            return r->employeeName;
        }
    }
    return partyName;
}

} // namespace

QVariantList BooksService::checkQueue(qint64 bankAccountId) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    for (const QueuedCheck &q : CheckQueue(*c).queue(bankAccountId)) {
        out << QVariantMap{{QStringLiteral("id"), q.docId},
                           {QStringLiteral("kind"), Format::kindKey(q.kind)},
                           {QStringLiteral("kindLabel"), Format::kindLabel(q.kind)},
                           {QStringLiteral("date"), Format::isoDate(q.date)},
                           {QStringLiteral("accountId"), q.accountId},
                           {QStringLiteral("payeeName"), payeeOf(*c, q, nameOfParty(q.payeeId))},
                           {QStringLiteral("amount"), q.amount.minor()},
                           {QStringLiteral("amountText"), money.text(q.amount.minor())},
                           {QStringLiteral("number"), q.number}};
    }
    return out;
}

qint64 BooksService::nextCheckNumber(qint64 bankAccountId) const
{
    return file() != nullptr ? CheckQueue(*file()).nextNumber(bankAccountId) : 0;
}

QVariantMap BooksService::setToPrint(qint64 id, bool toPrint)
{
    return file() != nullptr ? Format::result(CheckQueue(*file()).setToPrint(id, toPrint)) : noCompany();
}

QVariantMap BooksService::assignCheckNumbers(qint64 bankAccountId, const QVariantList &ids, const QString &firstNumber)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    bool numeric = false;
    const qint64 first = firstNumber.trimmed().toLongLong(&numeric);
    if (!numeric || first <= 0) {
        return Format::failure(tr("\"%1\" is not a check number.").arg(firstNumber));
    }
    QList<QueuedCheck> numbered;
    const Result r = CheckQueue(*c).assignNumbers(bankAccountId, idsOf(ids), first, &numbered);
    QVariantMap out = Format::result(r);
    const MoneyFormat money = moneyFormat();
    QVariantList checks;
    for (const QueuedCheck &q : numbered) {
        checks << QVariantMap{{QStringLiteral("id"), q.docId},
                              {QStringLiteral("number"), q.number},
                              {QStringLiteral("payeeName"), payeeOf(*c, q, nameOfParty(q.payeeId))},
                              {QStringLiteral("amountText"), money.text(q.amount.minor())}};
    }
    out.insert(QStringLiteral("checks"), checks);
    return out;
}

QVariantMap BooksService::confirmCheckPrint(const QVariantList &misprintedIds)
{
    return file() != nullptr ? Format::result(CheckQueue(*file()).confirmPrinted(idsOf(misprintedIds), todayDate()))
                             : noCompany();
}

QVariantMap BooksService::takeBackChecks(qint64 bankAccountId, const QVariantList &ids)
{
    return file() != nullptr ? Format::result(CheckQueue(*file()).takeBack(bankAccountId, idsOf(ids))) : noCompany();
}

QVariantList BooksService::checkPrinters() const
{
    const CheckAlignmentStore store;
    QStringList names = QPrinterInfo::availablePrinterNames();
    for (const QString &stored : store.printers()) {
        if (!names.contains(stored)) {
            names << stored;
        }
    }
    QVariantList out;
    for (const QString &name : std::as_const(names)) {
        out << alignmentMap(store.alignment(name));
    }
    return out;
}

QVariantMap BooksService::checkAlignment(const QString &printer) const
{
    return alignmentMap(CheckAlignmentStore().alignment(printer));
}

QVariantMap BooksService::saveCheckAlignment(const QVariantMap &map)
{
    const QString printer = map.value(QStringLiteral("printer")).toString().trimmed();
    if (printer.isEmpty()) {
        return Format::failure(tr("Choose the printer the checks print on."));
    }
    QString error;
    const std::optional<CheckAlignment> a =
        CheckAlignment::make(printer, map.value(QStringLiteral("x")).toString(), map.value(QStringLiteral("y")).toString(),
                             map.value(QStringLiteral("unit")).toString(), &error);
    if (!a || !CheckAlignmentStore().save(*a, &error)) {
        return Format::failure(error);
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("description"), a->describe());
    return out;
}

QVariantMap BooksService::exportAlignmentTestPdf(const QString &printer, const QUrl &fileUrl)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const CheckAlignment a = CheckAlignmentStore().alignment(printer);
    const QString path = localPath(fileUrl);
    QString error;
    if (!writePdf(path,
                  CheckRenderer::alignmentTestPainter(a.offset(), a.printer, a.describe(),
                                                      CheckRenderer::sampleCheck(*c, todayDate())),
                  QPageSize(QPageSize::Letter), QPageLayout::Portrait, &error)) {
        return Format::failure(error);
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("path"), path);
    return out;
}

} // namespace QindaOffice::Apps::Books
