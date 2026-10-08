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
  layouts/placeholders, backgrounds or theme.
- **QindaCalc**: native `.xlsx` (QXlsx + post-pass) but: numbers Excel
  writes without `t="n"` load as TEXT; `_xlfn.`-prefixed functions read as
  `#NAME?` and are written without the prefix; error values become text;
  QXlsx turns text starting with `=` into formulas and URL-like text into
  hyperlinks on save; save rebuilds the package (theme, properties,
  everything unmodelled dropped); pictures + dialog charts on one sheet
  produce an undeclared `r:` prefix; table column names go stale; long
  validation lists exceed Excel's 255-character inline limit.
- **QindaNote**: notebook folders only; no import of any document; exports
  PDF, Markdown (current page), HTML via Write; typed text has no formatting.
- **QindaDiagram**: `.qdiagram`; draw.io import; no Visio (`.vsdx`).
- **QindaPlan**: Project XML (MSPDI) open/save, but unmodelled task fields
  (priority, deadlines, custom fields) silently dropped; no `.xlsx`.
- **QindaBase**: SQLite `.qbase`; Access import via mdb-tools (tables
  only); no attachment/picture fields; no Excel import.
- **QindaBooks**: no QuickBooks migration (IIF/QBO/Excel import); File ▸
  Import Lists silently fails (`"customer"` vs `customers`); company logo,
  statements, recurring-invoice setup, budgets and sales-tax payments are in
  the engine but unreachable; no purchase orders, check printing or time.
- **QindaMail**: no contacts/address book (no vCard/CardDAV, no
  autocomplete); HTML mail shown as plain text, no inline images; no
  `.msg`/`.mbox`/`.pst` import; no meeting invitations; cannot print.

## Milestones

| Milestone | Scope | Gate |
| --- | --- | --- |
| **M60** Word interchange | `office_ooxml` foundation; `office_docx` (QTextDocument ⇄ WordprocessingML with preservation); QindaWrite: native `.docx` open/save, embedded pictures stored as bytes, page layout (paper sizes, orientation, four margins, header/footer distance), rich headers/footers with PAGE/NUMPAGES and different first page, footnotes, tracked changes; `.doc`/`.rtf`/`.odt` through LibreOffice → `.docx` → native reader | all 590 local `.docx` open without error; every saved package validates against the ECMA-376 Transitional XSDs; text and table structure survive our round trip; LibreOffice reopens our output |
| **M61** Excel interchange | the QindaCalc blockers above, function gaps (XMATCH, FILTER/SORT/UNIQUE without spill where possible, IPMT/PPMT, XNPV/XIRR, NETWORKDAYS.INTL, HYPERLINK, …), carry theme/properties | Excel-shaped fixtures (no `t="n"`, `_xlfn.`, shared strings, errors) read correctly; saved packages validate against `sml.xsd` |
| **M62** PowerPoint interchange | fix `.fodp` units and style links; extend `show_model` (tables, charts from cached data, gradients, dashes, arrowheads, flips, bullets/numbering, insets/anchoring/autofit, layouts/masters/placeholders, backgrounds, theme); native `.pptx` reader/writer with preservation | the 15 local decks open with tables, pictures, notes; saved packages validate against `pml.xsd`; reopen identical |
| **M63** Books essentials | list import fix; logo, statements, recurring setup, budgets, sales-tax payments reachable; IIF import (QuickBooks Desktop lists + transactions); reports to `.xlsx`; Plan MSPDI round-trip preservation | QuickBooks sample IIF imports and balances; Project XML unknown task fields survive |
| **M64** Mail essentials | contacts (vCard import/export, autocomplete); HTML message rendering with inline images; `.mbox`/`.msg` import; print | — |
| **M65** Note / Diagram / Base | Note: formatted typing, `.docx` export/import through `office_docx`; Diagram: `.vsdx` import/export; Base: `.xlsx` import, picture/attachment fields | — |

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
