// SPDX-License-Identifier: GPL-3.0-or-later
// Documents: the engine-internal write path (documents.h) — header and lines
// in one go, the persisted state, and applications. Callers hold a
// SqlTransaction. Split from documents.cpp at M68 (the 500-line rule).
#include "documents.h"

#include "company_file.h"
#include "sql_util.h"

#include <QSet>
#include <QSqlQuery>

namespace QindaOffice::Books {

namespace {

// AGENT-CONTRACT (documents.h, M68): which kind may carry which link.
qint64 poLinkFor(DocKind kind, qint64 id)
{
    return kind == DocKind::Bill ? id : 0;
}

qint64 timeLinkFor(DocKind kind, qint64 id)
{
    return kind == DocKind::Invoice ? id : 0;
}

bool closedFor(DocKind kind, bool closed)
{
    return kind == DocKind::PurchaseOrder && closed;
}

} // namespace

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
                              doc.shipTo.isEmpty() ? QVariant() : QVariant(doc.shipTo),
                              doc.toPrint ? 1 : 0,
                              CompanyFile::nowStamp()};
    QString error;
    if (doc.id == 0) {
        QVariantList binds = header;
        binds << CompanyFile::nowStamp();
        doc.id = Sql::insert(
            db,
            QStringLiteral("INSERT INTO documents(kind, state, number, party_id, date, due_date, terms_id, memo,"
                           " message, reference, account_id, to_account_id, discount, tax_rate_id, subtotal,"
                           " discount_amount, tax_amount, total, source_doc_id, template_id, ship_to, to_print,"
                           " updated_at, created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"),
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
                                      " source_doc_id = ?, template_id = ?, ship_to = ?, to_print = ?,"
                                      " updated_at = ? WHERE id = ?"),
                       binds, &error)) {
            return Result::failure(ErrorCode::Storage, error);
        }
    }
    // The lines this document already had keep their ids (documents.h).
    QSet<qint64> own;
    {
        QSqlQuery q(db);
        Sql::run(q, QStringLiteral("SELECT id FROM document_lines WHERE doc_id = ?"), {doc.id});
        while (q.next()) {
            own.insert(q.value(0).toLongLong());
        }
    }
    if (!own.isEmpty()
        && !Sql::exec(db, QStringLiteral("DELETE FROM document_lines WHERE doc_id = ?"), {doc.id}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    // AGENT-GUARD: kept ids go back first. A new line takes max(rowid) + 1,
    // which could otherwise be a kept id still waiting to be re-inserted.
    const QString columns = QStringLiteral(
        "doc_id, line_no, item_id, account_id, description, quantity, unit_price, amount, debit, credit, taxable,"
        " class_id, party_id, link_doc_id, po_line_id, time_entry_id, closed");
    QList<bool> keep;
    for (DocumentLine &line : doc.lines) {
        keep << (line.id != 0 && own.remove(line.id)); // one line, one id
        line.poLineId = poLinkFor(doc.kind, line.poLineId);
        line.timeEntryId = timeLinkFor(doc.kind, line.timeEntryId);
        line.closed = closedFor(doc.kind, line.closed);
    }
    for (const bool keptPass : {true, false}) {
        for (qsizetype i = 0; i < doc.lines.size(); ++i) {
            if (keep.at(i) != keptPass) {
                continue;
            }
            DocumentLine &line = doc.lines[i];
            QVariantList values{doc.id, int(i) + 1, Sql::idOrNull(line.itemId), Sql::idOrNull(line.accountId),
                                line.description, line.quantity.toString(), line.unitPrice.minor(),
                                line.amount.minor(), line.debit.minor(), line.credit.minor(), line.taxable ? 1 : 0,
                                Sql::idOrNull(line.classId), Sql::idOrNull(line.partyId),
                                Sql::idOrNull(line.linkDocId), Sql::idOrNull(line.poLineId),
                                Sql::idOrNull(line.timeEntryId), line.closed ? 1 : 0};
            if (keptPass) {
                values.prepend(line.id);
                if (!Sql::exec(db,
                               QStringLiteral("INSERT INTO document_lines(id, %1) VALUES(%2)")
                                   .arg(columns, QStringLiteral("?,").repeated(17) + QLatin1Char('?')),
                               values, &error)) {
                    return Result::failure(ErrorCode::Storage, error);
                }
                continue;
            }
            line.id = Sql::insert(db,
                                  QStringLiteral("INSERT INTO document_lines(%1) VALUES(%2)")
                                      .arg(columns, QStringLiteral("?,").repeated(16) + QLatin1Char('?')),
                                  values, &error);
            if (line.id == 0) {
                return Result::failure(ErrorCode::Storage, error);
            }
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
