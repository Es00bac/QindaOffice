// SPDX-License-Identifier: GPL-3.0-or-later
#include "schema_migrations.h"

#include "sql_util.h"

#include <QStringList>

namespace QindaOffice::Books::Schema {

namespace {

// ---- version 1: the ledger ------------------------------------------------
//
// AGENT-NOTE: the posting model (docs/books.md "Posting rules"):
//  * documents / document_lines — the business document the user edits
//    (invoice, bill, journal entry …). Mutable under audit.
//  * transactions / journal_lines — the posting a non-draft document made.
//    Immutable: the triggers below refuse UPDATE of any amount, account,
//    date or party column and every DELETE. An edit voids the current
//    transaction (voided = 1, lines keep their numbers but are excluded
//    from every balance) and posts a new one, in one SQL transaction.
//  * journal_lines.date/kind/voided duplicate the transaction header so
//    every report aggregates one indexed table (the 50k-line gate).
const char *const kVersion1[] = {
    "CREATE TABLE settings (key TEXT PRIMARY KEY, value BLOB)",
    "CREATE TABLE accounts ("
    " id INTEGER PRIMARY KEY, number TEXT, name TEXT NOT NULL, type INTEGER NOT NULL,"
    " parent_id INTEGER REFERENCES accounts(id), system_role TEXT UNIQUE,"
    " functional_group TEXT, description TEXT, active INTEGER NOT NULL DEFAULT 1,"
    " created_at TEXT NOT NULL)",
    "CREATE TABLE terms (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE,"
    " due_days INTEGER NOT NULL DEFAULT 0, active INTEGER NOT NULL DEFAULT 1)",
    "CREATE TABLE classes (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE,"
    " parent_id INTEGER REFERENCES classes(id), active INTEGER NOT NULL DEFAULT 1)",
    "CREATE TABLE parties ("
    " id INTEGER PRIMARY KEY, kind INTEGER NOT NULL, display_name TEXT NOT NULL,"
    " company_name TEXT, contact_name TEXT, email TEXT, phone TEXT,"
    " billing_address TEXT, shipping_address TEXT, tax_id TEXT,"
    " terms_id INTEGER REFERENCES terms(id), is_1099 INTEGER NOT NULL DEFAULT 0,"
    " default_account_id INTEGER REFERENCES accounts(id), tax_rate_id INTEGER,"
    " notes TEXT, active INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL)",
    "CREATE INDEX parties_kind ON parties(kind, display_name)",
    "CREATE TABLE tax_agencies (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE,"
    " registration_id TEXT, rounding INTEGER NOT NULL DEFAULT 0,"
    " active INTEGER NOT NULL DEFAULT 1)",
    "CREATE TABLE tax_components (id INTEGER PRIMARY KEY,"
    " agency_id INTEGER NOT NULL REFERENCES tax_agencies(id), name TEXT NOT NULL,"
    " rate TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 1)",
    "CREATE TABLE tax_rates (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE,"
    " active INTEGER NOT NULL DEFAULT 1)",
    "CREATE TABLE tax_rate_components ("
    " rate_id INTEGER NOT NULL REFERENCES tax_rates(id),"
    " component_id INTEGER NOT NULL REFERENCES tax_components(id),"
    " PRIMARY KEY (rate_id, component_id))",
    "CREATE TABLE items ("
    " id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, kind INTEGER NOT NULL DEFAULT 1,"
    " description TEXT, sku TEXT, price INTEGER NOT NULL DEFAULT 0,"
    " cost INTEGER NOT NULL DEFAULT 0,"
    " income_account_id INTEGER REFERENCES accounts(id),"
    " expense_account_id INTEGER REFERENCES accounts(id),"
    " taxable INTEGER NOT NULL DEFAULT 0, active INTEGER NOT NULL DEFAULT 1)",
    "CREATE TABLE documents ("
    " id INTEGER PRIMARY KEY, kind INTEGER NOT NULL, state INTEGER NOT NULL,"
    " number TEXT, party_id INTEGER REFERENCES parties(id), date TEXT NOT NULL,"
    " due_date TEXT, terms_id INTEGER REFERENCES terms(id), memo TEXT, message TEXT,"
    " reference TEXT, account_id INTEGER REFERENCES accounts(id),"
    " to_account_id INTEGER REFERENCES accounts(id), discount TEXT,"
    " tax_rate_id INTEGER REFERENCES tax_rates(id),"
    " subtotal INTEGER NOT NULL DEFAULT 0, discount_amount INTEGER NOT NULL DEFAULT 0,"
    " tax_amount INTEGER NOT NULL DEFAULT 0, total INTEGER NOT NULL DEFAULT 0,"
    " source_doc_id INTEGER REFERENCES documents(id), template_id INTEGER,"
    " txn_id INTEGER, created_at TEXT NOT NULL, updated_at TEXT NOT NULL)",
    "CREATE INDEX documents_party ON documents(party_id, kind)",
    "CREATE INDEX documents_kind_date ON documents(kind, date)",
    "CREATE TABLE document_lines ("
    " id INTEGER PRIMARY KEY,"
    " doc_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
    " line_no INTEGER NOT NULL, item_id INTEGER REFERENCES items(id),"
    " account_id INTEGER REFERENCES accounts(id), description TEXT, quantity TEXT,"
    " unit_price INTEGER NOT NULL DEFAULT 0, amount INTEGER NOT NULL DEFAULT 0,"
    " debit INTEGER NOT NULL DEFAULT 0, credit INTEGER NOT NULL DEFAULT 0,"
    " taxable INTEGER NOT NULL DEFAULT 0, class_id INTEGER REFERENCES classes(id),"
    " party_id INTEGER REFERENCES parties(id),"
    " link_doc_id INTEGER REFERENCES documents(id))",
    "CREATE INDEX document_lines_doc ON document_lines(doc_id, line_no)",
    "CREATE TABLE reconciliations ("
    " id INTEGER PRIMARY KEY, account_id INTEGER NOT NULL REFERENCES accounts(id),"
    " statement_date TEXT NOT NULL, ending_balance INTEGER NOT NULL,"
    " beginning_balance INTEGER NOT NULL, state INTEGER NOT NULL DEFAULT 0,"
    " started_at TEXT NOT NULL, finished_at TEXT)",
    "CREATE TABLE transactions ("
    " id INTEGER PRIMARY KEY, doc_id INTEGER NOT NULL REFERENCES documents(id),"
    " kind INTEGER NOT NULL, date TEXT NOT NULL, number TEXT, memo TEXT,"
    " voided INTEGER NOT NULL DEFAULT 0, void_reason TEXT,"
    " replaced_by INTEGER REFERENCES transactions(id), lock_override_reason TEXT,"
    " created_at TEXT NOT NULL, created_by TEXT)",
    "CREATE INDEX transactions_doc ON transactions(doc_id)",
    "CREATE TABLE journal_lines ("
    " id INTEGER PRIMARY KEY, txn_id INTEGER NOT NULL REFERENCES transactions(id),"
    " line_no INTEGER NOT NULL, account_id INTEGER NOT NULL REFERENCES accounts(id),"
    " date TEXT NOT NULL, kind INTEGER NOT NULL,"
    " debit INTEGER NOT NULL DEFAULT 0, credit INTEGER NOT NULL DEFAULT 0,"
    " party_id INTEGER REFERENCES parties(id), class_id INTEGER REFERENCES classes(id),"
    " item_id INTEGER REFERENCES items(id), tax_component_id INTEGER, tax_agency_id INTEGER,"
    " memo TEXT, voided INTEGER NOT NULL DEFAULT 0, cleared INTEGER NOT NULL DEFAULT 0,"
    " reconciliation_id INTEGER REFERENCES reconciliations(id),"
    " CHECK (debit >= 0 AND credit >= 0 AND (debit = 0 OR credit = 0)))",
    "CREATE INDEX journal_lines_account_date ON journal_lines(account_id, date)",
    "CREATE INDEX journal_lines_date ON journal_lines(date)",
    "CREATE INDEX journal_lines_txn ON journal_lines(txn_id)",
    "CREATE TABLE applications ("
    " id INTEGER PRIMARY KEY, source_doc_id INTEGER NOT NULL REFERENCES documents(id),"
    " target_doc_id INTEGER NOT NULL REFERENCES documents(id),"
    " amount INTEGER NOT NULL CHECK (amount > 0), date TEXT NOT NULL,"
    " voided INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL)",
    "CREATE INDEX applications_target ON applications(target_doc_id)",
    "CREATE INDEX applications_source ON applications(source_doc_id)",
    "CREATE TABLE recurring_templates ("
    " id INTEGER PRIMARY KEY, name TEXT NOT NULL,"
    " doc_id INTEGER NOT NULL REFERENCES documents(id), frequency INTEGER NOT NULL,"
    " interval INTEGER NOT NULL DEFAULT 1, day_of_month INTEGER NOT NULL DEFAULT 0,"
    " start_date TEXT NOT NULL, end_date TEXT, max_occurrences INTEGER NOT NULL DEFAULT 0,"
    " generated INTEGER NOT NULL DEFAULT 0, post_open INTEGER NOT NULL DEFAULT 1,"
    " active INTEGER NOT NULL DEFAULT 1)",
    "CREATE TABLE bank_feed ("
    " id INTEGER PRIMARY KEY, account_id INTEGER NOT NULL REFERENCES accounts(id),"
    " date TEXT NOT NULL, amount INTEGER NOT NULL, payee TEXT, memo TEXT, fitid TEXT,"
    " check_number TEXT, dedup_key TEXT NOT NULL, status INTEGER NOT NULL DEFAULT 0,"
    " suggested_account_id INTEGER REFERENCES accounts(id),"
    " suggested_party_id INTEGER REFERENCES parties(id),"
    " suggested_class_id INTEGER REFERENCES classes(id), rule_id INTEGER,"
    " doc_id INTEGER REFERENCES documents(id), batch TEXT, imported_at TEXT NOT NULL,"
    " UNIQUE (account_id, dedup_key))",
    "CREATE TABLE bank_rules ("
    " id INTEGER PRIMARY KEY, name TEXT NOT NULL, priority INTEGER NOT NULL DEFAULT 0,"
    " payee_contains TEXT, amount_min INTEGER, amount_max INTEGER,"
    " direction INTEGER NOT NULL DEFAULT 0, bank_account_id INTEGER,"
    " account_id INTEGER NOT NULL REFERENCES accounts(id),"
    " party_id INTEGER REFERENCES parties(id), class_id INTEGER REFERENCES classes(id),"
    " memo TEXT, active INTEGER NOT NULL DEFAULT 1)",
    "CREATE TABLE reconciliation_marks ("
    " reconciliation_id INTEGER NOT NULL REFERENCES reconciliations(id) ON DELETE CASCADE,"
    " line_id INTEGER NOT NULL REFERENCES journal_lines(id),"
    " PRIMARY KEY (reconciliation_id, line_id))",
    "CREATE TABLE audit_log ("
    " id INTEGER PRIMARY KEY, at TEXT NOT NULL, user TEXT, action TEXT NOT NULL,"
    " entity TEXT, entity_id INTEGER, before_json TEXT, after_json TEXT, reason TEXT)",
    // ---- immutability, enforced by the file itself ----
    "CREATE TRIGGER journal_lines_immutable BEFORE UPDATE OF"
    " txn_id, line_no, account_id, date, kind, debit, credit, party_id, class_id, item_id,"
    " tax_component_id, tax_agency_id, memo ON journal_lines"
    " BEGIN SELECT RAISE(ABORT, 'posted journal lines are immutable'); END",
    "CREATE TRIGGER journal_lines_no_delete BEFORE DELETE ON journal_lines"
    " BEGIN SELECT RAISE(ABORT, 'posted journal lines cannot be deleted; void instead'); END",
    "CREATE TRIGGER transactions_immutable BEFORE UPDATE OF doc_id, kind, date, number, memo"
    " ON transactions BEGIN SELECT RAISE(ABORT, 'posted transactions are immutable'); END",
    "CREATE TRIGGER transactions_no_unvoid BEFORE UPDATE OF voided ON transactions"
    " WHEN OLD.voided = 1 BEGIN SELECT RAISE(ABORT, 'a void cannot be undone'); END",
    "CREATE TRIGGER transactions_no_delete BEFORE DELETE ON transactions"
    " BEGIN SELECT RAISE(ABORT, 'posted transactions cannot be deleted; void instead'); END",
    "CREATE TRIGGER audit_log_append_only_u BEFORE UPDATE ON audit_log"
    " BEGIN SELECT RAISE(ABORT, 'the audit log is append-only'); END",
    "CREATE TRIGGER audit_log_append_only_d BEFORE DELETE ON audit_log"
    " BEGIN SELECT RAISE(ABORT, 'the audit log is append-only'); END",
};

// ---- version 2: budgets ---------------------------------------------------
// class_id 0 = the whole company (NOT NULL so the UNIQUE key holds).
const char *const kVersion2[] = {
    "CREATE TABLE budgets ("
    " id INTEGER PRIMARY KEY, account_id INTEGER NOT NULL REFERENCES accounts(id),"
    " class_id INTEGER NOT NULL DEFAULT 0, year INTEGER NOT NULL,"
    " month INTEGER NOT NULL CHECK (month BETWEEN 1 AND 12), amount INTEGER NOT NULL,"
    " UNIQUE (account_id, class_id, year, month))",
    "CREATE INDEX journal_lines_party ON journal_lines(party_id)",
};

// ---- version 3: bank and payment connections (M48, D-048) -------------------
// AGENT-GUARD: nothing here ever holds a credential. API tokens live only
// in the desktop keyring (books_connect/src/connect_secrets.h, key
// `<company_uuid>/<provider>`); `settings` and `cursor` are JSON of
// account links, flags and sync positions. Connections::save() refuses a
// value that looks like a token.
// payment_links: every online payment link made for a document (the
// newest one whose amount equals the open balance is the current one).
const char *const kVersion3[] = {
    "CREATE TABLE connections ("
    " provider TEXT PRIMARY KEY CHECK (provider IN ('mercury', 'stripe')),"
    " status TEXT NOT NULL DEFAULT 'connected', settings TEXT NOT NULL DEFAULT '{}',"
    " cursor TEXT NOT NULL DEFAULT '{}', last_sync TEXT, last_error TEXT,"
    " created_at TEXT NOT NULL, updated_at TEXT NOT NULL)",
    "CREATE TABLE payment_links ("
    " id INTEGER PRIMARY KEY, doc_id INTEGER NOT NULL REFERENCES documents(id),"
    " provider TEXT NOT NULL, amount INTEGER NOT NULL, currency TEXT NOT NULL,"
    " url TEXT NOT NULL, remote_id TEXT, created_at TEXT NOT NULL)",
    "CREATE INDEX payment_links_doc ON payment_links(doc_id, id)",
    "CREATE INDEX documents_reference ON documents(kind, reference)",
};

// ---- version 4: purchase orders, time, employees, the check queue (M68, D-077)
// AGENT-CONTRACT (links, purchase_orders.h / time_tracking.h): a bill line
// names the purchase-order line it receives (po_line_id) and an invoice line
// the time entry it bills (time_entry_id). Neither is a foreign key: lines
// are rewritten (DELETE + INSERT keeping their ids, documents_write.cpp) and
// the engine keeps the links. Received quantities and billed status are
// DERIVED from live (non-void) documents, so voiding or editing a bill or
// invoice puts quantities and time back by itself. ship_to: a purchase
// order's ship-to address; to_print: a check or bill payment in the print
// queue (check_queue.h); closed: a purchase-order line closed by hand.
// AGENT-CONTRACT (employees, with payroll M71): `employees` is the list time
// is entered for — name, contact fields, active, an optional default hourly
// BILLING rate (minor units; NULL = the service item's price). It holds no
// pay rate, tax field or identity number: payroll (M71) extends this table
// in its own schema version. A time entry names an employee OR a vendor (a
// contractor), never both, never neither.
const char *const kVersion4[] = {
    "ALTER TABLE documents ADD COLUMN ship_to TEXT",
    "ALTER TABLE documents ADD COLUMN to_print INTEGER NOT NULL DEFAULT 0",
    "ALTER TABLE document_lines ADD COLUMN po_line_id INTEGER",
    "ALTER TABLE document_lines ADD COLUMN time_entry_id INTEGER",
    "ALTER TABLE document_lines ADD COLUMN closed INTEGER NOT NULL DEFAULT 0",
    "CREATE INDEX document_lines_po_line ON document_lines(po_line_id)",
    "CREATE INDEX document_lines_time_entry ON document_lines(time_entry_id)",
    "CREATE INDEX documents_to_print ON documents(to_print, account_id)",
    "CREATE TABLE employees ("
    " id INTEGER PRIMARY KEY, name TEXT NOT NULL, email TEXT, phone TEXT, address TEXT, notes TEXT,"
    " default_rate INTEGER CHECK (default_rate IS NULL OR default_rate >= 0),"
    " active INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL, updated_at TEXT NOT NULL)",
    "CREATE UNIQUE INDEX employees_name ON employees(name COLLATE NOCASE)",
    "CREATE TABLE time_entries ("
    " id INTEGER PRIMARY KEY, date TEXT NOT NULL,"
    " employee_id INTEGER REFERENCES employees(id), vendor_id INTEGER REFERENCES parties(id),"
    " customer_id INTEGER REFERENCES parties(id), item_id INTEGER REFERENCES items(id),"
    " minutes INTEGER NOT NULL CHECK (minutes > 0 AND minutes <= 1440),"
    " billable INTEGER NOT NULL DEFAULT 0, notes TEXT, class_id INTEGER REFERENCES classes(id),"
    " created_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
    " CHECK ((employee_id IS NULL) <> (vendor_id IS NULL)))",
    "CREATE INDEX time_entries_customer ON time_entries(customer_id, date)",
    "CREATE INDEX time_entries_employee ON time_entries(employee_id, date)",
    "CREATE INDEX time_entries_vendor ON time_entries(vendor_id, date)",
    // The purchase-order counter (company_settings.cpp) for files that
    // predate it; a new company writes the same keys from its settings.
    "INSERT OR IGNORE INTO settings(key, value) VALUES('purchase_order_prefix', 'PO-')",
    "INSERT OR IGNORE INTO settings(key, value) VALUES('next_purchase_order_number', 1001)",
};

template<std::size_t N>
bool runAll(const QSqlDatabase &db, const char *const (&statements)[N], QString *error)
{
    for (const char *statement : statements) {
        if (!Sql::exec(db, QString::fromLatin1(statement), {}, error)) {
            return false;
        }
    }
    return true;
}

bool step(const QSqlDatabase &db, int toVersion, QString *error)
{
    switch (toVersion) {
    case 1: return runAll(db, kVersion1, error);
    case 2: return runAll(db, kVersion2, error);
    case 3: return runAll(db, kVersion3, error);
    case 4: return runAll(db, kVersion4, error);
    default: break;
    }
    if (error != nullptr) {
        *error = QStringLiteral("no migration to schema version %1").arg(toVersion);
    }
    return false;
}

bool setUserVersion(const QSqlDatabase &db, int version, QString *error)
{
    return Sql::exec(db, QStringLiteral("PRAGMA user_version = %1").arg(version), {}, error);
}

} // namespace

int readUserVersion(const QSqlDatabase &db)
{
    return int(Sql::scalarInt(db, QStringLiteral("PRAGMA user_version")));
}

int readApplicationId(const QSqlDatabase &db)
{
    return int(Sql::scalarInt(db, QStringLiteral("PRAGMA application_id")));
}

bool create(const QSqlDatabase &db, int version, QString *error)
{
    if (!Sql::exec(db, QStringLiteral("PRAGMA application_id = %1").arg(kApplicationId), {}, error)) {
        return false;
    }
    for (int v = 1; v <= version; ++v) {
        if (!step(db, v, error)) {
            return false;
        }
    }
    return setUserVersion(db, version, error);
}

bool migrate(const QSqlDatabase &db, int fromVersion, QString *error)
{
    for (int v = fromVersion + 1; v <= kCurrentVersion; ++v) {
        if (!step(db, v, error) || !setUserVersion(db, v, error)) {
            return false;
        }
    }
    return true;
}

} // namespace QindaOffice::Books::Schema
