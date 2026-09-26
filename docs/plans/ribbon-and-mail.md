<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Plan: the command ribbon, the finished toolkit, QindaMail and QindaBooks (M40–M47)

Status: in progress (2026-09-25). Owner: the suite. This page is the shared
brief every lane of this work reads; the rules in `AGENTS.md` (this repo)
and in QindaTK's `AGENTS.md` apply unchanged and outrank anything here.

## Why

1. The suite was converted to QindaTK (M27–M31) when the toolkit had ~70
   controls. The finished toolkit (`dev-libs/qindatk-0.1.0-r6`, QindaTK
   `88c2460c`) has ~300, including `Ribbon`, `CommandPalette`, `Toast`,
   `NavigationRail`, the mail/calendar/task views and the SMTP/IMAP/CalDAV
   providers over `LocalProductivityStore`. The suite should build on it.
2. The flat wrapping strips (`ToolBarFromModel`) carry every command at
   one weight. A tabbed command ribbon groups them by task with a clear
   primary action per group — our own design language, not a copy of
   Microsoft's (see *Ribbon design*).
3. The suite has no mail or calendar. QindaMail adds both, Outlook-shaped
   (folders · list · reading pane; day/week/month calendar), over the
   toolkit's store and providers, with the account kinds people actually
   have: Gmail (OAuth), any IMAP/SMTP, POP3/SMTP, CalDAV (generic, iCloud,
   Fastmail, Nextcloud…), Google Calendar, iCalendar files and feeds, and
   Exchange / Microsoft 365 calendars.

## Milestones

| M | Scope | Repo(s) |
| --- | --- | --- |
| M40 | Ribbon v2 in QindaTK; `RibbonFromModel` + `CommandSearch` in `office_quick`; Write/Calc/Show/Note move from strips to ribbons; build against the finished toolkit | QindaTK, QindaOffice |
| M41 | Toolkit protocol lanes: `Pop3MailProvider`; IMAP folder list, APPEND and modified UTF-7; `IcsCalendarProvider` (files + feeds) | QindaTK |
| M42 | `CalendarGridView` (day / work week / week / month) | QindaTK |
| M43 | `office_mail`: accounts + presets + autoconfig, OAuth 2.0 (PKCE, loopback), keyring secrets, Exchange EWS and Microsoft Graph calendar adapters | QindaOffice |
| M44 | QindaMail app (`apps/qindamail`, `org.qindaqt.QindaMail`), launcher card, packaging | QindaOffice |
| M45 | `books_engine`: double-entry ledger in a SQLite company file, sales/purchases, banking, tax, reports | QindaOffice |
| M46 | `books_io`: invoice/statement PDF, CSV/OFX/QFX/QIF bank import, CSV/IIF export, backups | QindaOffice |
| M47 | QindaBooks app (`apps/qindabooks`, `org.qindaqt.QindaBooks`), launcher card | QindaOffice |
| M48 | `books_connect`: Mercury bank sync, Stripe balance history + payouts + payment links, keyring tokens; `qindamail --attach` | QindaOffice |
| M49 | QindaBase (Access): a `.qbase` SQLite database with tables, queries, forms and reports | QindaOffice |
| M50 | QindaDiagram (Visio): flowcharts, org charts and network diagrams on the toolkit's diagram canvas | QindaOffice |
| M51 | QindaPlan (Project): tasks, dependencies, working calendars, Gantt, resources, baselines | QindaOffice |

## Ribbon design (M40, D-041 here; QindaTK D-297)

Office's ribbon is the reference for *function* (task tabs, labelled
groups, a primary command per group, contextual tabs, key tips, collapse);
the look is QindaTK's:

- **Tabs are a pill row**, not file-folder tabs, left-aligned; a
  **command search** field sits at the right of the same row (opens a
  `Tk.CommandPalette` over the window's `ActionModel`), then the
  collapse toggle.
- **Groups are soft cards** (`panelAlt` fill, `radius.md`, no divider
  rules) with the caption as an `Overline` under the commands.
- **Two command weights**: `large` (icon over a one-line label, the
  group's primary command) and `small` (icon + label, or icon-only)
  stacked **two rows** high — Office stacks three; two keeps the band at
  `2 × size.control + gaps` and reads calmer.
- **Modes**: `expanded` (default) and `compact` (one row of small
  buttons per group, captions hidden) — *View ▸ Compact Ribbon*, persisted
  per app in Preferences. Collapse hides the band until a tab is clicked.
- **Squeeze** (AGENTS.md verification rule): the ribbon never hides a
  command. As the window narrows, groups first drop to small buttons, then
  the group row **wraps**; at 520 px every command of the current tab has
  a visible button inside the ribbon. The squeeze tests iterate every tab.
- **Rule 4/5 hold**: the ribbon is a third *view* of the same ids. Items
  name `ActionEntry` ids; the menu tree stays the source of the panel
  export; the local menu bar and its fail-closed logic are unchanged.
- **Contextual tabs**: Write *Table* (cursor in a table), Show *Shape*
  (selection non-empty), Calc *Chart* (chart selected) — the host exposes
  `ribbonContexts` (a `QStringList`).

QindaTK `Ribbon` v2 adds (backward compatible): per-item `size`
(`large`/`small`/`icon`), two-row stacking, `itemDelegate` (a host
Component that receives `item`, `groupId`, `tabId`, `size`), `trailing`
(an Item slot at the right of the tab row), `mode` (`expanded`/`compact`),
wrap under squeeze, `checked`/`enabled` from the item data when no
registry is used, and a `menu` item kind (button + dropdown of items).

## Mail/calendar architecture (M41–M44)

```
apps/qindamail  → office_quick, office_mail
office_mail     → office_core, QindaTK (LocalProductivityStore, providers), Qt Network, qt6keychain
```

- **One main window per process** is a `MailHost : DocumentHost` with no
  `Core::Document` (Save stays disabled, D-011); New Window opens another
  main window over the same store. Compose windows are secondary
  `Window`s owned by the main window's QML — one draft per window.
- **Store**: one `Tk.LocalProductivityStore` file per process at
  `$XDG_DATA_HOME/qindaoffice/mail/store.json`, attachments in
  `Tk.LocalAttachmentStore` under `$XDG_DATA_HOME/qindaoffice/mail/attachments`.
  Account configuration (no secrets) in
  `$XDG_CONFIG_HOME/qindaoffice/mail/accounts.json`.
- **Secrets** only in the desktop keyring through QtKeychain, service
  `qindaoffice-mail`, key `<accountId>/<service>` (`imap`, `smtp`,
  `pop3`, `caldav`, `ews`, `oauth-refresh`). Never on disk, never in logs.
- **Result contract**: every protocol object — toolkit provider or
  `office_mail` adapter — follows `docs/productivity-providers.md` in
  QindaTK: `requestId` in, one `…Finished(requestId, result)` signal out,
  `result.state` ∈ `accepted | offline | rejected | conflict`,
  `retryable`, redacted `transcript`.

### Account kinds

| Kind | Mail in | Mail out | Calendar | Sign-in |
| --- | --- | --- | --- | --- |
| `gmail` | IMAP `imap.gmail.com:993` | SMTP `smtp.gmail.com:465/587` | CalDAV `https://apidata.googleusercontent.com/caldav/v2/<cal>/events/` | OAuth 2.0 (XOAUTH2 / bearer) or app password (mail only) |
| `imap` | IMAP | SMTP | optional CalDAV | password |
| `pop3` | POP3 | SMTP | — | password |
| `caldav` | — | — | CalDAV | password or bearer |
| `ics` | — | — | read-only feed (`https:`/`webcal:`) or imported file | none |
| `exchange` | optional IMAP | optional SMTP | EWS (`https://<host>/EWS/Exchange.asmx`) | password (Basic over TLS) or bearer |
| `microsoft365` | IMAP `outlook.office365.com:993` | SMTP `smtp.office365.com:587` | Microsoft Graph `/me/calendars/<id>/calendarView` | OAuth 2.0 |

OAuth client IDs are **not shipped** (the toolkit ships none, D-189 there;
we have none to ship). They come from, in order: the account's own
settings, `$XDG_CONFIG_HOME/qindaoffice/mail/oauth-clients.json`,
`/usr/share/qindaoffice/oauth-clients.json` (a distributor's file). The
account dialog says plainly when none is configured and offers the
app-password path for Gmail mail.

## Bookkeeping (M45–M47, D-045)

The user runs more than one business and keeps their records strictly
apart, so **one company = one `.qbooks` file = one window** (rule 1 fits a
bookkeeping app exactly). QuickBooks is the reference for *scope*, not for
look or file format:

- **Ledger**: a chart of accounts (asset/liability/equity/income/expense
  with sub-accounts and a starter template), balanced double-entry journal
  transactions that are immutable once posted (corrections are reversing
  or adjusting entries; void keeps an audit trail), period close with a
  lock date, fiscal year settings, multi-line journal entries.
- **Money** is integer minor units (`qint64` cents) with an ISO 4217
  currency per company; rounding is explicit (banker's rounding only where
  a tax rule says so). No floating point touches an amount.
- **Sales**: customers, products/services, estimates → invoices, sales
  receipts, received payments applied to invoices, credit memos, refunds,
  statements, recurring invoices, invoice numbering.
- **Purchases**: vendors, bills, bill payments, expenses/checks, vendor
  credits, 1099-style vendor flags.
- **Banking**: bank and credit-card registers, transfers, deposits,
  imported bank lines matched or categorised by rules, reconciliation
  against a statement ending balance with a reconciliation report.
- **Tax**: sales-tax rates/agencies, tax on invoice lines, liability report.
- **Reports** (all from the ledger, as-of/period parameters, comparative
  columns, drill-down to transactions, CSV/PDF export): Profit and Loss,
  Balance Sheet, Cash Flow (indirect), Trial Balance, General Ledger,
  Journal, AR and AP aging (summary + detail), Customer/Vendor balances,
  Sales by customer/item, Expenses by vendor/category, Sales-tax
  liability, Budget vs actual, plus financial ratios and trend charts on
  a dashboard.
- **Storage**: SQLite with `PRAGMA foreign_keys`, WAL off (single file),
  schema versioned with forward migrations, every posting in one
  transaction, an audit log table, and `VACUUM INTO` backups.

## Scope boundary (user decision, 2026-09-25)

Publisher and Clipchamp are NOT built here: QindaStudio Paper / Sloom
Studio Paper cover desktop publishing and the Studio video workspaces
cover video. The suite adds Access, Visio and Project equivalents
(M49–M51), each on the QindaTK components that already exist for them
(forms/reports/query engine; diagram canvas; Gantt/resource planning).

## Lanes and branches

| Lane | Branch / worktree | Deliverable |
| --- | --- | --- |
| TK-ribbon | QindaTK `work/ribbon-v2` | Ribbon v2 (lead) |
| TK-pop3 | QindaTK `work/mail-pop3` | `Pop3MailProvider` + tests + docs |
| TK-imap-ics | QindaTK `work/mail-folders-ics` | IMAP `listMailboxes`/`append`/mUTF-7, `IcsCalendarProvider` + tests + docs |
| TK-calgrid | QindaTK `work/calendar-grid` | `CalendarGridView` + tests + docs |
| OM-accounts | QindaOffice `work/mail-accounts` | `office_mail`: accounts, presets, autoconfig, OAuth, secrets + tests |
| OM-exchange | QindaOffice `work/mail-exchange` | `office_mail`: EWS + Graph calendar adapters + tests |
| OB-engine | QindaOffice `work/books-engine` | `books_engine` + `books_io` + tests |
| OB-connect | QindaOffice `work/books-connect` | `books_connect` + service wiring + `qindamail --attach` + tests (M48) |
| app | QindaOffice `main` (lead) | ribbons in the four apps, QindaMail, QindaBooks |

Decision numbers are reserved so parallel lanes never collide: QindaTK
D-297 ribbon, D-298 POP3, D-299 IMAP folders/APPEND/mUTF-7, D-300 ICS
feeds, D-301 calendar grid; QindaOffice D-041 ribbon, D-042 mail app
shape, D-043 hosted accounts and OAuth, D-044 Exchange/Graph calendars,
D-045 bookkeeping model (company file, integer money, immutable postings),
D-046 bookkeeping I/O, D-047 bookkeeping app, D-048 bank/payment
connections, D-049 QindaBase, D-050 QindaDiagram, D-051 QindaPlan.

## Gates

- Every lane: builds warning-clean under its repo's dev preset; its tests
  green offscreen; files ≤500 non-blank lines; SPDX headers; docs rows;
  its decision entry; no credentials in results, transcripts, logs or
  files (tests assert it); TLS or loopback only for credentials.
- Build policy (both repos, non-negotiable): never override the configured
  `MAKEOPTS` (`-j24 -l24`); plain `cmake --build` / `ninja`, no `-j`,
  `--parallel`, `CMAKE_BUILD_PARALLEL_LEVEL` or job pools.
- M40: fixtures regenerated deliberately, grabs of all four apps at 1280
  and 520 looked at, squeeze over every tab, panel-export tests unchanged.
- M44: QindaMail fixtures/grabs, menu-export validator, account flows
  against local fake servers (no real network in tests), the launcher card.

## Delivery (user request, 2026-09-25)

When every milestone is finished: publish QindaTK and QindaOffice to their
hubs, update the QindaGentoo overlay ebuilds (`dev-libs/qindatk`,
`gui-apps/qindaoffice`) with every new dependency declared (Qt Sql +
QSQLITE, qtkeychain, Qt Network, poppler-qt6, …), and install through
Portage on **qinda-top** (the laptop): sync the overlay, fast-forward the
laptop's `~/git/*.git` mirrors, then `emerge` — never overriding MAKEOPTS.

**Delivered 2026-09-26** as `dev-libs/qindatk-0.1.0-r7` (QindaTK 6cd0482d)
and `gui-apps/qindaoffice-0.1.0_p20260925` (QindaOffice 0b86d23, tarball
from `tools/make-dist.sh`, published with `qinda-sync publish`; overlay
bd5090a, pins 89fcb2b). Installed on qinda-top with `qinda-sync packages
'=dev-libs/qindatk-0.1.0-r7' '=gui-apps/qindaoffice-0.1.0_p20260925'`
(quote the atoms: zsh expands a leading `=`); every installed app builds
its command set headless (`--list-commands`, offscreen). Lessons paid for:

- AGENT-NOTE: git-r3 fetches the laptop mirror's `HEAD`, and `qinda-sync`
  only adds the pinned commit on a side branch — fast-forward
  `~/git/QindaTK.git`'s `main` from qinda first, or the unpack phase dies
  with "Referencing <commit> failed".
- Run the install detached on the laptop (`setsid nohup … > log`): emerge
  dies on its next status write when the ssh session that started it goes
  away, and the laptop farms compiles to qinda over distcc (QindaTK units
  take ~1.8 GB each), so qinda's memory is shared with its own builds.
- Never replace QindaTK while another emerge on the same host is building
  a package that compiles against it (qindaqt-desktop, qindastudio, …).
