<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaBase

QindaBase is the suite's desktop database — what Microsoft Access is to
Office: tables, queries, forms and reports for the records a small office
keeps (donors and gifts, volunteers and shifts, clients and projects,
stock). One database is one `.qbase` file and one window (rule 1). It
pairs with QindaBooks without sharing its file format: the books stay in
`.qbooks` files, the records in `.qbase` files. Decisions: [D-049](decisions.md#d-049-qindabase-one-sqlite-file-exact-money-every-change-previewed-and-undoable);
Excel import, the complete Access import and file fields: [D-074](decisions.md#d-074-qindabase-excel-import-the-complete-access-import-file-fields) (M65);
Access's validation rules, input masks, crosstab and make-table queries, the
Access SQL view, macros, subforms and buttons, engine-evaluated reports,
mailing labels and XML export: [D-089](decisions.md#d-089-qindabase-accesss-queries-forms-and-reports) (M80).

| Part | Where |
| --- | --- |
| Engine (Qt Core + SQLite C API) | `libs/base_engine` |
| App (host, service, QML) | `apps/qindabase` |
| Tests | `tests/base_engine`, `tests/qindabase` |
| Icon, desktop file, MIME | `data/icons/hicolor/scalable/apps/org.qindaqt.QindaBase.svg`, `data/org.qindaqt.QindaBase.desktop`, `application/x-qindabase` in `data/mime/qindaoffice.xml` |

## The file

A `.qbase` file is an ordinary SQLite 3 database — any SQLite tool can
read it. `PRAGMA application_id` is `0x51424153` ("QBAS", what the MIME
magic matches at offset 68) and the journal mode is `DELETE`, so it stays
one copyable file. The user's tables are ordinary tables. QindaBase's own
state is in three system tables:

| Table | Holds |
| --- | --- |
| `qb_meta(key, value)` | `schemaVersion` (1), `created`, `template`, `description`, `currency.symbol`, `currency.decimals`. Unknown keys are kept. A newer `schemaVersion` opens read-only. |
| `qb_objects(id, kind, name, definition, description, created, modified)` | Saved queries, forms, reports and (M80) macros as JSON (`kind` = `query` \| `form` \| `report` \| `macro`; names unique per kind, case-blind). Unknown JSON members survive a load–edit–save. |
| `qb_fields(table_name, field_name, type, caption, description, lookup_table, lookup_field, lookup_display, validation_rule, validation_text, input_mask)` | What SQLite cannot say about a column: its QindaBase type (AutoNumber), caption, description, lookup and (M80) validation rule, validation text and input mask. The row with an empty `field_name` holds the table's own record rule. An older file gains the three M80 columns in its first plan that needs them. |

**Plain SQLite files** (`.sqlite`, `.db`, …) open as they are: their
tables show and edit, nothing is written to the file until the user
agrees to add the three system tables (the first time a design, caption
or lookup has to be saved). Their `application_id` and `user_version`
are never changed.

## Field types

| Type | Declared | Stored as | Notes |
| --- | --- | --- | --- |
| AutoNumber | `INTEGER PRIMARY KEY AUTOINCREMENT` | rowid | numbers are never reused |
| Short text | `TEXT` | text | at most 255 characters |
| Long text | `LONGTEXT` | text | |
| Whole number | `INTEGER` | 64-bit integer | |
| Decimal number | `REAL` | binary double | for measurements; **use Currency for money** |
| Currency | `CURRENCY` | integer **minor units** (cents) | parsed digit by digit from text; never floating point in the engine |
| Yes/No | `BOOLEAN` | 0 / 1 | accepts yes/no/true/false/y/n/1/0 |
| Date | `DATE` | `yyyy-MM-dd` text | accepts ISO, US m/d/yyyy, "Mar 14, 2026" |
| Date & time | `DATETIME` | `yyyy-MM-dd HH:mm:ss` text | accepts AM/PM |
| File or picture | `FILE` | BLOB, at most 32 MB | M65: the bytes only (see *File fields*) |

The declared type is the source of truth, so other tools see sensible
types. Columns of foreign files map by SQLite affinity (`VARCHAR(40)` →
Short text, `DECIMAL(8,2)` → Decimal number, `TIMESTAMP` → Date & time,
`BLOB` → read-only). A foreign file's own declared spelling survives a
redesign. Currency uses the database's symbol and decimal places (0–4,
Database Properties); the number of decimals is fixed once a Currency
field holds amounts, because stored amounts are minor units.

**File fields** (`base_file_field.h`). A record's picture or document is
kept inside the file; a saved copy is named after its record
("Members-12-Photo.png") because the original name is not kept, and the
kind (PNG, JPEG, PDF, ZIP, Office 97–2003, text, …) comes from the bytes'
signature. Reads never load the bytes: the datasheet shows "PNG image ·
24 KB" with an icon from `length()` and the first 32 bytes (incremental
blob I/O), and the bytes are read only to preview or save them. Insert
File…, Save File As… and Remove File (Records menu, Home ▸ File field, the
panel's own buttons) act on the datasheet's current cell or a form's file
field, each one undo step; a picture Qt can decode previews as a thumbnail
in a panel beside the datasheet and under a form (QindaTK's grid cells
draw text and an icon only). A file field can be no key, index, default,
lookup or required field, filters only by having a file or not, and
converts to or from nothing but a foreign `BLOB` column; exports write its
description.

**Decision on "number"**: Decimal number is REAL (binary floating point),
not text-decimal. It is what SQLite, spreadsheets and Access's Double do,
it sorts and compares numerically in plain SQL, and money — the one place
where binary fractions hurt — has its own exact type.

## Validation rules and input masks (M80)

**Validation Rule** and **Validation Text** (`base_rules.h`), as in Access:
a field's rule is criteria with the field as its left side (`> 0`,
`Between 1 And 5`, `Like "*@*.*"`, `<= Date()`), compiled like a query's
criteria — `> 0` on money is 0 minor units; the table's own rule (Table
properties) is an expression over the record's fields (`[ends] >=
[starts]`). A rule tests only its own field; it may not total records or
ask for anything. A Null passes (Required is what stops an empty field).
The database enforces them: QindaBase writes SQLite triggers
(`qbv:<table>:<field>:i|u`, `BEFORE INSERT` / `BEFORE UPDATE OF`) that raise
the validation text — or "<field> must meet its validation rule: <rule>" —
so other SQLite tools meet the same rules. Triggers rather than CHECK
constraints, because a rule may use Date(). A plan that adds or changes a
rule is refused while records break it ("2 record(s) break the validation
rule of amount (> 0)"); a rebuild or rename of the table makes the triggers
again from the design. Table Design ▸ **Test Validation Rules** counts the
saved table's records against every rule.

**Input Mask** (`base_input_mask.h`): Access's syntax — the mask, then `;0`
to store the literal characters with the data (`;1` or nothing: only what
was typed), then the placeholder (`_`); `0` digit, `9` optional digit, `#`
digit or sign, `L`/`?` letter, `A`/`a` letter or digit, `&`/`C` any
character, `>` and `<` change case, `!` fills from the right, `\x` and
`"text"` are literal, `Password` hides the text. The Input Mask Wizard's
presets (phone, Social Security number, ZIP code, Canadian postal code,
extension, short date, password) are one click away. Masks apply where
people type — datasheet cells, forms, paste — never to imports or action
queries: `303 555 1234`, `(303) 555-1234` and `3035551234` all fit
`(999) 000-0000`, and a value stored without its literals shows through the
mask. The datasheet's record bar shows the current field's mask and rule.
Masks fit text, number, money and date fields that are not lookups.

## Changes, undo and safety

Every edit commits at once (as in Access) and is one step on the
window's undo stack (Edit ▸ Undo names it):

* **Data** edits — a cell, a new record, a delete, a paste, an import, an
  action query — run inside a savepoint under SQLite's session extension;
  the recorded changeset *is* the undo step, cascaded deletes included.
  Undo and redo replay it with foreign keys off, so cascades are not
  applied twice. If the same records were changed some other way since,
  the replay is refused and the history cleared, with a message.
* **Schema** changes — table designs, relationships, SQL console DDL,
  deleting a table — are planned as ordered single statements, shown with
  their SQL, previewed against the real data in a transaction that is
  rolled back, then applied in one `BEGIN IMMEDIATE` transaction with a
  `foreign_key_check` before commit. The undo step swaps whole-database
  images (sqlite3_serialize before and after, restored through the backup
  API). Databases over 64 MB keep no images: the step is not undoable, and
  a backup is taken instead.
* **Backups**: before any change that drops or narrows data (a deleted
  field or table, a type change that can lose detail), a `VACUUM INTO` copy
  goes to `$XDG_STATE_HOME/qindaoffice/base/backups/` (newest 20 per
  database). File ▸ Back Up Database makes one on demand.
* **Integrity**: `quick_check` and a foreign-key check when a file opens
  (problems are shown on the start page; the file still opens so data can
  be exported); Database Tools ▸ Compact and Repair runs `VACUUM` then
  `integrity_check` (and ends the undo history, because `VACUUM` may
  renumber rowids of tables without an INTEGER PRIMARY KEY).
* Saving: everything is already on disk; Save is a no-op. Save As writes a
  consistent `VACUUM INTO` copy next to the target, renames it into place,
  and the window continues on the copy. A new database lives in a scratch
  file until its first Save As.

## Table design

The designer edits a field grid (key, name, type, description) and the
selected field's properties: caption, required, default value (text, a
number, `Date()`, `Now()`, or `=expression`), indexed / unique, a
**relationship** (the related table, what happens on delete: refuse,
cascade, or leave unlinked; key changes cascade) and a **lookup** (the
datasheet and forms show and accept the related record's display field).
"What Apply will do" lists the plan in words, its warnings (values that
don't fit a new type are counted and kept as they were — nothing is
silently lost), whether a backup is made, and the SQL.

How a plan is built (`base_schema_alter.cpp`): a table rename and column
renames run first as plain `ALTER TABLE … RENAME`, because only those
rewrite the references other tables, views and triggers hold; everything
SQLite cannot `ALTER` (types, required, defaults, keys, relationships,
dropped or moved fields) goes through SQLite's documented table rebuild
(`sqlite.org/lang_altertable.html` §7) with `legacy_alter_table=ON`,
converting values through `qb_convert` (the engine's own parser, so a
REAL 19.99 becomes 1999 cents exactly). The properties also carry each
field's Validation Rule, Validation Text and Input Mask (with the wizard's
presets), and the table's own record rule (M80, above). Making a field required or unique
is refused, with the count, while records would break the rule. A table
rename rewrites the saved queries, forms and reports that name it, in the
same step.

## Datasheets

A table or query result in QindaTK's `EditableGrid` over a
`DatasheetModel` (a domain source with `sourceCommits`): typing edits a
cell (validated by type, with the field named in the message), the last
row (✱) adds a record once its required fields are in, Delete clears
cells, and every commit is one undo step. Sort, filter by selection or
excluding it, a filter bar (Tk.FilterBar, AND or OR), Find, copy (tab
separated), paste (tab-separated text from any spreadsheet: existing rows
are updated in view order, extra rows appended — one step; a bad cell
names its row and nothing is kept), and a **totals row** (sum, average,
count, minimum, maximum per column — exact for tables, integer minor units
for money). Lookup fields show the related record's name and accept it.
The record bar shows "n of N", the filter state and the field's type.

**PivotTable** mode summarises the rows with QindaTK's `QueryBuilder` and
`PivotView` over `QueryController`. QindaTK's query engine sums in double
precision (its own documentation says so), so money totals in a
PivotTable are exact only to about 15 significant digits and are shown to
the cent; the datasheet totals row and saved queries total in integer
cents.

## Queries

The designer is the grid people know from Access — tables with their
fields (joins suggested from relationships; inner, all-left or all-right),
and per column: field or expression, name, total (Group By, Sum, Avg,
Count, Min, Max, Where, Expression), sort, show, criteria and an "or" row
— plus a live **SQL view** (switch to SQL to write the statement
yourself). Query types: select, update, delete, append, **crosstab** and
**make-table** (M80). The header has Access's **Return** box (Top 5, 25,
100 records or 5 %, 25 % — a percentage rounds up) and **Unique Values**.

Criteria and expressions are Access-style: `> 100`, `Between #1/1/2026#
And #3/31/2026#`, `Like "A*"`, `In (1, 2)`, `Is Null`, `Not "x"`, bare
words (`Grace Hopper`), other fields (`<= [reorder_level]`), functions
(`Year`, `Month`, `Day`, `Left`, `Right`, `Mid`, `Len`, `UCase`, `LCase`,
`Trim`, `InStr`, `Replace`, `Round`, `Int`, `Abs`, `Nz`, `IIf`, `Date()`,
`Now()`, `DateSerial`, `Sum`/`Avg`/`Count`/`Min`/`Max`) and `&`
concatenation. Literals are converted by the type of the field they meet
— `> 100` on a Currency field means 100.00 (10000 cents) — and written as
quoted SQL literals; user text is never pasted into SQL unquoted. An
unknown `[name]` is a **parameter**: the query asks for it (typed by the
field it is compared with) each time it runs. The compiled SQL uses only
core SQLite, so a saved query's SQL runs in any SQLite tool.

**Action queries** never run blind: they are first executed inside a
savepoint that is rolled back, the user is told exactly how many records
will change, and the real run is refused if that number has changed in
the meantime. The run is one undo step.

**Crosstab queries** (M80, `base_query_crosstab.cpp`): one or more Row
Headings down the side, one Column Heading across the top, one Value (Sum,
Avg, Count, Min or Max) in the cells, and Where columns to filter; a Row
Heading with a total is a row summary. The columns are the distinct values
of the heading among the rows the criteria and the parameter values select,
sorted — or the fixed Column Headings in their order. A cell without
records is empty, Count too. It runs as plain SQLite (`CASE WHEN max(…)`
per heading), so the datasheet, exports and reports read it like any select.

**Make-table queries** (M80, `base_query_maketable.cpp`): the select's
records into a new table whose fields take the query's types (money stays
Currency, dates stay dates). Running it shows its plan first; replacing an
existing table deletes it after a backup; the whole run is one undo step
(a schema step). It may not replace a table it reads, nor one other tables
point to.

**Query Parameters** (M80): the prompts declared with their types and
order (Access's `PARAMETERS`); a declared prompt is asked first and keeps
its type whatever field it meets. Undeclared `[prompts]` still work as
before.

**SQL view in Access SQL** (M80, `base_access_sql.h`): the view shows the
statement Access would show — `PARAMETERS`, `TRANSFORM … PIVOT … IN (…)`,
`SELECT … INTO`, `INSERT INTO … SELECT`, `UPDATE … SET`, `DELETE`, `TOP n
PERCENT`, `[bracketed]` names — and edits made there go back into the grid
on Design, Run or Save: WHERE and HAVING split into criteria and Or rows,
ORDER BY onto its columns. What the grid cannot hold (UNION, subqueries,
joins on several fields, `INSERT … VALUES`, an Or inside an And) is said,
with **Keep as SQLite SQL** to store the text as a hand-written query. Under
it, the SQLite statement the query runs as.

## Forms

A form is a single-record view of one table on QindaTK's `FormDesigner`:
labels, order and shown fields are the design (one undoable step each in
the designer; Save Layout stores it). QindaBase hands the designer a
*form mirror* — an empty scratch table with the same columns where lookup
fields are text, so the form shows and accepts the related record's name —
and saves records through the engine (typed, validated, one undo step):
the designer shows the engine's refusals as its validation messages. The
record bar moves first / previous / n of N / next / last, adds and
deletes. The **form wizard** picks the table, fields and title.

**M80** (`base_form_controls.h`): **lookup fields are combo boxes** of the
related records — or **list boxes**, chosen in Design with whether each
shows — above the designer's fields (QindaTK's FormDesigner draws text
fields and check boxes only); a choice on a saved record is saved at once
(one undo step), on a new record it is saved with it. **Subforms** (Form &
Report ▸ Add Subform) show the records related to this one through a
relationship whose parent is the form's table, as an editable datasheet
under the record: its link field is hidden and set on new records; a form
shows up to four. **Command buttons** (Add Button) run a saved macro or one
action (open a form, report, query or table, apply a filter, go to a
record, …) — Access's On Click. A macro's ApplyFilter narrows the records
the navigator walks (a "Filtered" chip clears it).

## Macros (M80)

A macro (`base_macro.h`, Create ▸ Macro) is a list of Access's everyday
actions run top to bottom, without VBA: **OpenForm** (a form, a where
condition, Add mode for a new record), **OpenReport** (preview or print),
**OpenQuery** (a select shows its datasheet; an action query still shows
its count and asks), **OpenTable**, **ApplyFilter** and **ShowAllRecords**
(the open form or table), **GoToRecord** (first, previous, next, last,
new), **SaveRecord**, **Requery**, **FindRecord**, **CloseWindow**,
**MessageBox**, **RunMacro** and **StopMacro**. The Macro Design tab adds,
deletes and moves actions; what keeps a macro from running (a missing form,
a where condition naming no field) is listed as it is edited. Open on a
macro runs it; a failing step stops it with its number. Where conditions
are Access expressions over the table (`[city] = "Portland" And [amount] >
100`), compiled with literals converted and quoted. Data changes go through
saved queries — there is no RunSQL.

## Reports

A report is a table or saved select query printed in groups with totals,
on QindaTK's `LocalReportDocumentModel` (paginated rich text, header row
repeated on each page, "Page n of N") with `ReportDesigner` beside it in
Design (header, footer, page bands, grouping, totals, page break per
group, detail rows). QindaBase hands the toolkit a *report snapshot*: the
source's rows (after asking for parameters) in a scratch database, lookups
as names, money as units. Column, group and total labels come from the
field captions. The preview page is paper — white with black ink — on
every theme; Print, Print Preview and Export PDF render exactly those
pages. Reports read at most 4,000 records (the toolkit's snapshot bound)
and say so when there are more. The **report wizard** picks the source,
columns, one grouping with a total, title and paper.

**M80: the engine evaluates the report** (`base_report.h`): it reads the
records once (parameters asked first), filters, sorts and groups them and
computes every total itself — currency sums and extremes in integer minor
units, averages rounded half away from zero, "—" for a group with no values
— and QindaTK lays the bands out as before (headings, detail tables with
the header row repeated, page header and footer, "Page n of N"). So every
total on screen, on paper and in the PDF is exact to the cent. Form &
Report ▸ **Group, Sort and Total** sets up to three group levels with the
totals each footer prints (Sum, Average, Count, Lowest, Highest) and a new
page per group, the sort order inside them (four keys with the groups) and
the report's own totals — each change one step of the design's history. The
report wizard offers two group levels and a sort. A report prints on its
own paper and orientation from the first page.

**Mailing labels** (M80, `base_labels.h`, Create ▸ Labels): the Label
Wizard takes a table or query, an Avery sheet (5160–8163 on US Letter,
L7159–J8160 on A4 — Word's Label Options numbers), the label's lines as
Access's prototype label shows them (`{name}`, `{city}, {region}
{postal_code}`; a line starting with `=` is an expression), a sort order,
labels already used on the first sheet, copies and the text size, and
previews the first labels. A label report opens on its sheets, painted by
the same painter that prints them and writes the PDF; empty lines close up
and long lines shrink to fit, then elide.

Money prints as money: every currency column and every sum, average,
lowest and highest of one prints with the file's symbol, decimals and
thousands grouping ("$1,293.75") — since M80 by the engine itself, which
formats what it totals. The design still carries QindaTK's report `format`
key for the designer's own preview. Until M80 the toolkit totalled in
double precision; the engine's totals are exact for any amount, like the
datasheet's totals row and queries.

## Import and export

* **CSV / text import** into a new or an existing table: the delimiter
  (comma, semicolon, tab, bar) and header row are detected; each column's
  type is proposed from its values (codes with leading zeros stay text;
  money needs a currency sign or a money word in the header; an `id`
  column of unique numbers becomes the key); the user can rename, retype
  and leave out columns. UTF-8 (BOM stripped) or Windows-1252. The import
  is all-or-nothing (the first bad row is named) unless "skip rows that
  don't fit" is on; the new table and its rows are one undo step.
* **CSV export** of what the datasheet shows (filters, sort, a query's
  parameters), RFC 4180, UTF-8.
* **Excel (.xlsx) import** (M65) through `calc_io`, the reader QindaCalc
  opens workbooks with, in the same dialog and plan as CSV: the sheet (the
  first holding data unless chosen), whether the first row holds the
  names, and each column's type **from its cells** — TRUE/FALSE → Yes/No,
  a date format → Date or Date & time, a currency format → Currency, whole
  numbers → Whole number, other numbers → Decimal number, any text → the
  CSV rules — with the first rows previewed. Formulas import as their
  values; blank rows are skipped; `.xls` is refused ("save it as .xlsx").
* **Excel (.xlsx) export** through the suite's own `calc_io` writer (the
  one QindaCalc saves with): a bold header row, frozen, numbers as numbers,
  money in units under a format showing the database's symbol, Yes/No as
  booleans, dates and date-times as date serials with a date format, file
  fields as their description — so an export imports back with the same
  types.
* **XML export for Access** (M80, `base_xml_export.h`): External Data ▸
  Export as XML writes Access's ExportXML pair — the data (`<dataroot>`,
  one element per record, Nulls left out, money in units, dates as
  `xsd:dateTime`, Yes/No as 1/0, files as base64) and its `.xsd` beside it
  with Access's annotations (`od:jetType`, `od:sqlSType`, required fields,
  AutoNumber, the primary key and indexes) — which Access's External Data ▸
  XML File imports with its structure.
* **Writing `.accdb` is not possible** with open-source tools (mdb-tools
  only reads; Jackcess needs Java): records go to Access as Excel, CSV or
  XML, and come back through the Access import below.
* **Access (.accdb/.mdb) import** (M49, completed in M65) through
  `mdb-tools` — only when its `mdb-tables` and `mdb-export` programs are on
  `PATH` (Gentoo: `app-office/mdbtools`); otherwise the command says what
  to install. With `mdb-schema` each table keeps its design: types
  (AutoNumber with Access's own numbers, Currency, Date vs Date/Time, Memo,
  OLE and binary as file fields), the primary key, required fields,
  descriptions, unique and plain indexes (several fields: kept as the
  table's own index). The relationships Access enforces follow with their
  cascades. With `mdb-queries`, saved select queries that read one
  imported table and total nothing become designer queries (prompts become
  parameters). The whole import is one undo step, and the dialog then
  shows a report: what came across, what changed on the way (a Currency
  field with fractions of a cent becomes a Decimal number), and every
  table, field, relationship and query left out with the reason —
  attachment and multi-valued fields (mdb-tools cannot read them),
  relationships Access does not enforce, joins, totals, form references
  and action queries. Access forms, reports and macros do not come across.

## Starter templates

A new database can be blank or start from a template; each has tables with
captions, relationships and lookups, saved queries, forms and a report:

| Template | Tables | Queries | Forms | Report |
| --- | --- | --- | --- | --- |
| Contacts | contacts | Contacts by Category | Contact Details | Contact Directory (by category) |
| Donors & Donations | donors, campaigns, donations | Donation Details, Totals by Donor, Donations in a Period (parameters), Donations by Method and Year (crosstab) | Donor (with a Donations subform and a New donation button), Donation Entry | Donations by Donor (totals), Donor Mailing Labels (Avery 5160) |
| Volunteers & Shifts | volunteers, shifts, signups | Volunteer Hours, Shift Roster (SQL) | Volunteer | Shift Roster |
| Inventory | suppliers, items, stock_moves | Items to Reorder | Item | Inventory by Category |
| Projects & Tasks | clients, projects, tasks | Open Tasks | Project | Open Tasks by Project |

Donors & Donations also shows M80's parts at work: a validation rule on
`donations.amount` (> 0), an input mask on `donors.phone`, and the macro New
Donation (Donation Entry on a new record).

## The window

The navigation pane lists Tables · Queries · Forms · Reports · Macros (M80) with record
counts and descriptions, search, create, F2 rename in place and a context
menu (open, design, export, new form/report, rename, delete — deletes
say what they take with them and refuse while other records depend on a
table). The ribbon: File (database: back up, properties, compact) · Home
(views, clipboard, history, records, sort & filter, find, file field,
object) · Create (tables, queries, forms, reports and labels, macros)
· External Data · Database Tools (relationships, SQL, integrity) · View,
and contextual **Table Design** (with Validation), **Query Design** (six
query types, Parameters), **Form & Report** (Controls, Group, Sort and
Total) and **Macro Design** tabs that come forward with their designer. Database Tools ▸
Relationships draws every table on QindaTK's `DiagramCanvas` (dragging a
key onto a field proposes a relationship; deleting a wire ends one — both
through the preview). Database Tools ▸ SQL shows QindaTK's
`DatabaseSchemaView` (schema statements it validates run through the same
previewed, backed-up, undoable plan) and a read-only SQL console.

## Service API (for QML)

`BaseHost.base` is a `BaseService` (`apps/qindabase/src/base_service.h`);
every call returns a map with `ok` and `error`. Groups: database
(`createUntitled`, `addSystemTables`, `properties`/`setProperties`,
`objects`, `renameObject`, `deleteObjectImpact`/`deleteObject`), tables
(`tableDesign`, `newTableDesign`, `planTableDesign`/`applyTableDesign`,
`quickTable`, `relationships`, `planRelationship`/`applyRelationship`,
`planSql`/`applySql`), queries (`queryDesign`, `newQueryDesign`,
`suggestJoins`, `compileQuery`, `saveQuery`, `runQuery` → datasheet or
`{action, affected}`, `runAction(design, parameters, confirmedCount)`,
`previewSql`), forms and reports (`createForm`, `openForm`,
`saveFormDesign`, `formRowIds`, `formRecord`, `saveFormRecord`,
`deleteFormRecord`, `createReport`, `reportParameters`, `openReport`,
`saveReportDesign`, `sourceFields`), M80 (`testValidationRules`,
`accessSql`, `parseAccessSql`, `runMakeTable`, `macroDesign`,
`newMacroDesign`, `checkMacro`, `saveMacro`, `whereRowIds`, `formControls`,
`saveFormControls`, `lookupChoices`, `subformSheet`, `buildReport`,
`createLabels`, `previewLabels`, `openLabels`, `reportInfo`; the catalogs
`inputMaskPresets`, `macroActions`, `labelProducts`, `parameterTypes`; the
`labels` sheet), import (`csvProposal`, `importCsv`,
`xlsxProposal`, `importXlsx`, `accessTools`, `accessTables`,
`importAccess(url, tables, queries)` → the import report), export and
maintenance (`exportObject`, `compactAndRepair`, `checkIntegrity`,
`backupNow`) and file fields (`fileInfo`, `insertFile`, `saveFile`,
`removeFile`). `base.datasheet` is the `DatasheetModel`; the service's
`recordsChanged()` tells views showing one record's files to reload.
Commands are `base…Action` ids (`base_host_actions.cpp`, 71 of them); the
host announces them as `actionRequested(id)` and `BaseCommands.qml`
answers. `BaseHost.fileTarget` ({table, rowid, field, hasFile}) names the
file field the File commands act on.
