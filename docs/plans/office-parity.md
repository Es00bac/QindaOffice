<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Office parity: Microsoft Office and QuickBooks interchange and gaps

**Owner request (2026-10-08):** bring every application in the suite up to
par with its Microsoft Office / QuickBooks counterpart. Documents must be
interchangeable: open Office files, edit them, save them, and the saved file
must open in Microsoft Office — embedded images included. Close the gaps
"somebody would need".

This page is the program: what was measured, what is missing per app, and
the milestones that close it. Decision record: D-068 (native OOXML, mapped
where modelled, preserved where not).

## What the owner's documents actually use

Measured over the owner's own files on the laptop (634 packages; profiled
locally, contents never copied anywhere):

| | Files | Producer | Most used features |
| --- | --- | --- | --- |
| `.docx` | 590 | Word (96 %) | tables 88 %, cell shading 79 %, footers 56 %, lists 41 %, headers 32 %, PAGE fields 27 %, page breaks 11 %, merged cells 9 %, pictures 5 %, highlight 4 %, hyperlinks 3 %, content controls, TOC, landscape, text boxes (each < 2 %); no comments or tracked changes |
| `.pptx` | 15 (286 slides) | PowerPoint | speaker notes 100 %, pictures 93 %, tables 87 %, charts 33 %, connectors, custom geometry |
| `.xlsx` | 29 | Excel (79 %) | charts, tables, comments |

## State before this program (surveys of 2026-10-08)

- **QindaWrite**: `.docx`/`.doc`/`.rtf`/`.odt` open through LibreOffice →
  HTML → `QTextDocument::setHtml` (relative image references point into a
  deleted temp dir: every picture lost; page size, margins, headers, footers,
  footnotes lost). Save is the reverse (pictures inserted by file path only).
  One uniform margin in px, A4/Letter only, plain-text header/footer.
- **QindaShow**: every non-`.fodp` format goes through LibreOffice ↔ flat
  ODP. The `.fodp` writer stores geometry as unit-less cm that the reader
  takes as points (shapes shrink to 3.5 % on reopen; LibreOffice reads 1/100
  mm, so `.pptx` saves pile every shape into a corner) and never links shape
  styles (fills and outlines lost). Run formatting, alignment, bullets,
  rotation direction, transitions and arrowheads are lost on save; tables
  and charts load as undecodable metafiles. The model has no tables,
  charts, gradients, dashes, flips, bullets/numbering, insets, autofit,
  layouts/placeholders, backgrounds or theme. *(M62: closed — D-070. M67:
  tables, chart data and notes edited in place — D-076.)*
- **QindaCalc**: native `.xlsx` (QXlsx + post-pass) but: numbers Excel
  writes without `t="n"` load as TEXT; `_xlfn.`-prefixed functions read as
  `#NAME?` and are written without the prefix; error values become text;
  QXlsx turns text starting with `=` into formulas and URL-like text into
  hyperlinks on save; save rebuilds the package (theme, properties,
  everything unmodelled dropped); pictures + dialog charts on one sheet
  produce an undeclared `r:` prefix; table column names go stale; long
  validation lists exceed Excel's 255-character inline limit. *(M61:
  closed — D-069. M66 — D-075: links, outlines, protection, the parts
  QindaCalc does not model, `.xlsm` and dynamic arrays now survive a save.)*
- **QindaNote**: notebook folders only; no import of any document; exports
  PDF, Markdown (current page), HTML via Write; typed text has no formatting.
- **QindaDiagram**: `.qdiagram`; draw.io import; no Visio (`.vsdx`).
  *(M65: closed — D-073.)*
- **QindaPlan**: Project XML (MSPDI) open/save, but unmodelled task fields
  (priority, deadlines, custom fields) silently dropped; no `.xlsx`.
- **QindaBase**: SQLite `.qbase`; Access import via mdb-tools (tables
  only); no attachment/picture fields; no Excel import. *(M65: closed —
  D-074.)*
- **QindaBooks**: no QuickBooks migration (IIF/QBO/Excel import); File ▸
  Import Lists silently fails (`"customer"` vs `customers`); company logo,
  statements, recurring-invoice setup, budgets and sales-tax payments are in
  the engine but unreachable; no purchase orders, check printing or time.
  *(M63: QuickBooks migration, reachable features, checks on voucher stock —
  D-071. M68: purchase orders, employees and time, the check print queue and
  per-printer alignment — D-077.)*
- **QindaMail**: no contacts/address book (no vCard/CardDAV, no
  autocomplete); HTML mail shown as plain text, no inline images; no
  `.msg`/`.mbox`/`.pst` import; no meeting invitations; cannot print.
  (Closed by M64 except `.pst` and CardDAV.)

## Milestones

| Milestone | Scope | Gate |
| --- | --- | --- |
| **M60** Word interchange (**landed**: see [word-interchange.md](../word-interchange.md)) | `office_ooxml` foundation; `office_docx` (QTextDocument ⇄ WordprocessingML with preservation); QindaWrite: native `.docx` open/save, embedded pictures stored as bytes, page layout (paper sizes, orientation, four margins, header/footer distance), rich headers/footers with PAGE/NUMPAGES and different first page, footnotes, tracked changes; `.doc`/`.rtf`/`.odt` through LibreOffice → `.docx` → native reader | all 590 local `.docx` open without error; every saved package validates against the ECMA-376 Transitional XSDs; text and table structure survive our round trip; LibreOffice reopens our output |
| **M61** Excel interchange — **done 2026-10-08 (D-069)**; open: FILTER/SORT/UNIQUE/SEQUENCE need spilling (closed by M66), structured references | the QindaCalc blockers above, function gaps (XMATCH, FILTER/SORT/UNIQUE without spill where possible, IPMT/PPMT, XNPV/XIRR, NETWORKDAYS.INTL, HYPERLINK, …), carry theme/properties | Excel-shaped fixtures (no `t="n"`, `_xlfn.`, shared strings, errors) read correctly; saved packages validate against `sml.xsd` — met: `tst_calc_io_excel`, `tst_calc_io_package`, and the schema gate in every calc_io test |
| **M62** PowerPoint interchange — **landed (D-070)** | fix `.fodp` units and style links; extend `show_model` (tables, charts from cached data, gradients, dashes, arrowheads, flips, bullets/numbering, insets/anchoring/autofit, layouts/masters/placeholders, backgrounds, theme); native `.pptx` reader/writer with preservation | the 15 local decks open with tables, pictures, notes; saved packages validate against `pml.xsd`; reopen identical — **met**: the owner's 18 local decks (333 slides) and 7 sample decks (139 slides, 16 charts) all open, round-trip to an equal model and draw the same through `.fodp`; every package we write validates except chart parts carried verbatim from samples whose own copies fail the same way (`docs/verification.md`) |
| **M63** Books essentials — **done 2026-10-08 (D-071)** | list import fix ✔; logo, statements, recurring setup, budgets, sales-tax payments reachable ✔; IIF import (QuickBooks Desktop lists + transactions) ✔ + QuickBooks Online list CSVs ✔; reports to `.xlsx` ✔; Plan MSPDI round-trip preservation ✔ (+ Priority/Deadline, task sheet to `.xlsx`); check printing on US voucher stock ✔. Not done, for a later milestone: purchase orders, time tracking, blank-stock (MICR) checks and a printer alignment offset | QuickBooks sample IIF imports and balances; Project XML unknown task fields survive |
| **M64** Mail essentials — **done 2026-10-08 (D-072, [mail-interchange.md](../mail-interchange.md))**; open: POP3 HTML and proper iMIP parts need toolkit follow-ups, invitations from the event dialog, `.pst`, CardDAV | contacts (vCard 2.1/3.0/4.0 import/export, Outlook CSV import, People workspace, autocomplete) ✔; HTML message rendering, sanitized, remote pictures blocked, inline images ✔; `.eml` open/save, `.mbox` import, Outlook `.msg` import (MS-CFB, MS-OXMSG, compressed RTF) ✔; print / PDF ✔; meeting requests answered (receiving side) ✔ | the owner's 23 `.eml` parse (17 HTML, 9 inline pictures all resolved, part numbering equal to the toolkit's, writer round trip identical); the hostile-HTML corpus never makes the text engine load a remote or local URL; corrupt `.msg`/CFB files fail cleanly — met: 25/25 mail targets, 468 QtTest cases on qinda ([verification.md](../verification.md#m64-mail-essentials-gates-d-072)) |
| **M65** Note / Diagram / Base | Note: formatted typing, `.docx` export (**landed**: *Export as Word Document*, Open in QindaWrite as `.docx`) and import through `office_docx`; Diagram: `.vsdx` import/export — **landed (D-073, [visio-interchange.md](../visio-interchange.md))**: `.vsdx`/`.vsdm`/`.vstx`/`.vstm` open and save natively with per-shape preservation; Base — **landed (D-074)**: `.xlsx` import with types from the cells, typed `.xlsx` export, the Access import with design, relationships, select queries and a report, file/picture fields | the exported page validates and reads back with every block kind; Visio: hand-built packages read, write and round-trip, LibreOffice renders the exports, the local sample corpus round-trips with 0 failures; Base: Excel round trip keeps types and values, file fields persist and undo, the Access import runs on captured mdb-tools output (`docs/verification.md`) |
| **M66** Excel gaps — **done 2026-10-08 (D-075)**; open: chart sheets as chart views, structured references, array constants | cell hyperlinks (`<hyperlinks>`, internal places, Ctrl+click, Insert ▸ Link, clickable `HYPERLINK()`) ✔; row/column outlines with the gutter ✔; sheet and workbook protection with Excel's SHA-512 hash, locked and formula-hidden cells ✔; the unmodelled parts carried (pivot tables with `refreshOnLoad`, slicers, timelines, query tables, connections, external links, data model, custom XML, threaded comments, controls, printer settings, the `.xlsm` VBA project) ✔; dynamic arrays (FILTER, SORT, SORTBY, UNIQUE, SEQUENCE, RANDARRAY) spilling ✔ | per item a calc_io test over hand-built Excel-shaped packages; every saved package schema-valid with every relationship resolving; LibreOffice re-opens the outputs and imports the carried pivot table over its moved range; engine tests under structural edits; the QindaCalc UI, ribbon and menu-export gates — met on qinda ([verification.md](../verification.md#m66-calc-gaps-gates-d-075)) |
| **M67** Show editing — **done 2026-10-08 (D-076)**; open: combo charts beyond their first plot, multi-level categories after an edit, effect directions other than PowerPoint's defaults, a check in PowerPoint itself (not available here) | tables in place — click into a cell, Tab / Shift+Tab, insert and delete rows and columns, merge and split, shading, borders, column and row drags, Insert ▸ Table in the deck's default style, unedited cells written byte for byte ✔; charts: Edit Data in a sheet, saved into the chart part's formulas and caches and into its embedded workbook through calc_io, kind changes among the drawn kinds ✔; speaker notes in a pane under the slide, undoable, saved into their notes slide; Presenter View on one screen ✔; Wipe / Split entrances, Spin / Grow-Shrink emphasis, Wipe / Split / Push transitions ✔ | hand-built decks per item read back equal; every package and every embedded workbook valid against the ECMA-376 schemas; the workbook re-read through calc_io holds the edited numbers; LibreOffice converts the edited decks; the QindaShow UI, fixtures, grabs, ribbon squeeze and menu-export gates — met on qinda ([verification.md](../verification.md#m67-show-editing-gates-d-076)) |
| **M68** Books orders, time and checks — **done 2026-10-08 (D-077)**; open: job costing and billable expenses, billing rate levels, QuickBooks TIMEACT/EMP import, blank-stock (MICR) checks, payroll (M71 extends the employees list) | purchase orders (non-posting; partial and full receipt by bills, quantities derived from live bills so a void puts them back; close a line or the order; form, PDF, e-mail; Open Purchase Orders report; IIF `PURCHORDR` import) ✔; employees and time (single activities, the weekly timesheet, billable time onto invoices and off again on void or line removal; Time by Job, Time by Name, Unbilled Time by Job) ✔; check print queue (numbers given at printing, misprints recorded void, a run taken back) and per-printer alignment with a test page ✔; schema v4 | engine and io tests per item with hand-worked totals and PDF text positions; QindaBooks service, host, menu-export, ribbon/squeeze and fixtures; dialog grabs at 1280 and 520 px — met on qinda ([verification.md](../verification.md#m68-books-gates-d-077)) |
| **M70** Word formatting tools in QindaWrite — **done 2026-10-09 (D-079)**; open: the owner's check in Word, Border Styles and the Border Painter, table styles' gallery, a cell's own preferred width, drawing what Qt cannot (double underline and strike, hidden text, bar tabs, exact row heights) | Table Design (cell shading, borders with pen style / weight / colour on selected cells or the whole table) ✔; Table Layout (select, delete, nine alignments, AutoFit, distribute, repeat header rows that repeat on each page, sort, convert text ↔ table, Table Properties: width, alignment, indent, layout, cell margins, row height at least / exactly, can't split, column width) ✔; Paragraph dialog with Line and Page Breaks driving the pagination ✔; Tabs with leaders drawn on screen and in PDF ✔; Borders and Shading for paragraphs and cells ✔; Font dialog with Advanced and Change Case ✔ | per property group an office_docx test writes, validates against the ECMA-376 schemas and reads back equal; an edited Word table keeps its untouched cells' markup; LibreOffice renders a sample; through the real window each command and dialog is one undo step, enabled only where it applies, and the saved file carries Word's markup; ribbon squeeze, menu export, fixtures and dialog grabs at 1280 and 520 px — met on qinda ([verification.md](../verification.md#m70-write-formatting-gates-d-079)) |
| **M72** Word forms — **landed (D-080)** | content controls kept on save (block and inline; check boxes, lists, dates, placeholders, gallery wrappers) and filled in by a click (check box, list, calendar, placeholder); QindaTK focus-reveal fix so the page no longer jumps when focus returns (QindaTK D-309) | `tst_docx_controls`, `tst_write_controls`; LibreOffice renders an edited form |

Every milestone follows AGENTS.md: ≤ 500 lines per file, tests headless,
docs in the same commit, a `docs/decisions.md` entry, fixtures and grabs
looked at, the desktop launch only with the owner's consent.

## Working rules for this program

- Heavy builds and full test runs go to the desktop `qinda` (24 threads,
  31 GB); the laptop does incremental builds only. Work travels as hub
  branches (`ssh://desktop/home/cabewse/git/QindaOffice.git`), never as
  copied files.
- The owner's documents are test INPUT only, read in place. Their content
  never enters the repository, a commit message, a test fixture, a log
  shared off the machine, or speech. Fixtures are written by hand or
  generated by the tests.
- Validation against the ECMA-376 Transitional XSDs uses a local copy of
  the schemas (`QQO_OOXML_SCHEMA_DIR`); tests skip that step when the
  variable is unset.
