<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaBooks: `books_engine`, `books_document`, `books_io` and the app

Bookkeeping for small businesses — proper double-entry records, invoices,
bills, banking, reconciliation, sales tax, reports and financial analysis
(M45–M48; decisions [D-045](decisions.md#d-045-bookkeeping-model-one-company-file-integer-money-immutable-postings),
[D-046](decisions.md#d-046-bookkeeping-io-one-page-painter-for-pdf-and-paper-bank-files-in-accountant-files-out)
and [D-048](decisions.md#d-048-bank-and-payment-connections-feed-lines-first-tokens-in-the-keyring));
purchase orders, employees and time, and the check print queue (M68,
[D-077](decisions.md#d-077-qindabooks-purchase-orders-time-tracking-check-printing-workflow)).
QuickBooks is the reference for *scope*, not for look or file format.

| Target | Depends on | What it is |
| --- | --- | --- |
| `books_engine` | Qt Core + Qt Sql (QSQLITE) | The ledger and everything computed from it. No Gui, no Quick. |
| `books_document` | `books_engine`, `office_core` | `BooksDocument : Core::Document` — the company file as a DocumentHost document. |
| `books_io` | `books_engine`, Qt Gui/PrintSupport, `calc_io` (M63) | Printed forms and reports (PDF/paper/CSV/Excel), bank-file import, CSV/IIF export, QuickBooks IIF import, backups. |
| `books_connect` | `books_engine`, Qt Network, qt6keychain | Online connections (M48): Mercury bank transactions and Stripe balance history into the bank feed, payout matching, Stripe payment links; tokens in the keyring. |

## One company, one file

A business's books are **one `.qbooks` file** (SQLite). Different
businesses never share a file, so their records, customers and money
cannot mix; one file is one window (rule 1). The file is complete on its
own: copy it and you have copied the books.

- `PRAGMA application_id = 0x51424B53` ("QBKS"), `PRAGMA user_version` =
  schema version (currently **4**, M68).
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

### Schema (v4)

| Table | Holds |
| --- | --- |
| `settings` | key/value company settings (`CompanySettings`) |
| `accounts` | chart of accounts: number, name, type, parent, system role, functional group, active |
| `parties` | customers and vendors (kind 1/2), terms, 1099 flag, default expense account, default tax rate |
| `items` | products/services: price, cost, income and expense accounts, taxable |
| `classes`, `terms` | classes/tags; payment terms (Due on receipt, Net 15/30/60 seeded) |
| `tax_agencies`, `tax_components`, `tax_rates`, `tax_rate_components` | sales tax |
| `documents`, `document_lines` | the business documents the user edits (mutable, audited); v4: a purchase order's `ship_to`, a check's `to_print`, a bill line's `po_line_id`, an invoice line's `time_entry_id`, a purchase-order line's `closed` |
| `transactions`, `journal_lines` | the postings (immutable; see below) |
| `applications` | payment/credit → invoice/bill/refund links |
| `recurring_templates` | schedules over an `InvoiceTemplate` document |
| `bank_feed`, `bank_rules` | imported bank lines and categorisation rules |
| `reconciliations`, `reconciliation_marks` | reconciliation sessions |
| `audit_log` | who/when/what with before/after JSON (append-only) |
| `budgets` (v2) | amount per account per month (per class) |
| `connections` (v3) | one row per online connection (`mercury`, `stripe`): status, settings JSON, sync cursor, last sync, last error — never a token |
| `payment_links` (v3) | online payment links made for invoices (amount, currency, URL, Stripe id) |
| `employees` (v4) | the people time is entered for: name, email, phone, address, notes, an optional default hourly **billing** rate, active — nothing else (payroll, M71, extends this table in its own schema version) |
| `time_entries` (v4) | date, an employee **or** a vendor (exactly one, a CHECK), customer:job, service item, whole minutes (1 … 1440), billable, notes, class |

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

Estimates, drafts, recurring templates and purchase orders (M68) never
post. Every journal line carries the customer/vendor, and where relevant the
item, class, tax component and agency.

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
  new. The engine never reads the clock for this. `update()` (M63) rewrites
  the name, the invoice body and the posting mode; an unchanged schedule
  keeps its start and count, a changed one is **re-based** — occurrence 0
  becomes the next invoice date the user chose, the count restarts and
  `maxOccurrences` counts the invoices still to come — and may not start on
  or before the last invoice it made. `remove()` deletes the schedule and
  its template body; the invoices it made stay. Both are audited.
- **Inventory quantities are out of scope**: items are services,
  non-inventory products or other charges with a price, a cost and
  accounts; nothing tracks stock on hand.

## Purchase orders (M68, D-077)

`PurchaseOrders` (`purchase_orders.h`). A purchase order is a document of
kind `PurchaseOrder` that **never posts**: vendor, date, number (`PO-1001`;
the prefix and counter are company settings), expected date (the document's
due date), ship-to address, vendor message (printed), internal memo, and
lines — an item (its cost and expense account fill in) or a category,
description, quantity, rate, amount and a customer:job.

- **Receiving.** A bill line that receives an order line remembers it
  (`po_line_id`). An order line's received quantity is the sum of the
  quantities of the **live** bill lines that name it — nothing is stored —
  so voiding the bill, or editing it so the line is gone, puts the quantity
  back. A receipt is refused when it is more than what remains, comes from
  another vendor's bill, or names a closed line (a bill that already
  received it may keep it). Rate × received quantity is the bill line; an
  amount-only line is received in proportion.
- **Status** (derived): Open; Partially received once a bill received
  anything; Closed when everything is received, when every line not
  received was closed by hand, or when the whole order was closed by hand
  (reopening is allowed); Void. A received line cannot be removed from the
  order nor ordered below what was received, the vendor cannot change once
  anything was received, and an order a live bill receives cannot be voided
  — close it instead.
- **In the app.** *Expenses ▸ Purchase Order…* opens `PurchaseOrderEditor.qml`
  (with `OrderLinesTable.qml`: received "6 of 10" per line, a Closed box per
  saved line, a Closed switch for the order; Print, Save PDF…, Email to the
  vendor, Create bill, Void…). *Expenses ▸ Create Bill from Order* (or the
  editor's Create bill) opens a bill receiving everything left. A new bill
  for a vendor with open order lines offers them (`ReceiveOrderDialog.qml`:
  tick a line to receive all of it, or type part of it); received lines
  carry the order number as a badge. Expenses has a *Purchase orders* tab
  (All / Open / Partly received / Closed; the balance column is what is
  still to receive) and the vendor list an *On order* column.
- **Report.** Open Purchase Orders (by vendor, as of a date): each order
  neither closed nor void, its amount and its open balance — what is left
  to receive at the order's prices, received as of the date.
- **QuickBooks.** IIF `PURCHORDR` blocks import as purchase orders (below).

## Employees and time (M68, D-077)

`Employees` (`employees.h`) and `TimeTracking` (`time_tracking.h`,
`time_sheet.cpp`).

- **Employees** are a minimal list of their own — name (unique), email,
  phone, address, notes, active, and an optional **default hourly rate**
  that their billable time bills at (else the service's price). Contractors
  are vendors. **Payroll extends this list (M71)**: pay rates, tax setup and
  identity numbers arrive in their own schema version and never in these
  fields.
- **A time entry**: date, an employee or a vendor, customer:job (none =
  internal time), service item, duration (typed `1:30`, `1.5` or `90m`;
  stored as whole minutes, at most 24:00), billable, notes. Billable time
  needs a customer:job and a service.
- **Billed** is derived: an entry is billed while a non-void invoice (a
  draft too) has a line naming it (`time_entry_id`). Billed time cannot be
  edited or deleted; voiding the invoice or removing the line makes it
  unbilled again. The invoice line is the service item, the hours as the
  quantity — minutes ÷ 60 to two decimals, half-up (20 minutes = 0.33 h,
  45 = 0.75) — the rate (employee rate, else the item's price) and the notes
  (else the item's description) as the description.
- **Weekly timesheet**: one person's week, Monday first, a row per
  customer:job, service, billable and notes with a cell per day; a second
  entry for the same row and day opens another row. Saving writes the week
  in one SQL transaction; a billed cell is read-only and an attempt to
  change it saves nothing.
- **In the app.** *Time ▸ Time Activity…* (`TimeActivityDialog.qml`),
  *Time ▸ Weekly Timesheet…* (`TimesheetDialog.qml`), *Time ▸ New
  Employee…* (`EmployeeDialog.qml`), and the **Time** workspace (Ctrl+7,
  `TimeView.qml`: activities with All / Unbilled / Billed / Not billable,
  and the employees). A new invoice for a customer with unbilled billable
  time offers it (`BillableTimeDialog.qml`: pick which; each becomes a line).
- **Reports** (QuickBooks' names; durations as h:mm): Time by Job
  (customer:job → service), Time by Name (person → customer:job) and
  Unbilled Time by Job (each unbilled billable entry with its rate and what
  it will bill), for a period.

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
| Budget vs. Actual | Actual, budget, over budget and % of budget per income/expense account (budgets are per account per month; `setAnnual` spreads a year exactly; `setFiscalYearMonths` writes a fiscal year's twelve months in one transaction — the app's budget editor). |
| Reconciliation Report | See above. |
| Open Purchase Orders (M68) | Per vendor, each open order's amount and what is left to receive, as of a date. |
| Time by Job / Time by Name / Unbilled Time by Job (M68) | Durations (h:mm) per customer:job and service, per person and customer:job; unbilled billable time with rate and amount. |

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
money in plain `-1234.56` form, and — M63 — as an Excel workbook
(`ReportXlsx`, through calc_io's writer, the code QindaCalc saves with):
company, title and period above a bold header row; money cells are numbers
in major units under the company currency's format (`"$"#,##0.00;-"$"#,##0.00`,
exactly the currency's digits), numbers and percentages are numbers (a
percentage as a fraction under `0.0%`), dates are date serials under
`yyyy-mm-dd`; labels keep their indentation, subtotals and totals are bold
with a rule above, the header row is frozen and the notes follow.

**Checks** (M63, D-071 item 7). `buildPrintableCheck` turns a Check, or
a Bill Payment drawn on a bank account, into a `PrintableCheck`
(`check_print.h`), and `CheckRenderer` paints one per page for standard
US voucher stock (letter: the check is the top 3.5 in, two 3.5 in stubs
follow). The stock pre-prints the bank, the company, the labels, the check
number, the signature line and the MICR line, so the check third prints
only the values, each on its line (baselines measured from the sheet's
top-left corner, not from the printer's printable area): the date
(numeric, four-digit year, in the locale's order) top right, the payee,
the amount in figures (`**1,234.56`) in the amount box, the amount in
English words (`One thousand two hundred thirty-four and 56/100`) starred
to the end of its line, the payee's name and postal address where a #10
double-window envelope shows them, and the memo — the document's printed
note (the check editor's "Memo printed on the check"; the internal memo
never prints). Both stubs print in full: payee, date, number, the bills the
payment pays (date, bill, bill amount, paid) or the check's account lines,
the check amount, the company and the bank account; a stub that cannot hold
every line says how many it left out. Debit-card expenses, credit-card
payments, void and draft documents and zero amounts are refused with the
reason (`isPrintableCheck`). Not yet: blank stock (MICR toner, the bank's
routing digits), words in languages other than English. (M68: the print
queue and the per-printer alignment, below.)

**Check printing workflow** (M68, D-077; `check_queue.h`,
`check_alignment.h`). A check, or a bill payment from a bank account, can
be saved **to print later** (the check editor's *Print later*, Pay Bills'
*Print checks later*): it posts at once, without a number. *Expenses ▸
Checks to Print…* (`PrintChecksDialog.qml`) lists the queue of a bank
account and the first check number — the account's next one, never below
one past the highest number the account used — and *Print* gives the
chosen checks consecutive numbers and sends them to the printer through the
`"checks"` print source. Because a posted transaction's number cannot
change (D-045), giving or taking a number voids the live transaction and
posts the same lines under the new number; a check that has cleared, or is
dated in a closed period, is refused. Then the confirmation: *Done* when all
printed; a check ticked *Reprint* goes back to the queue without its number
and that number is **recorded void** (a void check with the number, payee,
bank account and the payment it was printed for), so the reprint takes the
next numbers; *Nothing printed* takes the run back with its numbers unused.
**Alignment** (*Expenses ▸ Check Printer Alignment…*,
`CheckAlignmentDialog.qml`): per printer name, move every field right and
down (inches to the hundredth or millimetres to the tenth, at most an inch
either way; negative moves left and up), kept per user in
`$XDG_CONFIG_HOME/qindaoffice/books/check-alignment.json` because one
printer serves every company file. The check painter shifts the whole sheet
by the offset of the printer the job goes to, looked up when it paints; a
PDF is never shifted. *Print test page* prints a sample check with a mark at
each field's anchor and a scale in tenths of an inch, to hold over the stock
against the light; *Save test page as PDF…* writes it with the printer's
offset.

**Purchase orders print** through the same forms path (`PurchaseOrder`
kind of `PrintableDocument`): vendor, ship-to, P.O. number, date, expected
date, item / description / customer / quantity / rate / amount (the item and
customer columns only when a line uses them), the order total and the
vendor message; e-mail goes to the vendor without payment instructions.

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
renumbering). In the app, *File ▸ Import and Export ▸ Import Lists… /
Export Lists…* open one dialog (`ListsDialog.qml`, M63) that asks which
list travels and reports what an import added, what was already there
and every refused row by number. **QuickBooks IIF** for accountants: account types map to
BANK, AR, OCASSET, FIXASSET, OASSET, AP, CCARD, OCLIAB, LTLIAB, EQUITY,
INC, EXINC, COGS, EXP, EXEXP; sub-accounts become `Parent:Child`;
customers, vendors and items (`!CUST`, `!VEND`, `!INVITEM`); every live
transaction becomes a balanced `GENERAL JOURNAL` TRNS/SPL block with
signed amounts. Item types are Intuit's INVITEMTYPE keywords — SERV, PART
(non-inventory part), OTHC (M63: before, non-inventory parts were written
as NONINV, which QuickBooks does not know); ACCNT rows carry EXTRA
(UNDEPOSIT, OPENBAL, RETEARNINGS) and HIDDEN, customers and vendors CONT1,
the shipping address and HIDDEN, items COGSACCNT and HIDDEN — so the export
imports back exactly (below).

### Moving from QuickBooks (M63, D-071)

**QuickBooks Desktop: IIF import** (`IifReader` + `IifImport`,
*File ▸ Import and Export ▸ Import from QuickBooks (IIF)…*). The reader
follows Intuit's format: tab-separated lines, `!TYPE` header rows naming
the columns of the rows after them (a later header replaces one), quoted
fields with doubled quotes, TRNS / SPL / ENDTRNS blocks, MM/DD/YYYY dates
(M/D/YY too), amounts positive for a debit and negative for a credit, bytes
UTF-8 else Windows-1252.

- **Lists first.** ACCNT with QuickBooks' types mapped to ours (BANK, AR,
  OCASSET, FIXASSET, OASSET, AP, CCARD, OCLIAB, LTLIAB, EQUITY, INC, EXINC,
  COGS, EXP, EXEXP; NONPOSTING left out), sub-accounts "Parent:Child"
  (missing parents made), a number this company already uses left off,
  HIDDEN accounts deactivated once the transactions are in, OBAMOUNT
  not imported (balances come from transactions). QuickBooks' own Accounts
  Receivable, Accounts Payable, Undeposited Funds, Sales Tax Payable,
  Retained Earnings and Opening Bal Equity (by name or EXTRA marker) are
  this company's system accounts. CUST, VEND (1099, tax id), INVITEM (SERV,
  PART, INVENTORY as non-inventory — no quantities —, OTHC; discount,
  subtotal, group, payment and tax items left out) and CLASS. A name the
  company already has is left as it is.
- **Then transactions, exactly.** A block is refused, with its line
  numbers, when its rows do not sum to zero, a date or amount does not
  read, or an account is unknown — the preview lists those accounts and
  the user maps each to one of the company's (`accountMap`). Every other
  block posts exactly its rows: INVOICE, CREDIT MEMO, CASH SALE, PAYMENT,
  BILL, BILL PMT, CHECK, CREDIT CARD, DEPOSIT and TRANSFER become the
  matching document when that document's posting reproduces the rows line
  for line (checked after posting, inside a savepoint); anything else — and
  GENERAL JOURNAL — posts as a journal entry with the same rows. Deposits
  take the matching payments out of Undeposited Funds (same customer and
  amount) and keep any other rows as deposit lines (`Banking::deposit`'s
  `otherLines`). Payments, credit memos and bill payments go to the oldest
  open invoices and bills of the same customer or vendor (IIF carries no
  links; switch it off to leave payments unapplied — bill payments then
  post as journal entries). A transaction already in the books (same date,
  same account/amount rows, posted for the same main name — counted per
  occurrence) is skipped: importing a file twice adds nothing.
- **Purchase orders (M68).** `PURCHORDR` blocks — QuickBooks' open
  purchase orders, on its non-posting "Purchase Orders" account — never
  post: they become purchase orders (TRNS NAME the vendor, DOCNUM the
  number, DUEDATE else SHIPDATE the expected date, SADDR1–5 the ship-to;
  each SPL an INVITEM / ACCNT line with QNTY, PRICE — kept only where price
  × quantity is the AMOUNT —, MEMO as the description and NAME as the
  customer:job), `PAID Y` imports one closed, and an order already in the
  books (vendor, date, number, total) is skipped. Before M68 these blocks
  were refused for an unmapped account.
- **One batch, previewed.** The import runs in one SQL transaction; the
  preview is the same run rolled back, so its counts are what Import does.
  Import refuses while any transaction is refused unless the user chooses
  to leave those out, and makes a timestamped backup copy first (postings
  are never undone, D-045; the copy is the way back).

**QuickBooks Online and Desktop list exports (CSV)** import through
*Import Lists* (`ListImport`): "Customer"/"Vendor"/"Product/Service" name
columns, "Full Name" as the contact, "Phone Numbers", "Account #" /
"Accnt. #" as numbers, Street/City/State/ZIP/Country joined into the
address, QuickBooks' type names ("Accounts receivable (A/R)", "Other
Current Liabilities", "Long Term Liabilities", "Expenses"; "Inventory"
items as non-inventory), and the company's own A/R and A/P rows skipped.

**Backups.** `Backups::createTimestamped(file, dir, keep)` makes a
`VACUUM INTO` copy named by time and keeps the newest `keep`; each copy is
a complete company file.

## Bank and payment connections (Mercury, Stripe)

M48, [D-048](decisions.md#d-048-bank-and-payment-connections-feed-lines-first-tokens-in-the-keyring),
library `books_connect`. A company can pull its **Mercury** bank
transactions and its **Stripe** balance history straight into the books,
and put a **Stripe payment link** on an invoice. Everything arrives in the
bank feed first, as if a statement had been imported, so rules, matching,
review and reconciliation work exactly as they do for files.

### Setting it up (what the user does once)

1. **Mercury — create a read-only API token.** In Mercury: *Settings ▸
   Tokens ▸ Create an API token*, choose **Read Only** (read-only tokens
   need no IP whitelist; QindaBooks never moves money), copy the token —
   Mercury shows it once. In QindaBooks: *Banking ▸ Connections… ▸
   Mercury*, paste it. QindaBooks lists the Mercury accounts (name, last
   four digits, balance); for each one to sync, pick an existing bank
   account or let QindaBooks create **"Mercury Checking ••1234"**.
2. **Stripe — create a restricted key.** In the Stripe Dashboard:
   *Developers ▸ API keys ▸ Create restricted key* (a key starting
   `rk_live_`; `rk_test_` for a sandbox). Give it **Read** on:
   - **Balance** — `GET /v1/balance` (checking the key) and the balance
     history;
   - **Balance transaction sources** — `GET /v1/balance_transactions`
     with the sources expanded;
   - **Charges**, **Refunds**, **Payouts** (and **Disputes** if you take
     disputes) — the expanded source objects;
   - optionally **Account** (Connect) — only for the business name shown
     in Connections; without it the connection is simply called "Stripe".

   For **payment links** add **Write** on **Products**, **Prices** and
   **Payment Links** (`POST /v1/prices` with `product_data`, `POST
   /v1/payment_links`). A key without them syncs fine and only payment
   links fail, with Stripe's own message naming the missing permission.
   If you prefer, keep sync on a read-only key and do not use payment
   links. (Permission labels are as the Dashboard's restricted-key editor
   names them; they were not checked against a live account — Stripe's
   403 answer, shown verbatim minus the key, says which one to add.)
   Paste the key in *Banking ▸ Connections… ▸ Stripe*. QindaBooks creates
   a bank-type **"Stripe (USD)"** clearing account and a **"Merchant
   Fees"** expense account if the chart has none, and sends payouts to the
   Mercury-linked checking account (all three can be changed).
3. *Banking ▸ Sync Mercury & Stripe* (**F5**) — the first sync reaches
   back 90 days; later ones continue from where the last one stopped.

A token is checked with the provider **before** it is stored; a refused
one is never kept. Tokens live only in the desktop keyring (QtKeychain,
service `qindaoffice-books`, key `<companyUuid>/<provider>`), never in the
company file, a log, a result or an error message (errors are redacted,
tests assert all four). `companyUuid` is a stable id in the company's
settings, created on first use; a copy of the file (Save As, a backup)
keeps it and so reaches the same keyring entry — it is the same business.
*Disconnect* removes the token and the connection's settings; everything
already imported stays. A company without connections never touches the
keyring.

### Mercury

`GET /accounts` validates the token and lists accounts; `GET
/account/{id}/transactions?start=…&limit=500&offset=…&order=asc` pages
until `total` (Mercury API reference, checked 2026-09-25). Only **posted**
transactions are imported — status `sent` with a `postedAt` time; pending,
failed, cancelled, reversed and blocked ones are skipped, and a pending one
is imported once it posts. Line: date = `postedAt` (local date), amount =
Mercury's signed `amount` (dollars → cents exactly; a value with more
decimals than the currency has is refused, never rounded), payee =
`counterpartyName`, memo = `bankDescription` · `note` · `externalMemo`,
FITID = the Mercury transaction id. The cursor per account is the newest
`postedAt` seen and the oldest still-pending creation date; the next sync
asks from the earlier of *last posted − 7 days* and *oldest pending* (the
overlap costs nothing: the feed dedups on FITID). Rules run on new lines.
Mercury accounts are US-dollar accounts, so only a USD company can connect
Mercury (one company, one currency).

### Stripe: the accounting model

The Stripe balance is money the business holds at Stripe, so it is a
**bank-type clearing account** ("Stripe (USD)") whose statement is Stripe's
balance history (`GET /v1/balance_transactions`, newest first, pages
through `starting_after`, `created[gte]` from the cursor, sources expanded;
posted oldest first). Every balance transaction becomes feed lines on that
account with FITID = the `txn_…` id (its fee: `txn_…:fee`), so the
account's balance **equals Stripe's available + pending balance** and a
second sync imports nothing.

| Stripe `type` | Feed lines on the clearing account | Posted automatically |
| --- | --- | --- |
| `charge`, `payment` | +gross; −fee | the fee → Merchant Fees (*autoPostFees*); a payment whose metadata names an invoice **of this company** → a customer payment applied to it, deposited to the clearing account and matched to the gross line (*autoApplyInvoices*) — the invoice shows Paid. Otherwise the sale waits for review (rules apply). |
| `refund`, `payment_refund` | −amount (a fee line if any) | nothing: review |
| `payout` | −amount | a **Transfer** clearing → payout account (reference = the `po_…` id), matched to the line (*autoPostPayouts*) |
| `stripe_fee`, `stripe_fx_fee`, `tax_fee`, `application_fee` | −amount | → Merchant Fees (*autoPostFees*) |
| `adjustment` (disputes), `payout_failure`, anything else | ±amount (+ fee line) | nothing: review |

**Payouts are never income twice.** The payout transfer's bank-side line
waits uncleared; the bank's own deposit (Mercury line, payee or memo
containing "Stripe", same amount, dated within 7 days of the payout) is
**matched** to it — whichever arrives first. Both import orders are tested.

**Invoices paid online.** A charge carries `qindabooks_invoice` (document
id), `qindabooks_company` (the company uuid) and `invoice_number` in its
metadata when it came from a QindaBooks payment link (Stripe copies a
payment intent's metadata onto its charge). A company uuid that is not
this file's is **never** looked up — the payment waits for review with a
note, because businesses never mix. An invoice that is no longer open is
reported the same way. The payment applies up to the open balance; any
excess stays as customer credit.

A posting the books refuse (a closed period, say) leaves the line in the
feed and says why; the import and its postings commit together, so a crash
never leaves a line imported but unposted. Multi-currency is out of scope:
a balance transaction in another currency is reported and skipped.

### Payment links

`createPaymentLink(invoiceId)` makes a Stripe **Price** (`unit_amount` =
the invoice's open balance, the company currency, inline `product_data`
"Invoice INV-1001 — <company>") and a **Payment Link** for it with
`metadata` and `payment_intent_data[metadata]` = {`qindabooks_invoice`,
`qindabooks_company`, `invoice_number`}, `payment_intent_data[description]`
and `restrictions[completed_sessions][limit]=1` (one completed payment per
link). Each request carries an **Idempotency-Key**
`qindabooks-<uuid>-inv<id>-<amount><currency>-price|link`, so a retried
request returns the same objects. Payment Links rather than Checkout
Sessions because a Checkout Session expires within a day and an invoice
link must work until the customer pays; a Payment Link does not expire.
The URL is stored (`payment_links`) and is **current** while its amount
equals the open balance: `document(id).paymentLinkUrl`, the invoice PDF
("Pay online: <url>" under the payment instructions) and the e-mail body
show only a current link. Asking again for the same amount reuses it
without a request; after a partial payment a new link is made for the new
balance.

### QML API (BooksService, M48)

| Call | Result |
| --- | --- |
| `connections()` | `[{provider ("mercury" \| "stripe"), name, status (disconnected \| connected \| syncing \| error \| needsToken), lastSync, lastError, summary}]` |
| `connectMercury(token)` | `{ok, error, accounts: [{id, name, kind, lastFour, balance, balanceText, currentBalance, currentBalanceText}]}` — waits for Mercury (input blocked, ≤ 30 s) |
| `linkMercuryAccount(mercuryAccountId, booksAccountId)` | `{ok, id, error}`; 0 creates "Mercury Checking ••1234", < 0 unlinks |
| `mercuryLinks()` | `[{mercuryAccountId, name, kind, lastFour, booksAccountId, booksAccountName, linked}]` |
| `connectStripe(key)` | `{ok, error, accountName, livemode}` |
| `stripeSettings()` / `saveStripeSettings(map)` | `{connected, clearingAccountId/Name, feeAccountId/Name, payoutAccountId/Name, autoPostFees, autoPostPayouts, autoApplyInvoices, accountName, livemode, currency}`; save takes any of the ids and flags |
| `syncConnections()` | requestId → `connectionSyncFinished(requestId, {ok, imported, applied, matched, posted, errors[]})` |
| `disconnect(provider)` | `{ok, error}` |
| `createPaymentLink(invoiceId)` | requestId → `paymentLinkFinished(requestId, {ok, url, error, reused})` |
| properties / signals | `syncingConnections`, `connectionsChanged()` |

BooksHost adds `booksSyncBankAction` ("Sync Mercury & Stripe", F5,
`refresh-cw`) and `booksConnectionsAction` ("Connections…", `link`, emits
`connectionsRequested()`), first in the Banking menu; the dialog is
`qml/ConnectionsDialog.qml`.

## Out of scope — plainly

Payroll (its own milestone, M71, which extends the employees list); inventory
quantities and COGS from stock; job costing (a bill line's customer:job is
kept, not posted), billing rate levels and billable expenses; multi-currency and
currency revaluation (one company, one currency); online bank-feed
connections other than Mercury and Stripe (every other bank's statements
are imported from the files it lets you download); e-filing of tax returns and 1099s (vendors carry a 1099 flag
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
  Company Settings… / Close the Books…; Sales, Expenses, Time (M68),
  Banking, Accounting and Reports menus; View ▸ the seven workspaces,
  Ctrl+1…7 — Time is the seventh, M68).
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
| Reports | `reportCatalog()` (with the option words each report takes), `runReport(id, options)` (the `ReportTable` as a map: typed columns, rows with kind/depth/key/cells/drill), `drillDown(drill)`, `exportReport(id, options, url, "pdf" \| "csv" \| "xlsx")`, `rangePreset(name)` (fiscal-year aware) |
| Dashboard | `kpis(asOf)` (MTD/YTD income, expenses, net; open/overdue invoices, unpaid bills, cash), `monthlyTrends(months)`, `expenseBreakdown(from, to)`, `ratios(asOf)` (value text, definition, `good`) |
| Connections (M48) | `connections`, `connectMercury`, `linkMercuryAccount`, `mercuryLinks`, `connectStripe`, `stripeSettings`, `saveStripeSettings`, `syncConnections` (→ `connectionSyncFinished`), `disconnect`, `createPaymentLink` (→ `paymentLinkFinished`) — see [Bank and payment connections](#bank-and-payment-connections-mercury-stripe); `document(id).paymentLinkUrl` |
| Statements, logo (M63) | `statement(customerId, from, to)` → `{ok, customerName, email, from, to, opening(Text), closing(Text), lines[{date, number, kindLabel, description, amount(Text), balance(Text)}], aging[{label, amountText}]}` (an open period is the fiscal year to date), `exportStatementPdf`, `emailStatement` (the statement PDF to QindaMail as `--attach`, like invoices), `setCompanyLogo(url)` (refused unless `QImage` decodes it — what prints — and ≤ 4 MB), `removeCompanyLogo()`, `companyLogo()` (a `data:` URL for the preview) |
| Recurring (M63) | `recurringTemplates()` rows also carry `remaining`, `scheduleText`, `statusText`; `recurringTemplate(id)` adds the body as `document`; `saveRecurring` edits an existing id (`nextDate`, invoices still to come in `maxOccurrences`; the service passes an unchanged schedule back as stored so nothing re-bases), `setRecurringActive`, `deleteRecurring` |
| Budgets, sales tax (M63) | `currentFiscalYear()`, `budget(fiscalYear)` (every active income/expense account, twelve month texts), `saveBudget({fiscalYear, rows})` (all parsed before anything is written), `budgetSpread(annual)` / `budgetTotal(months)` (exact splits and sums for the dialog); `salesTaxOwed(asOf)` (per agency: collected − paid), `recordSalesTaxPayment({agencyId, bankAccountId, amount, date, memo})` |
| QuickBooks (M63) | `previewIif(url, options)` (the whole import, rolled back) and `importIif(url, options)` (backup copy, then one batch); options `{accountMap: {name: accountId}, autoApply, skipRefused}`; answer `{ok, committed, error, backupPath, posted, postedText, lists[{key, label, added, existing, skipped}], transactions[{type, count, documents, journal, duplicates, refused}], refused[{firstLine, lastLine, type, reason}], unmapped[{name, lines, linesText}], notes[]}` |
| Purchase orders (M68) | `saveDocument`/`document` with kind `purchaseOrder` (`dueDate` = expected, `shipTo`, `notes` = vendor message, lines with `customerId`, `closed`; `document()` adds each line's `received`/`remaining` and `closedByHand`; the balance is what is left to receive); `documents({kind: "orders", status: open \| partiallyReceived \| closed})`; `orderLines(orderId)`, `openOrderLines(vendorId)` (`[{lineId, orderNumber, itemName, description, customerName, ordered, received, remaining, rateText, remainingText, closed}]`), `receiveOrderLines([{lineId, quantity}])` → `{ok, error, lines}` (bill-editor lines with `poLineId` and `orderNumber`), `billFromOrder(orderId)` → `{ok, draft}`, `setOrderClosed`, `setOrderLineClosed`; vendor rows of `parties("vendor")` add `openOrders`, `toReceive(Text)`; `exportDocumentPdf`/`emailDocument` take an order |
| Employees and time (M68) | `employees(includeInactive)`, `employee(id)`, `saveEmployee({id?, name, email, phone, address, notes, rate (text, "" = none), active})`, `setEmployeeActive`; `workers()` → `[{key ("employee:<id>" \| "vendor:<id>"), name, kindLabel}]`; `timeEntries({worker, customerId, from, to, status: all \| unbilled \| billed \| notBillable})`, `timeEntry(id)` (`{id, date, worker, workerName, customerId, customerName, itemId, itemName, minutes, duration, billable, status, statusLabel, notes, invoiceId, invoiceNumber}`), `saveTimeEntry({id?, date, worker, customerId, itemId, duration (text), billable, notes})`, `deleteTimeEntry`; `timesheet(worker, dateIso)` → `{weekStart, days[7], rows[{customerId, itemId, billable, notes, cells[7]{entryId, duration, billed}, totalText}], dayTotals[7], totalText}` and `saveTimesheet({worker, weekStart, rows})`; `unbilledTime(customerId)`, `timeInvoiceLines(customerId, entryIds)` → `{ok, lines}` (invoice-editor lines with `timeEntryId`) |
| Check queue and alignment (M68) | `saveDocument` of a check with `toPrint: true`, `payBills({…, toPrint: true})`; `checkQueue(bankAccountId)`, `nextCheckNumber(bankAccountId)`, `setToPrint(id, on)`, `assignCheckNumbers(bankAccountId, ids, firstNumber)` → `{ok, checks[{id, number, payeeName, amountText}]}`, `confirmCheckPrint(misprintedIds)`, `takeBackChecks(bankAccountId, ids)`; `checkPrinters()` (this computer's printers and those with a stored alignment), `checkAlignment(printer)` → `{printer, x, y, unit, description}`, `saveCheckAlignment({printer, x, y, unit: in \| mm})`, `exportAlignmentTestPdf(printer, url)` |
| Housekeeping | `backupNow()` (timestamped copies in `$XDG_DATA_HOME/qindaoffice/books/backups`, newest 20 kept), `backupTo(url)`, `exportIif`, `exportJournalCsv`, `exportListCsv(kind, url)`, `importListCsv(kind, url)` → `{ok, error, kind, added, skipped, errors[]}` (M63: `kind` is `customers`, `vendors`, `items` or `accounts`; the singular party kinds and obvious synonyms are accepted; refused rows come back as `row N: …` even when others were added) |

**Printing the current source.** Print, Print Preview and File ▸ Export
PDF render whatever the QML named with
`BooksHost.setPrintSource("report", reportId, options)` (the report on
screen), `setPrintSource("document", id)` (the selected invoice,
estimate, sales receipt or credit memo) or — M63 —
`setPrintSource("statement", customerId, {from, to})` (the statement the
Customer Statements dialog shows) or `setPrintSource("check", id)` (M63:
the selected check or bank-paid bill payment, on voucher stock; a
`"document"` source naming such a payment becomes `"check"`, so Print on a
selected check prints the check). The host builds the value when
the job is created and hands office_quick one `PrintRenderCallback` —
books_io's painter wrapped by `asPrintCallback<QPrinter>` — so paper,
preview and PDF are the same pages. `printLandscape` says when a report
reads better wide. With no source the print commands are disabled.

**Reached since M63** (D-071). *File ▸ Company Logo…*
(`booksCompanyLogoAction`, `LogoDialog.qml`), *Sales ▸ Customer
Statements…* (`booksStatementsAction`, `StatementDialog.qml`: customer,
period presets, the activity with running balance, aging, Preview / Print
through the standard commands, Save PDF, E-mail), *Sales ▸ Recurring
Invoices…* (`booksRecurringAction`, `RecurringDialog.qml` with
`RecurringEditor.qml`: new, edit, pause/resume, delete, create what is due),
*Accounting ▸ Budgets…* (`booksBudgetsAction`, `BudgetDialog.qml`: a fiscal
year per account, a year's amount split exactly or month by month, a jump to
Budget vs. Actual) and *Accounting ▸ Pay Sales Tax…*
(`booksSalesTaxPaymentAction`, `SalesTaxPaymentDialog.qml`). Each is also on
the ribbon (File, Home, Accounting) and, where a workspace shows the
subject, a button there (Sales: Recurring…, Statements…; Accounting:
Budgets…, Pay sales tax…). *File ▸ Import and Export ▸ Import from
QuickBooks (IIF)…* (`booksImportIifAction`, `IifImportDialog.qml`) shows the
preview — lists added or already here, each QuickBooks type as documents or
journal entries, what is already in the books, the accounts to map, the
refused transactions by line — and imports. *Reports ▸ Export Report as
Excel…* (`booksExportReportXlsxAction`, also on the Reports ribbon tab and
as "Excel" beside PDF and CSV in the report's option bar) writes the report
on screen as `.xlsx`. *Expenses ▸ Print Check…* (`booksPrintCheckAction`,
also in the Home ribbon's Document group) prints the selected check or
bill payment; it is enabled only while the selection is one, the check
editor shows the memo that prints on it, and its Print and Save PDF… give
the check page (`exportDocumentPdf` writes a check as its check).

**Reached since M68** (D-077). *Expenses ▸ Purchase Order…*
(`booksNewPurchaseOrderAction`), *Create Bill from Order*
(`booksCreateBillFromOrderAction`, enabled for a selected order with
something to receive; emits `billFromOrderRequested(orderId)`), *Checks to
Print…* (`booksPrintChecksAction`, `printChecksRequested()`) and *Check
Printer Alignment…* (`booksCheckAlignmentAction`,
`checkAlignmentRequested()`); a new **Time** menu: *Time Activity…*
(`booksTimeActivityAction`, Ctrl+Shift+T, `timeActivityRequested(0)`),
*Weekly Timesheet…* (`booksWeeklyTimesheetAction`, `timesheetRequested()`)
and *New Employee…* (`booksNewEmployeeAction`,
`employeeDialogRequested(0)`); the **Time** workspace
(`booksViewTimeAction`, Ctrl+7, view key `"time"`). On the ribbon: the
order command in Home's expense split menu, a **Purchases** tab (Purchase
orders, Checks) and a **Time** tab (Enter time, People), and the Time
workspace in View. `BooksHost.setPrintSource("checks", [ids])` prints a run
of checks and `setPrintSource("alignment", printerName)` the alignment test
page — both shifted by the alignment of the printer the job goes to; a
`"document"` source naming a purchase order prints the order form.

**E-mailing an invoice.** `emailDocument(id)` writes the form's PDF to
`$XDG_CACHE_HOME/qindaoffice/books/outbox/<number>.pdf` and starts
`qindamail --attach <pdf> mailto:<customer>?subject=Invoice INV-1001 from <company>&body=…`
(M48): QindaMail opens a composer draft with the PDF already attached
(`attached: true` in the result). When the invoice has a current Stripe
payment link the body carries "Pay online: <url>" above the payment
instructions.

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
M68 (D-077): `tst_books_orders`, `tst_books_time`, `tst_books_checks` and
the v3 → v4 migration in `tst_books_company`; `tst_books_io_orders` (the
purchase-order form, IIF `PURCHORDR`, durations in CSV and Excel) and the
alignment case of `tst_books_io_checks`; `tst_qindabooks_m68` and
`tst_qindabooks_m68_dialogs` — see
[verification](verification.md#m68-books-gates-d-077).
`tests/books_connect/` (M48): `tst_books_connect_mercury`,
`tst_books_connect_stripe` against a fake Mercury/Stripe API on 127.0.0.1
(`fake_bank_api.h`), plus `tst_qindabooks_connections` — see
[verification](verification.md#m48-gates-mercury-and-stripe-connections).
