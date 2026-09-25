// SPDX-License-Identifier: GPL-3.0-or-later
#include "documents.h"

#include "company_file.h"
#include "sql_util.h"
#include "tax.h"

#include <QJsonArray>
#include <QSqlQuery>

namespace QindaOffice::Books {

namespace {

bool isSourceKind(DocKind kind)
{
    return kind == DocKind::ReceivePayment || kind == DocKind::CreditMemo || kind == DocKind::BillPayment
        || kind == DocKind::VendorCredit;
}

const QString kHeaderColumns = QStringLiteral(
    "id, kind, state, number, party_id, date, due_date, terms_id, memo, message, reference, account_id,"
    " to_account_id, discount, tax_rate_id, subtotal, discount_amount, tax_amount, total, source_doc_id,"
    " template_id, txn_id");

BusinessDocument readHeader(const QSqlQuery &q, const QString &currency)
{
    BusinessDocument d;
    d.id = q.value(0).toLongLong();
    d.kind = DocKind(q.value(1).toInt());
    d.state = DocState(q.value(2).toInt());
    d.number = q.value(3).toString();
    d.partyId = q.value(4).toLongLong();
    d.date = Sql::fromIso(q.value(5));
    d.dueDate = Sql::fromIso(q.value(6));
    d.termsId = q.value(7).toLongLong();
    d.memo = q.value(8).toString();
    d.message = q.value(9).toString();
    d.reference = q.value(10).toString();
    d.accountId = q.value(11).toLongLong();
    d.toAccountId = q.value(12).toLongLong();
    d.discount = Discount::fromText(q.value(13).toString(), currency);
    d.taxRateId = q.value(14).toLongLong();
    d.subtotal = Money(q.value(15).toLongLong(), currency);
    d.discountAmount = Money(q.value(16).toLongLong(), currency);
    d.taxAmount = Money(q.value(17).toLongLong(), currency);
    d.total = Money(q.value(18).toLongLong(), currency);
    d.sourceDocId = q.value(19).toLongLong();
    d.templateId = q.value(20).toLongLong();
    d.txnId = q.value(21).toLongLong();
    return d;
}

} // namespace

QString Discount::toText() const
{
    switch (kind) {
    case Kind::None: return QString();
    case Kind::Percent: return percent.toPercentString();
    case Kind::Amount: return amount.toPlainString();
    }
    return QString();
}

Discount Discount::fromText(const QString &text, const QString &currency)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        return {};
    }
    if (t.endsWith(QLatin1Char('%'))) {
        if (const std::optional<Decimal> p = Decimal::parse(t)) {
            return ofPercent(*p);
        }
        return {};
    }
    if (const std::optional<Money> m = Money::parse(t, currency)) {
        return ofAmount(*m);
    }
    return {};
}

Documents::Documents(CompanyFile &file)
    : m_file(file)
{
}

std::optional<BusinessDocument> Documents::load(qint64 id) const
{
    const QSqlDatabase db = m_file.database();
    const QString currency = m_file.currency();
    BusinessDocument doc;
    {
        QSqlQuery q(db);
        Sql::run(q, QStringLiteral("SELECT ") + kHeaderColumns + QStringLiteral(" FROM documents WHERE id = ?"), {id});
        if (!q.next()) {
            return std::nullopt;
        }
        doc = readHeader(q, currency);
    }
    QSqlQuery q(db);
    Sql::run(q, QStringLiteral("SELECT id, item_id, account_id, description, quantity, unit_price, amount, debit,"
                               " credit, taxable, class_id, party_id, link_doc_id FROM document_lines"
                               " WHERE doc_id = ? ORDER BY line_no"),
             {id});
    while (q.next()) {
        DocumentLine line;
        line.id = q.value(0).toLongLong();
        line.itemId = q.value(1).toLongLong();
        line.accountId = q.value(2).toLongLong();
        line.description = q.value(3).toString();
        line.quantity = Decimal::parse(q.value(4).toString()).value_or(Decimal(1, 0));
        line.unitPrice = Money(q.value(5).toLongLong(), currency);
        line.amount = Money(q.value(6).toLongLong(), currency);
        line.debit = Money(q.value(7).toLongLong(), currency);
        line.credit = Money(q.value(8).toLongLong(), currency);
        line.taxable = q.value(9).toBool();
        line.taxableFromItem = false;
        line.classId = q.value(10).toLongLong();
        line.partyId = q.value(11).toLongLong();
        line.linkDocId = q.value(12).toLongLong();
        doc.lines << line;
    }
    return doc;
}

QList<BusinessDocument> Documents::list(const DocumentFilter &filter) const
{
    QStringList where;
    QVariantList binds;
    if (!filter.kinds.isEmpty()) {
        QStringList kinds;
        for (DocKind k : filter.kinds) {
            kinds << QString::number(int(k));
        }
        where << QStringLiteral("kind IN (%1)").arg(kinds.join(QLatin1Char(',')));
    }
    if (filter.partyId != 0) {
        where << QStringLiteral("party_id = ?");
        binds << filter.partyId;
    }
    if (filter.from.isValid()) {
        where << QStringLiteral("date >= ?");
        binds << Sql::isoDate(filter.from);
    }
    if (filter.to.isValid()) {
        where << QStringLiteral("date <= ?");
        binds << Sql::isoDate(filter.to);
    }
    if (!filter.includeVoid) {
        where << QStringLiteral("state <> %1").arg(int(DocState::Void));
    }
    if (!filter.includeDrafts) {
        where << QStringLiteral("state <> %1").arg(int(DocState::Draft));
    }
    QList<qint64> ids;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT id FROM documents") +
                        (where.isEmpty() ? QString() : QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND ")))
                        + QStringLiteral(" ORDER BY date, id"),
                 binds);
        while (q.next()) {
            ids << q.value(0).toLongLong();
        }
    }
    QList<BusinessDocument> out;
    for (qint64 id : ids) {
        if (std::optional<BusinessDocument> d = load(id)) {
            out << *d;
        }
    }
    return out;
}

void Documents::fillFromItems(BusinessDocument &doc, bool purchase) const
{
    const QString currency = m_file.currency();
    for (DocumentLine &line : doc.lines) {
        if (line.itemId == 0) {
            continue;
        }
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT name, description, price, cost, income_account_id, expense_account_id,"
                                   " taxable FROM items WHERE id = ?"),
                 {line.itemId});
        if (!q.next()) {
            continue;
        }
        if (line.description.isEmpty()) {
            line.description = q.value(1).toString().isEmpty() ? q.value(0).toString() : q.value(1).toString();
        }
        if (line.unitPrice.isZero() && line.amount.isZero()) {
            line.unitPrice = Money(q.value(purchase ? 3 : 2).toLongLong(), currency);
        }
        if (line.accountId == 0) {
            line.accountId = q.value(purchase ? 5 : 4).toLongLong();
        }
        if (line.taxableFromItem) {
            line.taxable = !purchase && q.value(6).toBool();
            line.taxableFromItem = false;
        }
    }
}

DocumentTotals Documents::computeTotals(const BusinessDocument &doc) const
{
    const QString currency = m_file.currency();
    DocumentTotals t;
    t.subtotal = Money::zero(currency);
    QList<qint64> weights;
    QList<bool> taxable;
    for (const DocumentLine &line : doc.lines) {
        const Money amount = line.unitPrice.isZero() ? line.amount
                                                     : line.unitPrice.multiplied(line.quantity, RoundingMode::HalfUp);
        t.lineAmounts << Money(amount.minor(), currency);
        t.subtotal += Money(amount.minor(), currency);
        weights << (amount.minor() > 0 ? amount.minor() : 0);
        taxable << line.taxable;
    }
    switch (doc.discount.kind) {
    case Discount::Kind::None:
        t.discount = Money::zero(currency);
        break;
    case Discount::Kind::Percent:
        t.discount = t.subtotal.multiplied(doc.discount.percent, RoundingMode::HalfUp);
        break;
    case Discount::Kind::Amount:
        t.discount = Money(qMin(doc.discount.amount.minor(), qMax<qint64>(t.subtotal.minor(), 0)), currency);
        break;
    }
    // The discount is shared across the lines in proportion to their
    // amounts; the taxable base is the taxable lines net of their share.
    const QList<Money> shares = t.discount.allocate(weights);
    t.taxableBase = Money::zero(currency);
    for (qsizetype i = 0; i < t.lineAmounts.size(); ++i) {
        if (taxable.at(i)) {
            t.taxableBase += t.lineAmounts.at(i) - (i < shares.size() ? shares.at(i) : Money::zero(currency));
        }
    }
    t.tax = Money::zero(currency);
    if (doc.taxRateId != 0) {
        t.taxes = SalesTax(m_file).compute(doc.taxRateId, t.taxableBase);
        for (const TaxShare &share : std::as_const(t.taxes)) {
            t.tax += share.amount;
        }
    }
    t.total = t.subtotal - t.discount + t.tax;
    return t;
}

DocumentTotals Documents::finalize(BusinessDocument &doc) const
{
    const DocumentTotals totals = computeTotals(doc);
    for (qsizetype i = 0; i < doc.lines.size() && i < totals.lineAmounts.size(); ++i) {
        doc.lines[i].amount = totals.lineAmounts.at(i);
    }
    doc.subtotal = totals.subtotal;
    doc.discountAmount = totals.discount;
    doc.taxAmount = totals.tax;
    doc.total = totals.total;
    return totals;
}

QDate Documents::dueDateFor(const QDate &date, qint64 termsId) const
{
    if (termsId == 0) {
        return date;
    }
    return date.addDays(Sql::scalarInt(m_file.database(), QStringLiteral("SELECT due_days FROM terms WHERE id = ?"),
                                       {termsId}));
}

Money Documents::appliedTo(qint64 targetDocId, const QDate &asOf) const
{
    return m_file.money(Sql::scalarInt(
        m_file.database(),
        QStringLiteral("SELECT COALESCE(SUM(amount), 0) FROM applications WHERE target_doc_id = ? AND voided = 0"
                       " AND (? = '' OR date <= ?)"),
        {targetDocId, Sql::isoDate(asOf), Sql::isoDate(asOf)}));
}

Money Documents::appliedFrom(qint64 sourceDocId, const QDate &asOf) const
{
    return m_file.money(Sql::scalarInt(
        m_file.database(),
        QStringLiteral("SELECT COALESCE(SUM(amount), 0) FROM applications WHERE source_doc_id = ? AND voided = 0"
                       " AND (? = '' OR date <= ?)"),
        {sourceDocId, Sql::isoDate(asOf), Sql::isoDate(asOf)}));
}

Money Documents::openBalance(qint64 docId, const QDate &asOf) const
{
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT kind, state, total FROM documents WHERE id = ?"), {docId});
    if (!q.next()) {
        return m_file.money(0);
    }
    const DocKind kind = DocKind(q.value(0).toInt());
    const DocState state = DocState(q.value(1).toInt());
    const Money total = m_file.money(q.value(2).toLongLong());
    if (state == DocState::Void || state == DocState::Draft) {
        return m_file.money(0);
    }
    return total - (isSourceKind(kind) ? appliedFrom(docId, asOf) : appliedTo(docId, asOf));
}

QList<Application> Documents::applicationsFor(qint64 docId) const
{
    QList<Application> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT id, source_doc_id, target_doc_id, amount, date, voided FROM applications"
                               " WHERE source_doc_id = ? OR target_doc_id = ? ORDER BY date, id"),
             {docId, docId});
    while (q.next()) {
        out << Application{q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toLongLong(),
                           m_file.money(q.value(3).toLongLong()), Sql::fromIso(q.value(4)), q.value(5).toBool()};
    }
    return out;
}

DocumentStatus Documents::status(const BusinessDocument &doc, const QDate &today) const
{
    if (doc.state == DocState::Void) {
        return DocumentStatus::Void;
    }
    if (doc.state == DocState::Draft) {
        return DocumentStatus::Draft;
    }
    if (doc.kind == DocKind::Estimate) {
        return doc.state == DocState::Closed ? DocumentStatus::Closed : DocumentStatus::Pending;
    }
    const Money open = openBalance(doc.id);
    if (open.isZero() || open.isNegative()) {
        return DocumentStatus::Paid;
    }
    const QDate due = doc.dueDate.isValid() ? doc.dueDate : doc.date;
    if ((doc.kind == DocKind::Invoice || doc.kind == DocKind::Bill) && today.isValid() && due < today) {
        return DocumentStatus::Overdue;
    }
    return open < doc.total ? DocumentStatus::Partial : DocumentStatus::Open;
}

QJsonObject Documents::toJson(const BusinessDocument &doc) const
{
    QJsonArray lines;
    for (const DocumentLine &l : doc.lines) {
        lines.append(QJsonObject{{QStringLiteral("item"), l.itemId},
                                 {QStringLiteral("account"), l.accountId},
                                 {QStringLiteral("description"), l.description},
                                 {QStringLiteral("quantity"), l.quantity.toString()},
                                 {QStringLiteral("unit_price"), l.unitPrice.toPlainString()},
                                 {QStringLiteral("amount"), l.amount.toPlainString()},
                                 {QStringLiteral("debit"), l.debit.toPlainString()},
                                 {QStringLiteral("credit"), l.credit.toPlainString()},
                                 {QStringLiteral("taxable"), l.taxable},
                                 {QStringLiteral("class"), l.classId},
                                 {QStringLiteral("party"), l.partyId}});
    }
    return QJsonObject{{QStringLiteral("id"), doc.id},
                       {QStringLiteral("kind"), docKindName(doc.kind)},
                       {QStringLiteral("state"), int(doc.state)},
                       {QStringLiteral("number"), doc.number},
                       {QStringLiteral("party"), doc.partyId},
                       {QStringLiteral("date"), Sql::isoDate(doc.date)},
                       {QStringLiteral("due"), Sql::isoDate(doc.dueDate)},
                       {QStringLiteral("memo"), doc.memo},
                       {QStringLiteral("account"), doc.accountId},
                       {QStringLiteral("discount"), doc.discount.toText()},
                       {QStringLiteral("tax_rate"), doc.taxRateId},
                       {QStringLiteral("total"), doc.total.toPlainString()},
                       {QStringLiteral("lines"), lines}};
}

Result Documents::write(BusinessDocument &doc)
{
    const QSqlDatabase db = m_file.database();
    const QVariantList header{int(doc.kind),
                              int(doc.state),
                              doc.number,
                              Sql::idOrNull(doc.partyId),
                              Sql::isoDate(doc.date),
                              doc.dueDate.isValid() ? QVariant(Sql::isoDate(doc.dueDate)) : QVariant(),
                              Sql::idOrNull(doc.termsId),
                              doc.memo,
                              doc.message,
                              doc.reference,
                              Sql::idOrNull(doc.accountId),
                              Sql::idOrNull(doc.toAccountId),
                              doc.discount.toText(),
                              Sql::idOrNull(doc.taxRateId),
                              doc.subtotal.minor(),
                              doc.discountAmount.minor(),
                              doc.taxAmount.minor(),
                              doc.total.minor(),
                              Sql::idOrNull(doc.sourceDocId),
                              Sql::idOrNull(doc.templateId),
                              CompanyFile::nowStamp()};
    QString error;
    if (doc.id == 0) {
        QVariantList binds = header;
        binds << CompanyFile::nowStamp();
        doc.id = Sql::insert(
            db,
            QStringLiteral("INSERT INTO documents(kind, state, number, party_id, date, due_date, terms_id, memo,"
                           " message, reference, account_id, to_account_id, discount, tax_rate_id, subtotal,"
                           " discount_amount, tax_amount, total, source_doc_id, template_id, updated_at,"
                           " created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"),
            binds, &error);
        if (doc.id == 0) {
            return Result::failure(ErrorCode::Storage, error);
        }
    } else {
        QVariantList binds = header;
        binds << doc.id;
        if (!Sql::exec(db,
                       QStringLiteral("UPDATE documents SET kind = ?, state = ?, number = ?, party_id = ?, date = ?,"
                                      " due_date = ?, terms_id = ?, memo = ?, message = ?, reference = ?,"
                                      " account_id = ?, to_account_id = ?, discount = ?, tax_rate_id = ?,"
                                      " subtotal = ?, discount_amount = ?, tax_amount = ?, total = ?,"
                                      " source_doc_id = ?, template_id = ?, updated_at = ? WHERE id = ?"),
                       binds, &error)
            || !Sql::exec(db, QStringLiteral("DELETE FROM document_lines WHERE doc_id = ?"), {doc.id}, &error)) {
            return Result::failure(ErrorCode::Storage, error);
        }
    }
    int lineNo = 0;
    for (DocumentLine &line : doc.lines) {
        line.id = Sql::insert(
            db,
            QStringLiteral("INSERT INTO document_lines(doc_id, line_no, item_id, account_id, description, quantity,"
                           " unit_price, amount, debit, credit, taxable, class_id, party_id, link_doc_id)"
                           " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"),
            {doc.id, ++lineNo, Sql::idOrNull(line.itemId), Sql::idOrNull(line.accountId), line.description,
             line.quantity.toString(), line.unitPrice.minor(), line.amount.minor(), line.debit.minor(),
             line.credit.minor(), line.taxable ? 1 : 0, Sql::idOrNull(line.classId), Sql::idOrNull(line.partyId),
             Sql::idOrNull(line.linkDocId)},
            &error);
        if (line.id == 0) {
            return Result::failure(ErrorCode::Storage, error);
        }
    }
    return Result::success(doc.id);
}

Result Documents::setState(qint64 docId, DocState state)
{
    QString error;
    if (!Sql::exec(m_file.database(), QStringLiteral("UPDATE documents SET state = ?, updated_at = ? WHERE id = ?"),
                   {int(state), CompanyFile::nowStamp(), docId}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    return Result::success(docId);
}

Result Documents::addApplication(qint64 sourceDocId, qint64 targetDocId, const Money &amount, const QDate &date)
{
    if (!amount.isPositive()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("an applied amount must be positive"));
    }
    QString error;
    const qint64 id = Sql::insert(m_file.database(),
                                  QStringLiteral("INSERT INTO applications(source_doc_id, target_doc_id, amount, date,"
                                                 " created_at) VALUES(?,?,?,?,?)"),
                                  {sourceDocId, targetDocId, amount.minor(), Sql::isoDate(date),
                                   CompanyFile::nowStamp()},
                                  &error);
    return id == 0 ? Result::failure(ErrorCode::Storage, error) : Result::success(id);
}

Result Documents::voidApplicationsOf(qint64 docId)
{
    QString error;
    if (!Sql::exec(m_file.database(),
                   QStringLiteral("UPDATE applications SET voided = 1 WHERE source_doc_id = ? OR target_doc_id = ?"),
                   {docId, docId}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    return Result::success(docId);
}

} // namespace QindaOffice::Books
