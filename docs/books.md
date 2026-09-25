<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaBooks: `books_engine`, `books_document`, `books_io` and the app

Bookkeeping for small businesses — proper double-entry records, invoices,
bills, banking, reconciliation, sales tax, reports and financial analysis
(M45–M47; decisions [D-045](decisions.md#d-045-bookkeeping-model-one-company-file-integer-money-immutable-postings)
and [D-046](decisions.md#d-046-bookkeeping-io-one-page-painter-for-pdf-and-paper-bank-files-in-accountant-files-out)).
QuickBooks is the reference for *scope*, not for look or file format.

| Target | Depends on | What it is |
| --- | --- | --- |
| `books_engine` | Qt Core + Qt Sql (QSQLITE) | The ledger and everything computed from it. No Gui, no Quick. |
| `books_document` | `books_engine`, `office_core` | `BooksDocument : Core::Document` — the company file as a DocumentHost document. |
| `books_io` | `books_engine`, Qt Gui/PrintSupport | Printed forms and reports (PDF/paper/CSV), bank-file import, CSV/IIF export, backups. |

## One company, one file

A business's books are **one `.qbooks` file** (SQLite). Different
businesses never share a file, so their records, customers and money
cannot mix; one file is one window (rule 1). The file is complete on its
own: copy it and you have copied the books.

- `PRAGMA application_id = 0x51424B53` ("QBKS"), `PRAGMA user_version` =
  schema version (currently **2**).
- `foreign_keys = ON`, `journal_mode = DELETE` (no `-wal`/`-shm`
  sidecars), `synchronous = FULL`, 5 s busy timeout.
- Opening an **older** file backs it up to `<file>.pre-v<N>.bak` and
  migrates forward in one transaction (audited). A **newer** file is refused
  (`ErrorCode::NewerSchema`, "written by a newer version of QindaBooks").
  A non-QindaBooks file is refused (`NotACompanyFile`).
- `CompanyFile::backupTo(path)` is `VACUUM INTO` — a consistent single-file
  copy, never over an existing file. `integrityProblems()` runs
  `integrity_check`, `foreign_key_check` and "every live transaction
  balances".
- There is **no unsaved state**: every mutation is one SQL transaction
  (nested calls use savepoints) and commits immediately;
  `CompanyFile::changed()` fires after each outermost commit.

### Schema (v2)

| Table | Holds |
| --- | --- |
| `settings` | key/value company settings (`CompanySettings`) |
| `accounts` | chart of accounts: number, name, type, parent, system role, functional group, active |
| `parties` | customers and vendors (kind 1/2), terms, 1099 flag, default expense account, default tax rate |
| `items` | products/services: price, cost, income and expense accounts, taxable |
| `classes`, `terms` | classes/tags; payment terms (Due on receipt, Net 15/30/60 seeded) |
| `tax_agencies`, `tax_components`, `tax_rates`, `tax_rate_components` | sales tax |
| `documents`, `document_lines` | the business documents the user edits (mutable, audited) |
| `transactions`, `journal_lines` | the postings (immutable; see below) |
| `applications` | payment/credit → invoice/bill/refund links |
| `recurring_templates` | schedules over an `InvoiceTemplate` document |
| `bank_feed`, `bank_rules` | imported bank lines and categorisation rules |
| `reconciliations`, `reconciliation_marks` | reconciliation sessions |
| `audit_log` | who/when/what with before/after JSON (append-only) |
| `budgets` (v2) | amount per account per month (per class) |

`journal_lines` duplicates the transaction's date, kind and void flag so
every report aggregates one indexed table.

## Money

`Money` is `qint64` **minor units** + an ISO 4217 code (exponent table:
USD 2, JPY 0, KWD 3 …; unknown codes get 2). Rates, quantities and
percentages are `Decimal` (mantissa × 10^-scale, parsed from text such as
`"1.5"` or `"8.25%"`). **No `double` or `float` exists anywhere in
`books_engine`** — `tst_books_money` scans the sources to prove it; report
ratios are fixed-point integers too.

- Rounding happens only where a call names a `RoundingMode`: `HalfUp`
  (default, ties away from zero), `HalfEven` (banker's, where a tax rule
  says so — per tax agency), `TowardZero`, `AwayFromZero`; 128-bit
  intermediates.
- `allocate(weights)` splits a total by largest remainder: the parts sum
  exactly, each within one minor unit of its share.
- `parse(text, currency, locale)` accepts grouping, symbol or code,
  leading/trailing minus and accounting parentheses, and **refuses** more
  fraction digits than the currency has (typed money is never silently
  rounded). `format(locale)` lays out the locale's currency pattern.
- One company = one currency (fixed once anything has posted).
  Multi-currency is out of scope.

## Chart of accounts

Fifteen account types, each with a normal balance and a report section:

| Type | Class | Normal | Section |
| --- | --- | --- | --- |
| Bank, Accounts receivable, Other current asset | Asset | Debit | Current Assets |
| Fixed asset / Other asset | Asset | Debit | Fixed / Other Assets |
| Accounts payable, Credit card, Other current liability | Liability | Credit | Current Liabilities |
| Long-term liability | Liability | Credit | Long-Term Liabilities |
| Equity | Equity | Credit | Equity |
| Income / Other income | Income | Credit | Income / Other Income |
| Cost of goods sold, Expense, Other expense | Expense | Debit | COGS / Expenses / Other Expenses |

Sub-accounts (same class as the parent) show as `Parent:Child`. Numbers
are optional and unique. **System roles** the engine posts to by itself:
Accounts Receivable, Accounts Payable, Undeposited Funds, Sales Tax
Payable, Retained Earnings, Opening Balance Equity, Discounts Given. An
account with postings cannot be deleted (`HasPostings`) — deactivate it,
after moving its balance out; system accounts cannot be deactivated.

**Starter templates** (JSON resources, every one names every system role):
`general` (small business), `services` (consulting/agency), `nonprofit`
(net assets with/without donor restrictions instead of owner's equity;
contributions, grants, program revenue; expenses grouped under Program
Services / Management and General / Fundraising with a `functional_group`
for the statement of functional expenses) and `creative` (project fees,
licensing, digital products, production costs, software and gear).

## Posting rules

Every business document that posts makes **one live, balanced journal
transaction** in the same SQL transaction as the document write:

| Document | Debit | Credit |
| --- | --- | --- |
| Invoice | A/R (total), Discounts Given | income per line, Sales Tax Payable per tax component |
| Sales receipt | Undeposited Funds (or the named account) | as an invoice |
| Credit memo | income per line, Sales Tax Payable | A/R, Discounts Given |
| Receive payment | Undeposited Funds (or bank) | A/R |
| Refund | A/R | bank |
| Bill | expense/asset per line | A/P |
| Bill payment | A/P | bank or credit card |
| Expense / Check / Card charge | expense per line | bank or credit card |
| Vendor credit | A/P | expense per line |
| Transfer | to-account | from-account |
| Deposit | bank | Undeposited Funds (per payment) or income |
| Sales tax payment | Sales Tax Payable (agency) | bank |
| Journal entry / Opening balance | as entered / account ↔ Opening Balance Equity | |

Estimates, drafts and recurring templates never post. Every journal line
carries the customer/vendor, and where relevant the item, class, tax
component and agency.

- **Balanced**: debits = credits, ≥ 2 lines, each line one-sided and
  non-negative, every account exists and is active — else `Unbalanced`,
  `Invalid`, `UnknownAccount`, `InactiveAccount`.
- **Immutable**: SQL triggers in the file refuse any UPDATE of an amount,
  account, date, party or memo of a posted line, any DELETE of a line or
  transaction, un-voiding, and any change to `audit_log`.
- **Edit = void + repost** in one step: the live transaction is voided
  (`voided = 1`, `replaced_by` → the new one), the document keeps its id
  (payments stay applied), the audit row carries before/after JSON and
  the reason. An invoice/bill edit that would drop the total below what
  is already applied is refused (`OverApplied`).
- **Void** keeps the document and transaction with zero effect (every
  balance filters `voided = 0`), unapplies its applications (a voided
  payment reopens its invoices), and is audited.
- **Lock date** (closing): posting or voiding on/before it is refused
  (`ClosedPeriod`) unless `PostOptions{overrideLock = true, reason}`; the
  reason is stored on the transaction and in `audit_log`.
- **Reconciled** lines lock their transaction (`Reconciled`) until the
  reconciliation is undone.

## Sales, purchases, tax

- Line amount = unit price × quantity (half-up) or the entered amount.
  A document **discount** (percent or amount) is pre-tax and shared across
  the lines by `allocate`; the taxable base is the taxable lines net of
  their share.
- **Sales tax**: agencies (remitted to) → components (one rate for one
  agency) → rates (one or several components, e.g. state 6% + city
  1.25%). Tax is computed **per component on the document's taxable base,
  rounded once per document** with the agency's rounding mode, and
  credited to Sales Tax Payable tagged with component and agency.
  `SalesTax::liability(agency)` and `recordPayment()` work per agency.
- Invoices take the customer's terms (else the company default, Net 30)
  for the due date; numbering is prefix + counter per kind
  (`INV-`, `EST-`, `CM-`, `SR-`), duplicates refused.
- **Payments** apply across several invoices; an overpayment remains as
  customer credit, applied later (`applyCredit`) or refunded. Credit memos
  work the same way. Bills are paid several at once (`payBills` makes one
  payment per vendor); vendor credits apply to bills.
- **Status** is derived: draft, open, partial, paid, overdue (past due
  with a balance, given "today"), void; estimates pending/closed.
- **Statements**: A/R activity for a customer between two dates with
  opening/closing balance and the aging of what is open.
- **Recurring invoices**: `occurrence(k)` is computed from the start date
  (Jan 31 monthly → Feb 28, Mar 31, Apr 30, never drifting); the template
  counts what it generated, so `generateDue(today)` twice creates nothing
  new. The engine never reads the clock for this.
- **Inventory quantities are out of scope**: items are services,
  non-inventory products or other charges with a price, a cost and
  accounts; nothing tracks stock on hand.

## Banking and reconciliation

- **Registers** per bank/card account with running balance in the
  account's normal sign, the other account (or `-Split-`) and clearing
  state.
- **Deposits** move chosen payments out of Undeposited Funds into a bank
  account; a voided deposit returns them to the queue.
- **Bank feed**: `BankLine.amount` is signed as a debit to the account
  (+ money in / card payment, − money out / card charge). Import dedups on
  FITID, else on (date, amount, normalised payee, occurrence within the
  file) — two identical coffees on one day survive, re-importing the same
  file adds nothing. Lines are then **excluded**, **matched** to an
  existing uncleared journal line (suggestions require the same amount,
  within ±10 days, scored by date closeness and payee similarity), or
  **categorised** (creates an Expense/Check/Card charge or a Deposit and
  clears it). **Rules** (payee/memo contains, direction, amount range,
  optional bank account, priority) fill suggestions that
  `acceptSuggestion` posts.
- **Reconciliation**: a session has a statement date and ending balance;
  its beginning balance is everything reconciled before. Marking lines
  clears them; `finish()` is refused until the difference is exactly zero
  and then stamps the lines reconciled; `undoLast()` reverses the latest
  session; the reconciliation report lists beginning, cleared increases
  and decreases, cleared balance, statement balance, difference and the
  uncleared items.

## Reports

Every report is a pure function returning a `ReportTable` (typed columns;
rows with kind — section, account, detail, subtotal, total — depth, one
cell per column, a stable key and a drill-down: account + date range,
party, item or document ids). Cells hold integers only.

| Report | Definition |
| --- | --- |
| Profit and Loss | Income − COGS = gross profit; − expenses = net operating income; ± other income/expense = net income. Columns: total, by month, by class, or comparative (prior period / prior year with $ and % change). |
| Balance Sheet | Assets = liabilities + equity on a date (always — tested on both bases and many dates). Earlier fiscal years' P&L sits in Retained Earnings, this year's as Net Income. Comparative prior year / prior month end. |
| Statement of Cash Flows | Indirect: net income + −(change of every non-cash balance-sheet account), grouped operating / investing (fixed, other assets) / financing (long-term liabilities, equity). Ties exactly to the change in cash (bank + Undeposited Funds). |
| Trial Balance | Every account's debit or credit balance; P&L year to date, earlier years folded into Retained Earnings; debits = credits. |
| General Ledger / Journal / Transaction List | What was posted, with running balances / debit–credit lines / one row per transaction. Voids left out. |
| A/R and A/P Aging (summary, detail) | Open balances by days past due: current (not yet due), 1–30, 31–60, 61–90, 91+; exactly 30 days late is 1–30. Unapplied credits show as negative current. |
| Customer / Vendor Balances | A/R or A/P per party as of a date. |
| Open Invoices / Unpaid Bills | Open documents with due date, days past due, amount and open balance. |
| Sales by Customer / by Product/Service | Income per customer / item with share of total. |
| Expenses by Vendor / by Category | Spending per vendor / per expense account with share. |
| Sales Tax Liability | Per agency and component: taxable sales, rate, tax collected, tax paid, balance due. |
| Budget vs. Actual | Actual, budget, over budget and % of budget per income/expense account (budgets are per account per month; `setAnnual` spreads a year exactly). |
| Reconciliation Report | See above. |

### Cash vs accrual

Accrual recognises income and expenses when invoiced/billed. **Cash**
takes invoices, credit memos, bills and vendor credits out of the ledger
and recognises them as money settles them: each application recognises
`round(line × Σ applied ÷ document total)` cumulatively — so a fully paid
document recognises exactly its own lines — dated on the application date,
with the A/R or A/P line set to balance. A 1,000.00 invoice paid 600.00
shows 600.00 of income; tax, discounts and classes are recognised in the
same proportion. P&L, Balance Sheet, Trial Balance, Cash Flow, the
sales/expense breakdowns, Budget vs Actual and the analysis honour the
basis (`CompanySettings::basis` is the default); the listings and aging
are accrual by nature.

### Worked example (the test fixture)

`tests/books_engine/books_fixture.h`, general template, Q1 2026: owner
puts in 10,000.00; invoice Acme 2 × Consulting 500.00; invoice Beta 1 ×
Widget 200.00 + 10% tax; January rent bill 1,500.00; card charge
120.00 office supplies; Acme pays 600.00 (deposited); rent paid; supplier
bill 80.00 COGS (unpaid); walk-in sales receipt 0.6 × 500.00 to checking.

| As of 2026-03-31 | Accrual | Cash |
| --- | --- | --- |
| Income | 1,500.00 | 900.00 |
| COGS / Expenses | 80.00 / 1,620.00 | 0.00 / 1,620.00 |
| Net income | −200.00 | −720.00 |
| Total assets (checking 9,400.00 + A/R 620.00) | 10,020.00 | 9,400.00 |
| Liabilities (A/P 80 + card 120 + tax 20) | 220.00 | 120.00 |
| Cash flow: operating / financing / net change | −600 / +10,000 / +9,400 | same |

Ratios Q1 accrual: gross margin 94.7%, net margin −13.3%, current ratio
45.55×, debt to equity 0.02×, DSO 37 days (620 × 90 ÷ 1,500), DPO 4 days.

## Financial analysis

`FinancialAnalysis` (every figure carries a one-sentence definition for the
tooltip): gross margin, net margin, current ratio, quick ratio, debt to
equity, days sales outstanding, days payable outstanding (fixed-point
`value` + `scale`; unavailable when the denominator is zero); the expense
mix (top N accounts + "everything else" with shares); monthly trends
(revenue, expenses, net income, month-end cash) for N months; a
trailing-average forecast projected flat — a trend line, not a prediction.

## The document adapter (`books_document`)

`BooksDocument : Core::Document` maps the window lifecycle onto the
company file (its header has the full contract):

- An opened or created company is **never dirty** — every posting has
  already committed. Save is a successful no-op; Ctrl+W asks nothing.
- **Save As = save a copy and switch to it** (`VACUUM INTO` a temp file,
  atomic rename over the target after the host's overwrite consent,
  reopen). The original file stays as it was.
- **No recovery journal**: SQLite's rollback journal already rolls a crash
  back to the last commit; `recoverySnapshot()` is empty and
  `loadRecovery()` refuses with an explanation.
- An **untitled** company (`createUntitled`) lives in a scratch file and is
  dirty until the first Save As, so closing it asks for consent.
- The byte revision follows our own commits, so the host's "changed on
  disk" check never fires on our postings.

## Import and export (`books_io`)

**Printed forms and reports.** Invoices, estimates, sales receipts,
credit memos and customer statements (`buildPrintableDocument`,
`buildPrintableStatement` → `PrintableDocument` → `FormRenderer`) and
every `ReportTable` (`ReportPdf`) are drawn by one `PagePainter`
(`std::function<void(QPagedPaintDevice*, QPainter*)>`) per output. The
same function writes the PDF (`writePdf`, QPdfWriter), prints to paper
and feeds Print Preview — `asPrintCallback<QPrinter>(painter)` is exactly
office_quick's `PrintRenderCallback` — so they cannot differ. Pages are
laid out in points on a white page in black ink with the company's single
accent colour (D-004): logo, company and customer blocks, document facts,
a highlighted amount due, a line table whose header repeats on every page,
totals (discount, tax, paid, balance due), notes, payment instructions and
"Page x of y". Wide reports turn landscape. Letter for US/CA/MX locales,
A4 elsewhere. Reports also export as RFC 4180 CSV (`reportToCsv`) with
money in plain `-1234.56` form.

AGENT-NOTE (fonts): Noto Sans, no OpenType features, ligatures off. Qt's
PDF engine gives glyphs substituted by a font feature (`tnum`, `ffi`) no
text mapping, so digits vanished from the PDF's text layer; the
text-extraction assertions in `tst_books_io_forms` guard it.

**Bank statement import** (`BankImport`, all into `BankLine`s):

- **CSV**: delimiter, header columns (date/posted, amount,
  debit/withdrawal, credit/deposit, description/payee/merchant, memo,
  check number, id/FITID), date format (a day above 12 settles day-first
  vs month-first; month names; two-digit years) and decimal comma are
  detected; any of it can be set by hand with a `CsvMapping`. One signed
  amount column or separate money-out/money-in columns; `invertSign` for
  card exports that list charges as positive.
- **OFX/QFX**: SGML 1.x (unclosed leaf tags) and XML 2.x; `STMTTRN` with
  TRNTYPE, DTPOSTED, TRNAMT, FITID, NAME/PAYEE, MEMO, CHECKNUM; entities
  decoded; account id and ledger balance reported.
- **QIF**: `!Type:Bank` and `!Type:CCard`, dates such as `1/15'26` with a
  day-first hint.

Amounts go straight into minor units of the company currency (never
floating point); more decimals than the currency allows is a row error,
not a rounding. Sign: positive = money into the account (deposit, or a
payment onto a card), negative = money out. Importing the same file twice
adds nothing (the engine's dedup: FITID, else date + amount + payee +
position among identical lines).

**Export and import.** General journal CSV and chart of accounts /
customers / vendors / items CSV (`LedgerExport`), which import back into
another company (`ListImport::importCsv`, through the engine's validating
and audited APIs; a clashing account number is a row error, never a
renumbering). **QuickBooks IIF** for accountants: account types map to
BANK, AR, OCASSET, FIXASSET, OASSET, AP, CCARD, OCLIAB, LTLIAB, EQUITY,
INC, EXINC, COGS, EXP, EXEXP; sub-accounts become `Parent:Child`;
customers, vendors and items (`!CUST`, `!VEND`, `!INVITEM`); every live
transaction becomes a balanced `GENERAL JOURNAL` TRNS/SPL block with
signed amounts. The IIF item-type keywords (SERV, NONINV, OTHC) are
unverified against Intuit's current reference — check before relying on
item import into QuickBooks. IIF is export-only.

**Backups.** `Backups::createTimestamped(file, dir, keep)` makes a
`VACUUM INTO` copy named by time and keeps the newest `keep`; each copy is
a complete company file.

## Out of scope — plainly

Payroll; inventory quantities and COGS from stock; multi-currency and
currency revaluation (one company, one currency); online bank-feed
connections (statements are imported from files the bank lets you
download); e-filing of tax returns and 1099s (vendors carry a 1099 flag
and Expenses by Vendor gives the totals); multi-user concurrent editing of
one company file.

## QindaBooks (app)

`apps/qindabooks` (binary `qindabooks`, desktop id
`org.qindaqt.QindaBooks`, MIME `application/x-qindabooks`, M47,
[D-047](decisions.md#d-047-bookkeeping-app-one-company-per-window-a-service-over-the-engine-one-print-source)):
a static QML module `QindaOffice.Books.App` over `office_quick`, like the
other apps. One window = one `BooksHost` = one `BooksDocument` = one
company file.

- **`BooksHost : DocumentHost`** owns the document, the commands and the
  menus (File ▸ New Company… / Open… / Backup… / Import and Export ▸ /
  Company Settings… / Close the Books…; Sales, Expenses, Banking,
  Accounting and Reports menus; View ▸ the six workspaces, Ctrl+1…6).
  Every command is an `ActionEntry` with a `books…Action` id
  (`books_host_actions.cpp` lists them) whose handler only **emits a
  request** (`documentEditorRequested(kind, id)`,
  `paymentDialogRequested("receive" | "payBills")`,
  `partyDialogRequested(kind, id)`, `exportRequested(what)`, …) that the
  QML answers. `currentView` mirrors the workspace radio group;
  `selectedDocumentId` is what Void / E-mail / Print Document act on. A
  window still without a company when it reaches the event loop emits
  `companySetupRequested()` once — the setup wizard — so File ▸ New
  Company is "a new window that asks"; Open into that pristine window
  loads instead. Save is a no-op on an open company, Save As copies and
  switches (the books_document contract).
- **`BooksService`** (`BooksHost.books`) is the QVariant API the views
  call. It never caches the `CompanyFile*`: every call asks the document,
  so after Save As it works on the copy without being re-created, and
  `companyChanged()` tells the views. `changed()` fires after every
  committed mutation (forwarded `CompanyFile::changed`), `revision`
  increments with it. Money crosses as **integer minor units plus text**
  (`amount` + `amountText`); inputs are typed text (company locale, then
  C locale; excess decimals refused, never rounded) or minor units. The
  only doubles are the dashboard's `…Chart` series (major units, for
  drawing). Dates are ISO strings; "today" comes from a clock tests pin
  (`setToday`, or `QQO_BOOKS_TODAY=yyyy-MM-dd` for grabs). Every mutating
  call returns `{ok, id, error}`.

| Area | Calls |
| --- | --- |
| Company | `hasCompany`, `companyName`, `settings` / `saveSettings` (keys: name, legalName, tradeName, address, email, phone, website, taxId, currency, locale, fiscalYearStartMonth, basis, invoicePrefix, nextInvoiceNumber, …, invoiceNotes, paymentInstructions, accentColor, lockDate read-only), `currency`, `currencies()`, `formatMoney`, `formatPlain`, `parseMoney`, `templates()`, `createCompany(url, settings, templateId)` (empty url = untitled scratch company), `setLockDate`, `lockDate`, `today`, `revision` |
| Lists | `accounts`, `accountTypes`, `saveAccount` (+ `openingBalance`/`openingDate`), `setAccountActive`, `paymentAccounts`, `expenseAccounts`, `incomeAccounts`, `parties`, `party`, `saveParty`, `setPartyActive`, `items` (`type` service/product/other, `price` plain text, `priceMinor`), `saveItem`, `terms`, `classes`, `saveClass`, `taxRates`, `taxAgencies`, `saveTaxRate` (creates or reuses the agency by name) |
| Documents | `documents(filter)` (kind or group sales/purchases/journal/all; status open/overdue/paid/draft/void/pending/closed/all; from, to, partyId, search), `document(id)`, `computeTotals(draft)` (live editor totals, due date, one line entry per input row), `saveDocument(draft)` (drafts only for invoices; invoices, bills and estimates edit in place, other kinds are voided and re-entered), `postDraft`, `convertEstimate`, `voidDocument(id, reason, overrideLock)` |
| Money in/out | `receivePayment`, `payBills` (one payment per vendor), `openInvoices`, `unpaidBills(0 = all)`, `exportDocumentPdf`, `emailDocument`, `recurringTemplates`, `saveRecurring`, `generateDueRecurring`, `dueRecurringCount` |
| Journal | `postJournalEntry` (id = replace), `checkJournalEntry` (balance check, posts nothing), `transfer`, `deposit`, `undepositedPayments`, `auditTrail` |
| Banking | `bankAccounts`, `accountRegister` (the brief's `register`: a C++ keyword), `importStatement(account, url, mapping?)` (CSV/OFX/QFX/QIF; an unsure CSV returns `needsMapping` + `columns`; rules run on new lines), `feed(account, status)` with a rule or match `suggestion`, `acceptSuggestion`, `categorize`, `exclude`, `restore`, `rules`, `saveRule` (edit = replace), `deleteRule`, `applyRules` |
| Reconcile | `startReconciliation`, `reconciliationInProgress`, `reconcileCandidates`, `markCleared`, `reconcileState`, `finishReconciliation`, `cancelReconciliation`, `undoLastReconciliation` |
| Reports | `reportCatalog()` (with the option words each report takes), `runReport(id, options)` (the `ReportTable` as a map: typed columns, rows with kind/depth/key/cells/drill), `drillDown(drill)`, `exportReport(id, options, url, "pdf" \| "csv")`, `rangePreset(name)` (fiscal-year aware) |
| Dashboard | `kpis(asOf)` (MTD/YTD income, expenses, net; open/overdue invoices, unpaid bills, cash), `monthlyTrends(months)`, `expenseBreakdown(from, to)`, `ratios(asOf)` (value text, definition, `good`) |
| Housekeeping | `backupNow()` (timestamped copies in `$XDG_DATA_HOME/qindaoffice/books/backups`, newest 20 kept), `backupTo(url)`, `exportIif`, `exportJournalCsv`, `exportListCsv`, `importListCsv` |

**Printing the current source.** Print, Print Preview and File ▸ Export
PDF render whatever the QML named with
`BooksHost.setPrintSource("report", reportId, options)` (the report on
screen) or `setPrintSource("document", id)` (the selected invoice,
estimate, sales receipt or credit memo). The host builds the value when
the job is created and hands office_quick one `PrintRenderCallback` —
books_io's painter wrapped by `asPrintCallback<QPrinter>` — so paper,
preview and PDF are the same pages. `printLandscape` says when a report
reads better wide. With no source the print commands are disabled.

**E-mailing an invoice.** `emailDocument(id)` writes the form's PDF to
`$XDG_CACHE_HOME/qindaoffice/books/outbox/<number>.pdf` and starts
`qindamail mailto:<customer>?subject=Invoice INV-1001 from <company>&body=…`.
QindaMail's `mailto:` handling reads to/cc/bcc/subject/body only, so the
PDF is **not attached**: the service says so through `notify()` and names
the file to attach. When QindaMail takes an attachment argument, pass
the path there.

**Runtime requirement.** The company file is SQLite through Qt Sql's
**QSQLITE driver plugin**, which is loaded at run time: a Qt without it
builds and links fine and fails on the first open. The ebuild must
depend on `dev-qt/qtbase[sql,sqlite]` (in addition to what the suite
already needs).

## Tests

`tests/books_engine/` (M45): `tst_books_money`, `tst_books_company`,
`tst_books_ledger`, `tst_books_sales`, `tst_books_purchases`,
`tst_books_banking`, `tst_books_reports`, `tst_books_analysis`,
`tst_books_perf` (50,000 journal lines; P&L and Balance Sheet each under
one second), `tst_books_document`. `tests/books_io/` (M46): see
[verification](verification.md#m45m46-gates-bookkeeping-engine-and-io).
`tests/qindabooks/` (M47): `tst_qindabooks_service`,
`tst_qindabooks_banking`, `tst_qindabooks_host`,
`tst_qindabooks_menu_export` — see
[verification](verification.md#m47-gates-the-qindabooks-app).
