<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Verification

## Build gates (every milestone)

```
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

Every test runs under `QT_QPA_PLATFORM=offscreen` and, for the QtQuick
windows, `QT_QUICK_BACKEND=software` (pinned via ctest `ENVIRONMENT`; each
test's `main` also sets both defensively). A build against a QindaTK
development prefix needs `-DCMAKE_PREFIX_PATH=<QindaTK>/install`.

The shell lifetime check in `tst_office_window` destroys a document host
while its QML window is still bound to it. It checks that the window dies
first and that no null-property warning is emitted (D-056).

The UI tests also write what a reviewer looks at: the structural fixtures
in `tests/qml_fixtures/` (`QQO_UPDATE_FIXTURES=1` rewrites them) and, with
`QQO_GRAB_DIR=<dir>`, a PNG of each window at 1280x800 and 520x700 — saved
*before* the fixture comparison, so a failing layout is still captured.
`QQO_GRAB_PREFIX` prefixes the file names.

**Validation gallery.** `tools/validation_gallery.py` (default
`http://127.0.0.1:8788/`) is a live page of every capture of the current
work: untracked or modified PNGs in the checkout, its `QindaOffice-*`
worktrees and the plan's QindaTK lane worktrees, plus
`~/.local/share/qindaoffice-validation-gallery/inbox/`. Grab straight into
it with `QQO_GRAB_DIR=~/.local/share/qindaoffice-validation-gallery/inbox
QQO_GRAB_PREFIX=M40-`; captures are archived by content hash, so later
runs never overwrite earlier evidence.

## M73 Write lists and styles gates (D-082)

After merging (lead): the full suite on qinda 221/221 at f1a2aee; the
owner's 590 `.docx` read and round-trip with 0 failures, 0 text/table/
picture changes and 0 schema-invalid packages; page counts against Word
unchanged for all 567 Word-paginated documents.

On qinda (branch `m73-write-lists`, merged with `m60-office-formats`
6de7b18; only office_ooxml, office_docx, QindaWrite, their tests and
`tst_note_word` built; `QQO_OOXML_SCHEMA_DIR` set, so every package a test
writes is validated against the ECMA-376 Transitional schemas):
`ctest -R "tst_write_|tst_docx_|tst_ooxml|tst_note_word"` **31/31 targets**
— the 27 after M70 plus `tst_docx_lists`, `tst_docx_styles`,
`tst_write_lists` and `tst_write_styles`. QtTest functions (init/cleanup
included): **241 passed, 3 skipped, 0 failed** (the skips: the corpus
without `QQO_DOCX_CORPUS`, two opt-in renders).

**office_docx** (hand-built packages only). `tst_docx_lists` 9/9 — Word's
lists read as Word lists (instance, definition identity, Qt's marker off,
0.25 in hanging; 1. a) 2. 3. 4. •); an edit elsewhere leaves every element
of `numbering.xml` as read and the items' `w:numPr` as Word wrote them, no
`w:ind`; the libraries written and read back equal (• in Symbol, I. II.
right-aligned, a red ➢ in Wingdings, –), one Undo taking the four new lists
away, a `w:nsid` on each new `w:abstractNum`; 1. 1.1. 1.1.1. 1.2. 2. 2.1.
at Word's indents (792 / 432 twips), the same list redefined as 1) a) i)
and as Article I. / Section 1.01 / (a) with legal numbering, the text
after a wide number at the next default tab stop; "Step %1:" from 3,
ordinal and spelled-out numbers ("4th", "Twenty-One", "Twelfth"); Restart
at 1 as a new `w:num` over the same `w:abstractNum` with
`<w:lvlOverride w:ilvl="0"><w:startOverride w:val="1"/>` (no new
definition) and read back 2. 1. 2., Continue Numbering putting every item
back on `w:numId` 1, Set Numbering Value 7. 8. 9.; Tab on an item written
as `w:ilvl="1"` of the same list, a removed item without `w:numPr`;
LibreOffice renders the sample. `tst_docx_styles` 9/9 — the Styles pane's
list (Heading 1 linked and in use, Heading 1 Char and Default Paragraph Font
not listed, Strong a built-in the document lacks; the gallery Normal
first); Heading 1 modified (20 pt, C00000, centred, 18 pt before) restyling
both headings and not Normal, one Undo and Redo, written as the
`Heading1` style with `w:name` "heading 1", `w:basedOn` and `w:next`
Normal, `w:link` Heading1Char, `w:uiPriority` 9 and `w:qFormat` in Word's
order, the paragraphs saying only `<w:pStyle/>` (the one centred by hand
keeps its `w:jc`) and no run colour; a new style from a centred bold paragraph (`Note2`,
`w:customStyle="1"`, based on Normal, next itself, `w:qFormat`) read back
centred and bold; Strong as `w:rStyle` and back, Default Paragraph Font
taking it off; Delete Note (Sub Note based on Normal now, Note's paragraph
falling back), Clear Formatting keeping Heading 1 and dropping a hand
centring and an italic word; a style's numbering (`w:numPr` in the style,
none on the paragraphs, read back numbered); LibreOffice renders the
sample.

**QindaWrite, through the real window.** `tst_write_lists` 8/8 — library
picks on one list (➢, then o, then I. II. III.; three undo steps back to
plain paragraphs), Multilevel 1. 2. 3., the Numbering button turning it
off in one step, `%1.%2.` and `multilevel` in the saved numbering part, a
heading style on an item keeping its number; Tab at an item's start → 1.1.
(no tab typed, Change List Level ▸ Level 2 checked), Shift+Tab back, a tab
typed mid-item, Enter on an empty item ending the list, Backspace at an
item's start taking the number and keeping the indent (Word's 0.1. for a
second level with no first); Restart at 1 (`w:startOverride` saved,
Continue enabled), Continue Numbering, Set Numbering Value 7 and its Undo;
Define New Bullet (★ in red), Define New Number Format (Step 3: Step 4:
Step 5:), Define New Multilevel List (Part I / (a) / Part II), Adjust List
Indents (number at 0, text at 0.75 in, a space after it), each one step,
`upperRoman` "Part %1" saved; Define New Number Format on a 1.1. item
keeping Word's `%1.%2.` (preview 1.b), saved `lowerLetter` "%1.%2)", one
Undo) while a one-level list's second level still reads "%1."; the five
list dialogs and a multilevel list at 1280 and 520 px. `tst_write_styles`
9/9 — the pane's rows (linked, in use, built-in), the gallery
(`writeQuickStyle0` is Normal) applying the document's Note in one step,
Strong joining the document as a character style (Undo takes the run and
the styles part back together), Heading 1 on part of a paragraph applying
Heading 1 Char, the pane opening from its command; Modify Style through
M70's Font and Paragraph on the style (20 pt, C00000, centred) restyling
both Heading 1 paragraphs and not Heading 2, the paragraphs saying only
`w:pStyle`, one Undo; Modify Heading 2 ▸ Numbering (the style numbered,
the paragraphs not; reopened, 3.); New Style from a bold right-aligned
selection (Callout), a linked Aside with Aside Char, Update Callout to
Match Selection (italic) reaching both paragraphs, Delete Callout (back to
Normal; Heading 1 cannot be deleted) and its Undo; Clear Formatting keeping
Heading 1; Enter after a heading giving the document's own Normal; Format
Painter once (one step) and locked until Esc, Ctrl+Shift+C / Ctrl+Shift+V
carrying a paragraph's centring; the Navigation pane (Introduction, Scope,
Plan, Budget, Staffing at levels 1 2 1 2 2), go, collapse, Move Down in one
step with the moved heading still Heading 2, a whole section moved; both
panes and Modify Style at 1280 and 520 px. `tst_write_ui` 19/19: the
ribbon gate at 1400 and 520 px (every menu command reachable, every icon
known, every item inside the ribbon — the Multilevel List and Format
Painter split buttons, the quick-style slots, More styles and Styles Pane
included) and the fixtures regenerated on purpose
(`QQO_UPDATE_FIXTURES=1`, 356 → 494 rows each: the hidden Styles and
Navigation panes with their rows, Format Painter in Write's own Clipboard
group, the Multilevel List button, `writeQuickStyle0`–`3` and Styles Pane
where `writeStyle0`–`3` and `writeStyle8` were — those ids stay, under
More styles and Format ▸ Style). `tst_write_menu_export` 3/3 (every new id
resolves in the ActionModel). The rest unchanged and green:
`tst_write_control` 8/8, `tst_write_view` 9/9, `tst_write_typography` 7/7,
`tst_write_review` 13/13 (Track Changes on Word's Ctrl+Shift+E),
`tst_write_formats` 6/6, `tst_write_formatting` 8/8, `tst_write_tables`
7/7, `tst_write_probe_pagination` 7/7, `tst_write_tools` 3/3,
`tst_write_docx` 7/7 (2 opt-in skips), `tst_write_pagination` 8/8,
`tst_write_story` 6/6, `tst_write_picture` 5/5, `tst_write_comments` 3/3,
`tst_write_controls` 3/3, `tst_docx_read` 15/15, `tst_docx_write` 6/6,
`tst_docx_restyle` 3/3, `tst_docx_comments` 8/8, `tst_docx_controls` 7/7,
`tst_docx_tables` 8/8, `tst_docx_paragraphs` 8/8, `tst_docx_corpus` 2/2 (1
skip), `tst_ooxml` 24/24, `tst_note_word` 3/3.

**Looked at** (`QQO_GRAB_DIR`): the multilevel list at 1280 and 520 px (1.
1.1. 1.1.1. 2. 3. 4. and a ✓ item at Word's indents; Home's Clipboard with
Format Painter and Styles with Normal, No Spacing, Heading 1, Heading 2,
More styles and Styles Pane; at 520 the groups wrap with every button);
Define New Bullet (Word's symbols, any character, font, colour, alignment,
preview), Define New Number Format on a 1.1. item (`%1.%2.`, previews 1.1.
1.2. 1.3.), Define New Multilevel List (levels 1–9, include level, restart
after, legal, alignment, positions, follow with, the nine-level preview),
Set Numbering Value and Adjust List Indents at both widths; the Styles and
Navigation panes together (at 520 both fit, the page a sliver between
them); Modify Style on Heading 1 at both widths; LibreOffice's PDFs of the
two samples (Heading 1 20 pt bold dark red, the centred heading, the note
styles, 1. 1.1. 1.1.1. 2.; 1. 1.1. 1.1.1. 2. 3. and a ➢ bullet —
LibreOffice runs the text into a number wider than its hanging indent,
"1.1.1.Goals", where Word moves it to the next tab stop). In some dialog
grabs the page is scrolled: QindaTK's `Tk.Scroll` reveals the page item by
its whole height when focus returns to it after the previous dialog closes
(M70's dialog loop does the same; a toolkit gap, D-082).

On the desktop (owner, Microsoft Word): in QindaWrite build a multilevel
list (Home ▸ Multilevel List ▸ 1. 1.1. 1.1.1., Tab and Shift+Tab), Restart
at 1 on one item and Continue Numbering on another; modify Heading 1
(Styles pane ▸ Modify: size, colour, centred) and give a word Strong; save,
open in Word — no repair prompt; the numbers, levels and restart as set,
every Heading 1 as modified and Word's own Styles pane showing the change.
Then open an untouched Word document with lists, change a word elsewhere,
save — its numbering as it was.

## M71 payroll gates (D-081)

Qualified on qinda (branch `m71b-general-books` over `m60-office-formats`
f1a2aee — M71 with its M71b amendment: business types, Colorado as a
choice, payroll tax items), books and payroll targets only (qinda's disk is
shared), `ctest --preset dev -R "books|payroll"`: **46 of 46 passed** — the 32
before M71 plus fourteen new ones (seven `tst_payroll_*`,
`tst_books_payroll`, `tst_books_payroll_items`, `tst_books_owner_pay`,
`tst_books_payroll_forms`, `tst_books_io_payroll`, `tst_qindabooks_payroll`,
`tst_qindabooks_payroll_ui`); `tst_books_company` (the M68 migration now
reaches version 5), `tst_books_reports` (39 reports), `tst_qindabooks_host`
(the eighth workspace and the twelve payroll commands),
`tst_qindabooks_menu_export` (the Payroll menu) and the QindaBooks fixtures
changed for it. QtTest functions (init/cleanup and data rows included):
**430 passed, 0 failed, 0 skipped** — `books_payroll` 75 (tables 10, federal 10,
colorado 11, paycheck 14, estimates 12, calendar 9, custom 9);
`books_engine` 140 (payroll 13, payroll items 8, owner pay 9, forms 10);
`books_io` 94 (payroll 7); `books_connect` 21; QindaBooks 100 (payroll
10, payroll UI 8). `TZ=UTC`, generated company files only, every
business, person and item made up, identity numbers made up (EIN
00-0000000, SSN 900-00-0001) and compared without being printed.

- [x] `tst_payroll_tables` — the 2026 file loads with 29 sources and four
      unconfirmed values; every Publication 15-T base is recomputed from the
      row above (the Step 2 single schedule is half the standard one, as
      printed); the standard thresholds are the Form 1040-ES brackets
      shifted by the standard deduction less line 1g ($19,300, $7,500,
      $15,550); 2027 is refused in words; a damaged file names the bad path;
      holidays move due dates; no floating point anywhere in `books_payroll`.
- [x] `tst_payroll_federal` — Pub 15-T's own example (a nonresident alien,
      $31.23 → $31), then by hand: single biweekly $156.15, married monthly
      with a credit $53.34, Step 2 checked $485.29 / $292.08, head of
      household with Step 4 $70.46, 2019-or-earlier W-4s $366.67 / $515.33,
      exempt and credits above the tax → $0, supplemental 22 % and 37 %
      over $1,000,000 ($220.00, $15,500.00).
- [x] `tst_payroll_colorado` — DR 1098 by hand ($79, $180, $200), DR 0004
      line 2 ($50, $44) and a blank line 2, exempt and no-certificate rules,
      the 2019-or-earlier mapping flagged unconfirmed, UI by part (91.50,
      37.66), FAMLI shares (26.40 / 13.20; the employee capped at half),
      each city's threshold (at least, more than, Aurora repealed).
- [x] `tst_payroll_paycheck` — $3,000 biweekly in Denver: 320.38, 186.00,
      43.50, Colorado $123, FAMLI 13.20, OPT 5.75, net **2,308.17**, employer
      356.20; Section 125 and 401(k) (FAMLI on gross, flagged; net
      2,819.37); the social security base crossed mid-check (279.00), the
      Additional Medicare threshold (45.00), FUTA and UI bases, a
      shareholder's health premium, a bonus at 22 %, employer shares on
      running totals (76.55 then 76.53), whole-dollar rounding, refusals
      (another year, negatives, deductions above pay, the deferral limit
      note "$24,500.00"); the same check for a business outside Colorado —
      none of Colorado's taxes or notes, net 2,450.12, employer 247.50 —
      and with three made-up items of its own (27.00 over a wage base,
      10.00, 4 % + $5.00 = 125.00; net 2,315.12, employer 274.50).
- [x] `tst_payroll_custom` — a business's own payroll tax items, by hand:
      2.7 % of $3,000 = 81.00 on gross less Section 125; a $10,000 wage base
      crossed mid-check (27.00, then nothing); an annual limit cutting 30.00
      to the 5.00 left; user-entered withholding on the income tax wages
      (4 % of $2,700 + $5.00 = 113.00; the extra alone 5.00); an exempt
      employee with the worksheet saying why; half-up cents (4.17, 0.01);
      the stored keys.
- [x] `tst_payroll_estimates` — Pub 505's safe-harbor example ($46,839.10,
      "110 % of 2025 tax") and amended installments ($2,175 / $1,025); a
      $80,000 Schedule C: SE 11,303.64, QBI 11,649.64, taxable income
      46,598.54, total 16,647.46, required 14,982.71 in 3,745.68 ×3 +
      3,745.67, Colorado 2,050 → 1,435 in 359/359/359/358, set-aside 24 %;
      outside Colorado federal only (16,647.46, set-aside 21 %, no Colorado
      note); the prior-year harbor; zero and negative profit; annualizing;
      wages sharing the social security base; the $400 and QBI floors; 2027
      refused.
- [x] `tst_payroll_calendar` — Pub 15's semiweekly example, a holiday adding
      a day, a period split at the quarter, monthly, next-day, 941 dates
      (Nov 2, 2026; Feb 1, 2027), Colorado quarterly/monthly/weekly and the
      year-end period.
- [x] `tst_books_payroll` — the accounts once; one paycheck posts what was
      computed (Officer Wages 3,000.00, Payroll Taxes 343.00, each
      liability, bank −2,308.17); year to date (OPT once a month, FUTA
      stops at $7,000); void and adjust restore the year; paycheck rows
      never change (triggers); paychecks numbered through the M68 queue
      (1001); liabilities: federal 779.38 due Nov 16, Colorado Feb 1, FUTA
      to Form 940, a cent of rounding to Payroll Taxes, a partial payment,
      more than owed refused, a void putting the balance back; the SSN never
      in the audit; reports add up; a version-4 file migrates (`.pre-v5.bak`)
      with its employee intact; a sole proprietor or partner refused a
      paycheck, "not sure" allowed.
- [x] `tst_books_payroll_items` — **custom items end to end**: a business
      outside Colorado (Form 940 state TX) with three made-up items — an
      employer unemployment tax (2.7 % up to a $10,000 wage base), an
      employee disability tax (1 % up to a $5,000 wage base) and state
      withholding whose 4 % + $5.00 the employee carries. Bad items refused
      (the same name in other case, no rate, 150 %, a zero wage base, an
      account that is not a liability); each item gets its own account
      under Payroll Liabilities and none of Colorado's is made. Two $3,000
      checks: the disability **crosses its wage base** (30.00, then 20.00),
      unemployment 81.00 and 81.00, withholding 125.00 each; net 2,295.12;
      the accounts owe 50.00, 162.00 and 250.00. **In the liabilities**:
      each item owed to its agency on its own schedule (monthly due the 20th:
      Nov 20; quarterly: Feb 1, 2027; monthly, last day: Nov 30), a payment
      clearing one, the Payroll Summary's rows for them, no Colorado or OPT
      line.
      **On the stub**: the employee's items with the year to date
      (disability 50.00), the employer's after them, no Colorado line. **The
      round trip**: the company file closed and opened again keeps the
      items (2.7 %, $10,000, the agency), the employee's 4 % + $5.00 and
      what each check took (three `paycheck_items`, net 2,295.12). What a
      check took never changes (triggers); a used item's account cannot
      move; an inactive item stops applying.
- [x] `tst_books_owner_pay` — a draw by queued check and by transfer
      (−3,500.00 to Owner Draws), estimated payments as draws ("Federal
      estimated tax 2026, payment 3 of 4"), the set-aside transfer, an
      S corporation refused a draw and paying (and voiding) a distribution,
      a C corporation's dividend from retained earnings (2,500.00) and the
      draw, distribution, set-aside and estimated payment it may not
      record, an S corporation refused a dividend, a nonprofit
      refused every owner payment ("no owners"), "not sure" allowed each,
      a Schedule C owner refused a distribution.
- [x] `tst_books_payroll_forms` — Form 941 over three checks: line 2
      9,000.00, 3 961.14, 5a 1,116.00, 5c 261.00, 6 and 12 2,338.14, line 7
      0, months 1,558.76 / 0 / 779.38, a deposit on line 13; fractions of
      cents (line 7 −0.01, line 12 equal to the books); Schedule B; Form 940
      (3 9,000, 5 2,000, 7 7,000, 8 42.00 = the books); W-2 and W-3 masked
      and, for filing, in full; DR 1094 / DR 1093 369.00; the UI report
      (months "101", premium 274.50 = the books) and FAMLI 39.60; outside
      Colorado, Form 940 line 1a the business's state ("TX"), a W-2 without
      the Colorado boxes saying so, a state name refused; a year without
      tables says so.
- [x] `tst_books_io_payroll` — the paycheck on voucher stock: payee and
      address, `**2,308.17`, "Two thousand three hundred eight and 17/100",
      the stub lines this check and year to date on both stubs, the SSN
      masked; a business's own item on the printed stub (30.00 this check
      and year to date; the Colorado lines 0.00 this check); the Form 941
      PDF; signature PINs (check digit); EFW2 refused with the made-up
      numbers, and written unvalidated with every field at its Publication
      42-007 position.
- [x] `tst_qindabooks_payroll` — `PayrollService`: setup (the EIN in full
      only for the edit field, CDLE's three UI parts, refusals), employees on
      M68's list with a masked SSN, Run Payroll's draft, preview ($2,308.17,
      employer 343.00) and posting, the stub, adjust and void, the federal
      deposit due Apr 15 and paid, payroll reports in the catalog, the W-2
      masked on screen and a filing PDF, EFW2 refused without echoing a
      number; the Schedule C planner (16,647.46, 3,745.68, Colorado 2,050,
      24 %), a draw with its $240.00 set-aside, a queued draw check, the
      installment paid, Colorado's due Apr 15 in `upcoming(30)` and one
      reminder; S corporation refusals; each business type's owner pay (a
      C corporation's dividend, a nonprofit's "no owners", a partner's
      draw); a business outside Colorado with its own items (2.7 % over
      $10,000; an employee's 4 % + $5.00; the preview's Other taxes
      $125.00, net $2,325.12, employer $328.50; the stub and employer lines;
      $81.00 owed to the agency, due Apr 30; no Colorado form offered).
- [x] `tst_qindabooks_payroll_ui` (1280 and 520 px) — the setup dialog, the
      employee editor, Run Payroll's three steps, the paycheck (8 stub lines,
      the Pub 15-T worksheet, queued for Checks to Print), Pay Payroll
      Liabilities (federal first, due Apr 15), Form 941 under Reports, Pay
      Owner turned into a distribution, the tax picture, the planner (4
      installments), an estimated payment, a draw with its set-aside moved,
      the Overview and Owner pay tabs, the Payroll ribbon tab; a partnership
      outside Colorado: the setup with its state and no Colorado section,
      Payroll Tax Items (an employer item over a wage base and a withholding
      item), the employee editor's user-entered 4 % + $5.00, the preview's
      Other taxes ($125.00, net $2,325.12).
- [x] Fixtures — `qindabooks_1280.txt` and `qindabooks_520.txt` regenerated
      with `QQO_UPDATE_FIXTURES=1` and compared: byte-identical to M71's,
      which added the Payroll rail entry, the hidden Payroll workspace and
      the hidden narrow report picker (the new dialogs are popups and the
      Payroll ribbon tab is not the one showing).
- [x] Ribbon gate (`tst_qindabooks_ui`) green at 1320 and 520 px with the
      Payroll tab and its two new commands; menu export
      (`tst_qindabooks_menu_export`) validates the tree with the twelve-item
      Payroll menu and fires every exported command.
- [x] Grabs reviewed (`QQO_GRAB_DIR`, 1280 and 520): setup (Colorado and
      another state), Payroll Tax Items, the employee editor (with
      user-entered withholding) and list, Run Payroll (grid, preview with
      Other taxes, done), the paycheck and the voucher stub PDF, Pay Payroll
      Liabilities, Form 941, Pay Owner (distribution and draw with
      set-aside), the tax picture, Estimated taxes, an estimated payment,
      Overview, Owner pay, the Payroll ribbon tab. At 520 px the dialogs
      drop to one column, wide tables scroll sideways, the Payroll sections
      and the reports come from a list.

**On the desktop** (not done here — nothing is launched into the live
session): open a *new* test company with made-up details, set it up as
"Not sure", then as each business type in turn — S corporation, C
corporation, partnership, sole proprietorship, nonprofit — and check what
Pay Owner offers; add an employee with a made-up SSN; Run Payroll with
Print later, print the paycheck on plain paper from Checks to Print and
hold it against voucher stock; Pay Payroll Liabilities; open Payroll Tax
Forms and export Form 941 as PDF and Excel; switch Colorado off, name
another state, add a payroll tax item and an employee's user-entered
withholding, and run a paycheck; for a sole proprietorship, enter a tax
picture, record a draw and its set-aside; check the panel menu (Payroll),
the Payroll ribbon tab in dark and light themes, and the window at 520 px.
QindaBooks prepares the numbers; a business should confirm how the IRS
taxes it and have a tax professional review its first payroll and its first
estimate.

## M69 Calc gates (D-078)

Qualified on qinda (branch `m69-calc-formulas`, merged with
`m60-office-formats` 48e3074) with `QQO_OOXML_SCHEMA_DIR=~/scratch/qqo/ecma-xsd`
and xmllint, as a targeted build (only the targets below; qinda's disk is
shared): **45/45 CTest targets passed, 124.20 s** (`ctest --preset dev -R`
over every calc target and the calc_io consumers `tst_pptx_charts`,
`tst_books_io_reports`, `tst_books_io_orders`, `tst_plan_output`,
`tst_qindabase_interchange`). QtTest counts (init/cleanup included):

- calc_engine — `tst_calc_parser` 57, `_functions` 6, `_recalc` 12,
  `_numfmt` 7, `_csv` 8, `_structure` 18, `_rules` 13, `_spill` 11,
  `_features` 10, **`_tables` 13, `_arrays` 12**: 167 passed.
- calc_io — `tst_calc_io` 9, `_styles` 6, `_rules` 7, `_charts` 5, `_tables`
  4, `_view` 4, `_images` 5, `_excel` 10, `_package` 9, `_links` 4,
  `_outline` 3, `_protect` 3, `_carry` 7, `_carry_sheet` 6, `_arrays` 5,
  **`_structured` 6, `_chartsheets` 5, `_corpus` 2** (1 skipped without a
  corpus), `_libre` 6: 106 passed.
- QindaCalc — `tst_calc_window` 12 (1 skipped), `_sample` 3 (8 skipped
  without `QQO_SAMPLE_XLSX`), `_ui` 16, `_rules_ui` 6, `_menu_export` 3,
  `_tools` 4, `_agent` 5, `_features_ui` 7, **`_tables_ui` 6**, and
  `calc_agent_process`: 62 passed.
- Consumers of calc_io — `tst_pptx_charts` 7, `tst_books_io_reports` 11,
  `tst_books_io_orders` 5, `tst_plan_output` 7, `tst_qindabase_interchange`
  6: 36 passed.

The gates, per item (every package is built by hand in the test the way
Excel writes it — `excel_package.h`, `m69_fixtures.h` — and both the source
and our saved package validate against the Transitional XSDs):

- **Structured references** (`tst_calc_tables`, `tst_calc_io_structured`):
  every specifier parses and prints in both spellings and resolves
  (`#VALUE!` for `[@…]` outside the data rows, `#REF!` for what is gone);
  references follow their cells; calculated columns fill and follow new
  rows; typing below or beside a table grows it; a retyped header renames
  every reference; inserted and deleted rows and columns, a deleted table;
  the totals row on and off; Insert ▸ Table names blank headers. In the
  file: `Table1[[#This Row],[Sales]]` read as `[@Sales]` inside the table
  and written back qualified, `calculatedColumnFormula`,
  `totalsRowFunction`/`totalsRowLabel`/`totalsRowFormula`, `_xlfn.SINGLE`
  as `@`; an edit (a row inserted in the table, a retyped header) reaches
  the saved table part and every `<f>`.
- **Chart sheets** (`tst_calc_io_chartsheets`): a chart sheet opens in its
  place with its chart; the saved chartsheet, drawing, chart and style parts
  are byte-identical to the source and every relationship resolves; after a
  rename of the data sheet and inserted rows only the chart's `<c:f>`
  references change.
- **Array formulas** (`tst_calc_arrays`, `tst_calc_io_arrays`,
  `tst_calc_io_structured`): array constants; `@`; dynamic formulas lift
  (`LEN(A1:A3)` spills) and legacy ones intersect; legacy blocks fill by
  Excel's rules (`#N/A` past the result), refuse partial edits, move with
  structural edits and are written back as `<f t="array" ref>` without `cm`;
  array-typed parameters in legacy formulas; relative names follow the
  using cell; the array and shaping functions.
- **Functions** (`tst_calc_functions`): 132 new rows in
  `tests/fixtures/functions/functions_m69.tsv`, expected values from headless
  LibreOffice 25.2 (`tools/lo_oracle.py`); the rows where it departs from
  Microsoft's documentation (PERCENTRANK truncation, error kinds, SKEW.P,
  EXPON.DIST below 0) and Excel's comparison semantics, numbers stored as
  text in criteria, are asserted by hand (`m69RulesBeyondTheOracle`,
  `excelRulesBeyondTheOracle`).
- **Sizes** (`tst_calc_io_excel`): only the row with `ht` comes back with
  `customHeight="1"`; widths from `<cols>` and `baseColWidth`; a workbook
  with cells in row 1,048,576 and in column XFD loads and saves in under a
  second.
- **LibreOffice re-opens the outputs** (`tst_calc_io_libre`, headless 25.2.7
  to flat ODS): the edited table workbook with its calculated column, totals
  row and legacy array values; the chart sheet re-opens as a chart sheet in
  its place between the worksheets.
- **QindaCalc UI** (`tst_calc_tables_ui`, the real window): Insert ▸ Table
  over typed data, a column typed beside it joining it, a formula filling a
  calculated column, the Total Row, a retyped header renaming references —
  one undo step each; Ctrl+Shift+Enter's braces over a selection, shown as
  `{=…}`, a partial edit refused; a workbook with a chart sheet showing its
  chart in the grid's place; the Insert tab's Tables group at 1280 and
  520 px (squeeze). The ribbon gate (`tst_calc_ui`) and the menu export
  (`calcInsertTableAction`, `calcTableTotalRowAction` resolve) are green.
- **Fixtures** regenerated deliberately (`QQO_UPDATE_FIXTURES=1`): at 1280 and
  520 the only change is the hidden chart sheet view (`Box#chartSheetView`,
  `ChartItem#chartSheetChart`). **Grabs** looked at: the table with its
  calculated column and totals row, the chart sheet filling the window with
  its legend, and the Insert tab at 1280 and 520 px.
- **Real-workbook gate** (`tst_calc_io_corpus`, M69): reads every
  `.xlsx`/`.xlsm` listed in the file `QQO_XLSX_CORPUS` names (one path per
  line), saves each through `writeXlsxFile` as QindaCalc's Save does, reads
  it again and prints only aggregate counts (sheets, cells, formulas,
  changed cells and formula texts, charts, pictures, tables, notes, carried
  parts; schema errors the save adds when `QQO_OOXML_SCHEMA_DIR` is set).
  Run it as `QQO_XLSX_CORPUS=<list> QQO_OOXML_SCHEMA_DIR=<xsd dir>
  QTEST_FUNCTION_TIMEOUT=3600000 build/dev/tests/calc_io/tst_calc_io_corpus`
  (the one test function has QtTest's 5-minute limit otherwise). On qinda,
  over the 482 Excel-written test workbooks vendored with QXlsx
  (`third_party/QXlsx/**`, libxlsxwriter's reference files): 482 read, 0
  failed, 482 written and re-read, 0 cells or formula texts changed, 271
  charts and 48 pictures kept, 0 schema-invalid, slowest round trip 265 ms;
  one non-Excel package (`IssueTest/test.xlsx`, parts at the root) counts
  one carried part differently (an empty relationships part). `data03.xlsx`
  (cells at XFD1 and XFD1048576) is left out: QXlsx's writer needs about
  half an hour for it (D-078 limits). The first runs found the reader's
  rectangle walk (hung on such cells), the custom row heights and QXlsx's
  quadratic column lookups — fixed (D-078 item 8). The owner's 27
  workbooks (laptop, XSD-validated): 27 read and re-read, 263 sheets,
  185,107 cells, 18,243 formulas, 20 tables, 11 charts, 406 notes, 457
  carried parts; 0 cells or formula texts changed and 0 schema-invalid.
  The first run found 364 text values under merged ranges (hidden by the
  merge, still seen by formulas) blanked in 3 workbooks: QXlsx's
  `mergeCells()` overwrites the covered cells, so the writer now merges
  before it writes cells (`tst_calc_io` `roundTrip` keeps a value under a
  merge and a formula reading it).
- Not verified: Microsoft Excel itself (none here).
- **Desktop check (owner):** in QindaCalc on the desktop, open an Excel
  workbook that has a table with a calculated column and a totals row, a
  chart sheet and an array formula; type a row under the table, retype a
  header, change a number the chart shows; save; open the file in Excel: no
  repair prompt, the table has grown with its formulas and totals, the
  renamed column's formulas still work, the chart sheet shows the edited
  chart, the array formula is still `{=…}`. In QindaCalc, Ctrl+T and the
  Total Row read in dark and light themes.

## M70 Write formatting gates (D-079)

After merging (lead): the full suite on qinda 199/199 at 48e3074; the
owner's 590 `.docx` read and round-trip with 0 failures, 0 text/table/
picture changes and 0 schema-invalid packages (`QQO_DOCX_CORPUS_VALIDATE`);
page counts against Word unchanged by M70 (567 documents: 383 exact, 101
off by one; 156 of the 211 reliable ones exact, 198 within one).

On qinda (only office_ooxml, office_docx, QindaWrite, their tests and
`tst_note_word` built; `QQO_OOXML_SCHEMA_DIR` set, so every package a test
writes is validated against the ECMA-376 Transitional schemas):
`ctest -R "tst_write_|tst_docx_|tst_ooxml|tst_note_word"` 27/27 targets; 206
test functions passed, 3 skipped (the corpus without `QQO_DOCX_CORPUS`, two
opt-in renders).

**office_docx.** `tst_docx_tables` 8/8 — in a Table Grid table read from
Word: a shaded cell (`w:shd w:val="clear" w:color="auto" w:fill="FFF2CC"`),
a double red line under the first row in its cells' `w:tcBorders` and the
second row's top, the third row's cells (a theme fill, a theme-coloured
border: cell, paragraph and run properties) and the table's `w:tblPr` /
`w:tblGrid` identical to the source;
Outside and Inside Vertical on the whole table as `w:tblBorders` with no
cell keeping a side of its own; table properties (80 % wide, centred, fixed
layout, cell margins, a header row), a row 0.5 in exactly that cannot
split, a 2000-twip column and a cell's own margins written and read back
equal; Distribute Columns / Rows and AutoFit Window (`w:tblW` 5000 pct,
`w:tcW` pct), Contents (auto, no `w:tblLayout`) and Fixed; a new table as
Word's Table Grid; a 120 × 6 table from HTML taking Word's model (grey frame
line as `w:tblBorders`, padding as `w:tblCellMar`) and formatted in under
three seconds. `tst_docx_paragraphs` 8/8 — spacing before and after
re-folded exactly as a re-read of the saved file folds it, contextual
spacing taking the gap away; outline level, widow control off, keep with
next, keep lines, page break before, exact line spacing, hanging indent;
tab stops replacing a style's dotted stop (written `w:val="clear"`) and the
default stop in `settings.xml`; a paragraph box and shading, then nil
sides; double underline in red, double strikethrough, small caps, hidden,
2 pt spacing, 150 % scale, a raise, kerning — every one read back equal;
LibreOffice renders the sample. The rest of the Word gates are unchanged
and green: `tst_docx_read` 15/15, `tst_docx_write` 6/6, `tst_docx_restyle`
3/3, `tst_docx_comments` 8/8, `tst_docx_controls` 7/7, `tst_ooxml` 24/24,
`tst_note_word` 3/3.

**QindaWrite, through the real window.** `tst_write_tables` 7/7 — the Table
commands disabled outside a table and Convert Text to Table disabled inside
one, Inside Vertical only in a table; shading the header row, a double red
rule under it (pressed again it goes, Undo brings it back), a dashed
outside border on the whole table, each one undo step with Redo; Align
Bottom Right, AutoFit, Distribute, Repeat Header Rows (checked state follows
the caret), Tab in the last cell adding a row with the grid lines and
margins of the one above (one undo step), Select Column, Delete Table and
its Undo; text to table (no empty paragraph left before what followed) and
back, Sort by a number column descending under a header row (one undo
step), Table Properties (60 %, centred, a header row 0.5 in exactly, one
step); the saved `.docx` carries `w:shd`, `w:tcBorders`, `w:tblBorders`,
`w:tblW`, `w:trHeight w:hRule="exact"`, `w:tblHeader` and no run shading
from typing in a shaded cell; a 70-row table's header row repeats at the
top of page two; HTML export keeps the fill and the double rule.
`tst_write_formatting` 8/8 — the Paragraph dialog's answer (centred,
indents, 12/6 pt, exactly 18 pt, level 2, contextual, widow off) in one step
and its `w:pPr`; keep with next moving a heading to its next paragraph's
page through `KeepRules`; dotted right tabs drawn and written, one step,
the default stop in settings, a PDF kept; Outside Borders on two paragraphs
drawn as one box, shading, the Borders and Shading dialog's double red box
(each one step), No Border; the Font dialog's effects in one step and their
`w:rPr`; Change Case cycling lowercase → UPPERCASE → Capitalize Each Word →
Sentence case → lowercase on Shift+F3, tOGGLE cASE and Undo; each dialog
opening from its command at 1280 and 520 px; the context menu offering Font…
and Paragraph…. `tst_write_ui` 19/19: the ribbon gate at 1400 and 520 px
with every contextual tab forced on (every menu command reachable, every
icon known, every item inside the ribbon), the Word-model Insert Table, and
the structural fixtures regenerated on purpose (`QQO_UPDATE_FIXTURES=1`: +30
rows each, the five new Home items — Change Case, Font…, Shading, Borders,
Paragraph…; the dialogs are popups and add none). `tst_write_menu_export`
3/3 (every new id resolves in the ActionModel). The rest of the suite is
unchanged and green: `tst_write_control` 8/8, `tst_write_view` 9/9,
`tst_write_typography` 7/7 (line spacing now through Word's metrics),
`tst_write_review` 13/13, `tst_write_formats` 6/6,
`tst_write_probe_pagination` 7/7, `tst_write_tools` 3/3, `tst_write_docx`
7/7 (2 opt-in skips), `tst_write_pagination` 8/8, `tst_write_story` 6/6,
`tst_write_picture` 5/5, `tst_write_comments` 3/3, `tst_write_controls` 3/3.

**Looked at** (`QQO_GRAB_DIR`): the shaded, bordered table at 1280 and 520
px (pale and dark blue header rows, the double red rule, the dashed blue
frame; at 520 the ribbon wraps its groups); the Table Design tab (shading
slot, Shading, line style, pen colour, weight, Borders); a 70-row table
with its header row repeated on page two; the Table Properties, Borders and
Shading, Sort, Paragraph, Tabs and Font dialogs at both widths; the dotted
leaders on the page and in the exported PDF; LibreOffice's PDF of the
office_docx sample (dotted leader, double red box over pale yellow, dark
blue header row, red dashed inner lines, thick blue outer frame).

On the desktop (owner, Microsoft Word): open a Word document with a table in
QindaWrite; shade its header row, give it a double bottom border and the
table a thick outside border (Table Design); set right tab stops with dot
leaders on two paragraphs (Format ▸ Tabs); give a paragraph a box and
shading (Borders and Shading) and a word a double red underline (Font);
save, open in Word — no repair prompt; the fill, borders, leaders, box,
underline and table properties as set, every untouched cell as it was.

## M68 Books gates (D-077)

Qualified on qinda (branch `m68-books-orders-time`, merged with
`m60-office-formats` 288cd76), books targets only, `ctest --preset dev -R
books`: **32 of 32 passed** — the 26 before M68 plus six new ones
(`tst_books_orders`, `tst_books_time`, `tst_books_checks`,
`tst_books_io_orders`, `tst_qindabooks_m68`, `tst_qindabooks_m68_dialogs`);
`tst_books_company`, `tst_books_io_checks` and `tst_qindabooks_host` gained a
case. QtTest functions across the 32 binaries, init and cleanup included:
**290 passed, 0 failed**. `TZ=UTC`, "today" 2026-03-31,
generated company files only.

- [x] `tst_books_orders` (5 cases) — a purchase order never posts (journal
      lines unchanged, `txnId` 0), is numbered PO-1001 then PO-1002 (a typed
      duplicate refused), takes the item's cost and expense account, keeps
      ship-to, expected date and the line's customer:job; 6 of 10 received
      on a 170.00 bill (the bill line names the order line; open 150.00 =
      4 × 25.00 + 4 × 12.50, Partially received); 5 more refused by the
      receive and by the bill, another vendor's bill refused; Create Bill
      receives the rest (Closed); voiding that bill reopens it; editing the
      first bill so the stock line is gone puts its 6 back (open 300.00); a
      line closed by hand drops out (270.00 → receivable lines 2), the order
      closes and reopens (4 audit rows); an edit below received or removing
      a received line refused, a received line keeps its id through an edit
      that adds a line (total 405.00), a received order cannot be voided, a
      fresh one can. Open Purchase Orders worked by hand: PO-1001 320.00 /
      150.00, PO-1002 200.00 / 200.00, the received and the closed order
      left out, totals 520.00 / 350.00; as of Mar 5 the Mar 10 bill had not
      come — 320.00 open and the order billed Mar 12 still counts (720.00).
- [x] `tst_books_time` (6 cases) — durations read `1:30`, `0:20`, `1.5`,
      `0.25`, `0.33` (19.8 min → 20), `45m`, `2h` and refuse `1:5`, `1:60`,
      `abc`, `0`, `-1`, `1:30:00`; hours 20 → 0.33, 90 → 1.5, 50 → 0.83;
      the employees list (a case-insensitive duplicate and an empty name
      refused; Lee's 90.00 rate bills his hour, Maya's bills the item's
      120.00; an inactive employee keeps his time and takes no new time;
      a negative rate refused; 2 audit rows); a time entry needs an employee
      or a vendor (a customer, nobody, both refused) and billable time a
      customer:job; billing 1:30 + 0:20 + 2:30 at 120.00 makes a 519.60
      invoice (1.5 h, 0.33 h, 2.5 h), billed time cannot change or go on
      another invoice (Duplicate) nor another customer's (Invalid);
      removing a line un-bills it (480.00), voiding the invoice un-bills
      the rest, a draft holds its time; the weekly timesheet reads 4 rows
      (two entries of one row and day never share a cell), writes 1:30 →
      2:00, adds Thursday, deletes the call, and refuses a whole save that
      touches a billed cell (nothing written) or another week's entry. Time
      by Job (Harbor 4:20, Riverbend 0:45, no customer 1:00, total 6:05),
      Time by Name (Jun 2:30; Maya Harbor 1:50, total 3:35) and Unbilled
      Time by Job (180.00 + 39.60 = 219.60 for 1:50; 90.00; total 2:35 /
      309.60) worked by hand.
- [x] `tst_books_checks` (4 cases) — next number 1001 with no checks, 1043
      after a hand-written 1042; a check to print posts at once without a
      number (a typed 999 cleared), a bill payment to print has no
      reference; a card check, a debit-card expense and a card bill payment
      to print refused; the queue lists both by date; numbered from 1043:
      1043 and 1044 on the documents and on the live transactions, the bank
      balance unchanged, the bill still paid, the first posting voided and
      replaced; a misprint goes back unnumbered with 2002 recorded as a void
      zero check (payee, account, the payment it was for), 2002 refused on
      the reprint, 2003 given; a cancelled run comes back with the next
      number at its first; a used number refuses the whole run (nothing
      changes), the wrong account, no checks and number 0 refused, a cleared
      check and one in a closed period keep their numbers.
- [x] `tst_books_company` — a version-3 file with a bill in it opens as
      version 4 with a `.pre-v4.bak` copy, the bill and its line (id 70)
      intact, `to_print` 0, no ship-to, no links, empty `time_entries` and
      `employees`, the counter giving PO-1001; integrity clean.
- [x] `tst_books_reports` — the catalog has 26 reports.
- [x] `tst_books_io_orders` (3 cases) — the purchase-order form: six columns
      (item, description, customer, quantity, the item's 0.90 cost, 360.00),
      one PDF page with PURCHASE ORDER, PO-1001, the vendor and his address,
      SHIP TO, Expected Mar 16, 2026, the lines, Order total $405.00 and the
      vendor message, no payment instructions (gallery
      `books-purchase-order.png` reviewed); QuickBooks `PURCHORDR`: the
      preview counts 2 orders as documents, maps no "Purchase Orders"
      account, rolls back; the import posts only the bill (2 journal lines),
      PO-77 keeps 12 × 25.00 for Bluebird Bakery, the 45.00 freight, the
      expected date and the three-line ship-to; PO-70 (PAID Y) is closed; a
      second import skips both; an order without a vendor is refused at line
      4. A Time by Job of 1:30 + 4:35 writes `6:05` in the CSV and 365
      minutes as a day fraction under `[h]:mm` in Excel.
- [x] `tst_books_io_checks` (alignment case) — 0.25 in right, 0.5 in down
      is 18 pt / 36 pt; the date, payee, figures, memo and a stub line of a
      check PDF each move by exactly that (Poppler text boxes); −5 mm / −2.5
      mm reads "5 mm left, 2.5 mm up"; 1.01 in, 0.125 in, 25.5 mm, 0.05 mm
      and `abc` refused; the store keeps per printer as typed and a zero
      offset removes the printer; the test page carries the sample check
      (03/31/2026, **1,234.56, the words), the printer and the offset, its
      date moved 18 pt (gallery `books-check-alignment.png` reviewed).
- [x] `tst_qindabooks_m68` (3 cases) — through BooksService: an order saved
      (PO-1001, $270.00 to receive), listed under `orders` not `purchases`,
      the vendor row's On order; 6 received → Partially received, $120.00;
      5 more refused with "at most 4"; Create Bill closes it, its void
      reopens it; a line and the order closed by hand; the order cannot be
      voided; PDF written; e-mail to the vendor ("Purchase Order PO-1001
      from …", no payment instructions); the report's totals 27000 / 10000
      minor units and its Excel export. Employees (duplicate and 12.345
      refused, $90.00), `workers()` keys, a time activity and a bad
      duration, the weekly timesheet to 2:45, three unbilled entries to a
      $420.00 invoice (Lee at his rate), billed then unbilled by the void,
      the time reports (Duration columns, "3:45") exported. The check
      queue: a check (number 77 dropped) and a bill payment queued, 1001 and
      1002, the `"checks"` print source exports, a misprint recorded void
      as 1002, 1003 next and 1002 refused, a run taken back; alignment
      saved, described and refused beyond an inch or without a printer; the
      test page PDF and the `"alignment"` source.
- [x] `tst_qindabooks_host` — the 8 new command ids exist with icons; seven
      workspaces (Time on Ctrl+7); every new command emits its request;
      Create Bill from Order follows a selected order with something to
      receive (not a bill, not a closed order) and asks for that order; an
      order prints through the `"document"` source; a `"checks"` source
      naming an order is cleared.
- [x] `tst_qindabooks_menu_export` — File Edit View Sales Expenses **Time**
      Banking Accounting Reports AI Settings Help; every books command once.
- [x] `tst_qindabooks_ui` — the ribbon gate (coverage, icons, squeeze) at
      1320 and 520 px with the Purchases and Time tabs; fixtures
      `qindabooks_1280/520` regenerated for the reviewed additions (Time
      rail entry and view, the Purchase orders tab and its Create bill
      button, the vendor On order column, the named status segments, the
      ribbon tabs).
- [x] `tst_qindabooks_m68_dialogs` (3 cases × 1280 and 520 px) — the
      purchase-order editor (lines scroll sideways at 520), the bill
      offering the vendor's two open lines, receiving 6 through
      `ReceiveOrderDialog`, the received line badged PO-1001, Create Bill
      from the Purchase orders selection; the timesheet saved to 2:15 (the
      week scrolls sideways at 520), the invoice offering two entries,
      `BillableTimeDialog` adding them ($270.00) and the save billing them,
      the Time workspace, the time activity and employee dialogs; Print
      Checks numbering 1001–1002, the confirmation with one reprint (back in
      the queue, next 1003), the alignment dialog saving "0.25 in right, 0.1
      in up". Grabs reviewed at both widths: `qindabooks_po_editor`,
      `qindabooks_receive`, `qindabooks_bill_from_order`,
      `qindabooks_timesheet`, `qindabooks_billable_time`, `qindabooks_time`,
      `qindabooks_time_activity`, `qindabooks_employee`,
      `qindabooks_print_checks`, `qindabooks_print_checks_confirm`,
      `qindabooks_check_alignment`, `qindabooks_ribbon_purchases`,
      `qindabooks_ribbon_time`.

**On the owner's desktop (manual).** Open a copy of a company: create a
purchase order, print it and e-mail it to a vendor through QindaMail;
receive part of it on a bill and the rest with Create Bill; void a bill and
see the order reopen. Enter a week on the timesheet, invoice the billable
time from a new invoice, void the invoice and see the time unbilled. Save
two checks with Print later, print them on a real check printer from
Checks to Print, tick one as misprinted and reprint it; print the alignment
test page, hold it over the check stock, set the offset and print again.
Check the Time workspace and the dialogs in the dark and light themes and
at comfortable density.

## M67 Show editing gates (D-076)

Qualified on qinda (branch `m67-show-editing`, merged with
`m60-office-formats` 02eb244) with
`QQO_OOXML_SCHEMA_DIR=~/scratch/qqo/ecma-xsd`, xmllint and LibreOffice
25.2, as a targeted build (show_model, calc_io, the QindaShow app and their
tests only; qinda's disk is shared): **14/14 CTest targets passed,
56.01 s** (`ctest --preset dev -R "show|pptx|fodp"`). QtTest counts
(init/cleanup included), none skipped — the schema and LibreOffice steps
ran:

- show_model — `tst_show_model` 8, `tst_fodp` 7, `tst_pptx_read` 17,
  `tst_pptx_write` 9, **`tst_pptx_tables` 10, `tst_pptx_charts` 7,
  `tst_show_effects` 5**.
- QindaShow — `tst_qindashow` 12, `tst_show_ui` 19, `tst_show_design` 11,
  `tst_show_menu_export` 3, `tst_show_tools` 3, **`tst_show_table` 8,
  `tst_show_chart_notes` 5**: 124 QtTest functions in all (63 show_model,
  61 QindaShow), 0 failed, 0 skipped.

**Tables** (automated: `tst_pptx_tables`, `tst_show_table`):

- `rowsAndColumnsAroundMergedBlocks`, `mergeAndSplit`, `bordersAndFills`,
  `styleOptionsRestyleTheHeader`: the model operations — a row inserted
  inside a vertical block grows it, deleting a block's anchor row moves its
  text down, a column inserted inside a horizontal block grows it, a merge
  keeps every cell's text in the anchor and grows to whole blocks, a merged
  block re-divides only into parts that divide it, a single cell split 2×2
  gains grid lines the cells around it span, Outside borders set the facing
  edges of the neighbours, None is an explicit no-line, Header Row off turns
  the header's white bold into the body's look and a row inserted above the
  header takes the header's look.
- `editedTableKeepsUneditedCells`: the fixture's table (merged header,
  PowerPoint's default style) gets a header row and a first column above /
  beside it and a typed cell, is written and read back equal; the cells
  nobody edited ("A", the yellow "B") are their source `a:tc` serialised
  byte for byte at their new places; the old header, moved into the body,
  keeps its source `<a:rPr lang="en-US"/>`; `tableStyleId`, Header Row and
  Banded Rows are kept; an insert undone by a delete writes the source
  table verbatim.
- `newTableIsARealTable` (a PowerPoint deck and a QindaShow deck): a new
  table is an `a:tbl` graphic frame with `tableStyleId`
  {5C22544A-7EE6-4342-B048-85BDC9FD1C3A} and `firstRow="1" bandRow="1"`, no
  `b="1"` (the style carries the header's bold), and reads back equal.
- `libreOfficeConvertsTheEditedDeck`: an inserted row, a vertical merge, all
  borders and edited notes — LibreOffice's PDF has the 4 shown slides.
- Through the window: `insertTableLandsARealTable` (Insert ▸ Table, the
  Table Design / Table Layout context, one undo step),
  `clickIntoACellAndTabThroughIt` (a real click into a cell, typing, Tab /
  Shift+Tab, typed header text in the header's look, Tab in the last cell
  adds a row, each cell edit one undo step),
  `aClickIntoAnUnselectedTableEditsTheCell` (one click selects and edits; a
  click on the frame edge only selects), `commandsActOnTheSelectedCells`
  (insert, merge, split dialog defaults, shading, borders with the pen
  colour, Header Row, distribute, delete — each one step, all undone back
  to the inserted table), `boundaryDragResizesAColumn` (a real drag: +30 pt
  to the left column, the right one gives it up, one step),
  `powerPointTableEditedAndSaved` (a column inserted beside the merged
  header stays outside it; typed text keeps the cell's look; saved `.pptx`
  reads back equal).

**Charts** (automated: `tst_pptx_charts`, `tst_show_chart_notes`):

- `editedDataReachesCachesAndWorkbook` (the fixture's chart given an
  embedded workbook with an Excel table, as PowerPoint embeds it): a changed
  value, a fourth category, a renamed and a new series read back equal; the
  formulas are `Sheet1!$B$1 Sheet1!$A$2:$A$5 Sheet1!$B$2:$B$5 …
  Sheet1!$D$2:$D$5`; one chart part and one embedding in the package (the
  stale ones not carried); the embedded workbook re-read through calc_io
  holds North / South region / East, Q4, 7.5, 9, the blank, 1.25 and 4 where
  the formulas point, the untouched "Quarter" header, and its table over
  A1:D5; shrunk again, the cells the data left are empty and the table is
  A1:C4 again.
- `unchangedChartIsCarriedVerbatim` (a moved chart: part and workbook byte
  for byte), `tableOfAChart` (the sheet, unique Excel-safe series names,
  `parseValue`), `kindChangesRebuildThePlot` (line, pie, scatter, stacked
  bar, doughnut: each validates, reads back equal and has the axes its kind
  needs), `libreOfficeConvertsTheEditedDeck`.
- Through the window, `editDataReachesTheChartAndTheFile`: Edit Chart Data
  lights with a chart, the Chart tab appears, the dialog opens on the
  chart's numbers; an edited value, a new series and Stacked column land as
  one undo step; the saved `.pptx` reads back equal with its workbook.

**Notes and the presenter** (automated: `tst_pptx_tables`,
`tst_show_chart_notes`, `tst_qindashow`):

- `editedNotesKeepTheirNotesSlide`: a line added between two notes lines —
  the notes slide keeps its slide image placeholder and the unchanged lines'
  runs as written; a slide without notes gets a notes slide on the deck's
  own notes master; both read back.
- `notesPaneUnderTheSlide`: the pane is under the slide, typing into it
  sets the notes as one undo step, View ▸ Notes hides and shows it;
  `hostWiresDeckNotesAndUndo` now undoes the notes before the slide.
- `presenterViewOnOneScreen`: Presenter View on the offscreen platform's one
  screen shows the presenter and not the player, the current slide's notes,
  next preview and timer; advancing shows the next notes and "End of the
  show".

**Effects** (automated: `tst_show_effects`, `tst_show_design`):

- `writtenWithPowerPointPresets`: Wipe (22/4, `wipe(up)`), Split (16/37,
  `barn(outVertical)`), Spin (`emph` 8, `animRot by="21600000"`), Grow/Shrink
  (`emph` 6, `by 150000`), `<p:wipe dir="l"/>`, `<p:split orient="vert"
  dir="out"/>`; schema-valid; read back with the same effects, steps,
  durations and transitions; unchanged, the timing is written verbatim.
- `travelsThroughFodp`, `drawnAsTheyPlay` (half a wipe shows the bottom,
  half a split the centre, emphasis shapes are there before their step, a
  spin ends where it began, a grown shape stays grown in the show but not on
  the finished slide); `newTransitionsPlay` (the player plays wipe, split
  and push through the clip and shows the slide whole) and Spin's 2 s.

**UI gates**: the structural fixtures `qindashow_{1280,520}.txt` were
regenerated deliberately (`QQO_UPDATE_FIXTURES=1`; the diff is the notes
pane — `slideNotes` moved from the side panel into `NotesPane` under the
slide in `SlideEditor`'s splitter); the ribbon gate (every menu command on
the ribbon, every icon real, every tab — Table Design, Table Layout and
Chart forced on — inside the ribbon at 1280 and 520 px) and the panel
export's validator (the Table menu; ids once in the tree) pass. Grabs
looked at (`QQO_GRAB_DIR`): `qindashow_1280` (the notes pane under the
slide), `qindashow_table_editing` (Medium Style 2's blue header and bands,
the caret in the added row, the Table Design / Table Layout tabs),
`qindashow_chart_data` (the sheet lettered A–D, the new Series 3, Stacked
column chosen), `qindashow_notes_pane`, `qindashow_presenter` (the current
and next slide, the notes, the timer and counter; offscreen it keeps the
window's declared size).

**On the desktop (manual, the owner):** open one of your decks with a
table and a chart in QindaShow; in the table click into a cell and type,
Tab to the next, insert a row, merge two cells, shade one, drag a column
boundary; on the chart choose Edit Chart Data…, change a number and add a
series; type a speaker note under a slide; save as `.pptx` and open it in
PowerPoint — the table keeps its style and your edits, the chart shows the
new numbers and PowerPoint's own Edit Data opens a workbook with the same
numbers, the notes are on the slide's notes page, and a Wipe / Spin you set
plays in PowerPoint's slide show. Then try Slide Show ▸ Presenter View on
one monitor.

## M66 Calc gaps gates (D-075)

Qualified on qinda (branch `m66-calc-gaps`, merged with `m60-office-formats`
6e3368f) with `QQO_OOXML_SCHEMA_DIR=~/scratch/qqo/ecma-xsd` and xmllint, as a
targeted build (only the targets below; qinda's disk is shared): **43/43
CTest targets passed, 104.24 s** (`ctest --preset dev -R` over every calc
target, `tst_ooxml`, the four `tst_docx_*`, and the calc_io consumers
`tst_books_io_reports`, `tst_plan_output`, `tst_qindabase_service`,
`tst_qindabase_interchange`). QtTest counts (init/cleanup included):

- calc_engine — `tst_calc_parser` 57, `_functions` 5, `_recalc` 12,
  `_numfmt` 7, `_csv` 8, `_structure` 18, `_rules` 13, **`_spill` 11**,
  **`_features` 10**: 141 passed.
- calc_io — `tst_calc_io` 9, `_styles` 6, `_rules` 7, `_charts` 5, `_tables`
  4, `_view` 4, `_images` 5, `_excel` 9, `_package` 9, **`_links` 4,
  `_outline` 3, `_protect` 3, `_carry` 7, `_carry_sheet` 6, `_arrays` 5,
  `_libre` 5**: 91 passed.
- QindaCalc — `tst_calc_window` 12 (1 skipped), `_sample` 3 (8 skipped
  without `QQO_SAMPLE_XLSX`), `_ui` 16, `_rules_ui` 6, `_menu_export` 3,
  `_tools` 4, `_agent` 5, **`_features_ui` 7**, and `calc_agent_process`:
  56 passed.
- `tst_ooxml` 24; `tst_docx_read` 14, `_write` 6, `_restyle` 3, `_corpus` 2
  (1 skipped without a corpus).

The gates, per item (every package below is built by hand in the test —
`excel_package.h`, `carry_fixtures.h` — and both the source fixture and our
saved package validate against the Transitional XSDs; `brokenWiring()`
finds no relationship id that fails to resolve):

- **Links** (`tst_calc_io_links`): an External URL, `mailto:`, a place
  (`'Q1 Sales'!B2`) with no relationship, a range link with a `#fragment`,
  display text and tooltip survive our save and read back equal.
  `tst_calc_features`: ranges and places follow inserted/deleted rows and
  columns and a rename; targets and `HYPERLINK()` resolve.
- **Outlines** (`tst_calc_io_outline`): levels, hidden and collapsed rows and
  columns, `outlineLevelRow/Col`, `summaryBelow/Right="0"`; engine: groups
  collapse and expand (inner collapsed groups stay hidden), levels follow
  structural edits.
- **Protection** (`tst_calc_io_protect`): Excel's SHA-512 hash attributes
  verbatim, the permission flags, `lockStructure`, `locked="0"` /
  `hidden="1"` cell formats; engine: our hash equals Excel's for a known
  salt (cross-checked by an independent script), the legacy verifier,
  locked cells under protection.
- **Carried parts** (`tst_calc_io_carry`, `_carry_sheet`): pivot table +
  cache + records (`refreshOnLoad="1"`, `<pivotCaches>`), slicers,
  timelines and a shape with their caches, `extLst` uris and anchors; the
  pivot's source follows a rename and inserted rows, and goes with a deleted
  data sheet (warned); query table + connection, external link, data model
  + custom XML; threaded comment + reply + persons, a form control (shape
  ids renumbered consistently across control, VML, idmap and drawing),
  printer settings; `.xlsm` keeps the VBA project, its signature and code
  names, and an `.xlsx` save leaves it out with a warning.
- **Dynamic arrays** (`tst_calc_io_arrays`, `tst_calc_spill`): Excel's
  `cm="1"` + XLDAPR read as dynamic, written back with prefixes and
  `xl/metadata.xml`; a blocked spill saved as `#SPILL!`; SEQUENCE, FILTER,
  SORT, SORTBY, UNIQUE, RANDARRAY, dependents of spilled cells, blocking
  and unblocking, spills moved by structural edits, `A1#`.
- **LibreOffice re-opens the outputs** (`tst_calc_io_libre`, headless 25.2.7
  to flat ODS, svp backend, private profile; skips without soffice): links
  (external and `#Locked.B2`), the collapsed row group, the protected sheet,
  `cell-protect` none / formula-hidden, every spilled value; the carried
  pivot table is imported as a data pilot over `'Sales Data'.A3:B6` (the
  range after the rename and two inserted rows) with its total; the
  query-table, sheet-parts and macro workbooks re-open with our edit.
- **QindaCalc UI** (`tst_calc_features_ui`, the real window): Insert ▸ Link
  writes the link and its look; a plain click selects, Ctrl+click follows
  through an injected URL opener (nothing is launched); a place switches
  sheet and cursor; `HYPERLINK()` follows its evaluated target; Remove Link
  takes the look; `javascript:` is refused with a message; Group, Hide
  Detail, the gutter's +/− and level-1 buttons, one undo step; column
  groups; Hide/Unhide Columns and the cursor stepping over them; a locked
  cell refuses input with a status message, an unlocked one accepts it, a
  hidden formula shows its value, Bold and Insert Rows are refused, a wrong
  password keeps the protection; the structure lock refuses a new sheet;
  spills, `#SPILL!` and undo in the grid.
- **Fixtures** regenerated deliberately (`QQO_UPDATE_FIXTURES=1`): at 1280 and
  520 the only change is the Review ribbon tab and the Cells group's Hide &
  unhide button. **Grabs** looked at: the link and protect dialogs, the
  collapsed outline with its gutter, spills with their border beside a link
  and a two-level gutter, and the Review, Data and Insert tabs at 520 px.
  The ribbon gate (`tst_calc_ui`) and the menu export (every exported id
  resolves, the Review menu included) are green.
- Manual cross-checks on qinda (not committed): LibreOffice (UNO) refuses a
  wrong password and accepts the right one on the SHA-512 sheet hash we
  wrote, and its own `.xlsx` export keeps that hash verbatim. LibreOffice
  25.2 imports no workbook structure lock from any `.xlsx` (SHA-512, legacy
  `workbookPassword` or none), so that flag is checked by our own reader.
- Not run: the laptop survey of the owner's workbooks (optional; the laptop
  was under load). Not verified: Microsoft Excel itself (none here).
- **Desktop check (owner):** save, in QindaCalc, an Excel workbook that has
  a pivot table with a slicer, links, a grouped outline, a protected sheet
  and a dynamic-array formula; open it in Excel: the pivot table refreshes
  without a repair prompt and the slicer filters it, links open (a place
  jumps), the outline's +/− and level buttons work, the sheet asks for its
  password and accepts it, the formula spills; an `.xlsm` keeps its macros
  (QindaCalc never runs them). In QindaCalc on the desktop, Ctrl+click a
  link opens the browser, and the outline gutter reads in dark and light
  themes.

## M65 Diagram and Base gates (D-073, D-074)

Qualified on qinda (branch `m65-visio-base`, merged with `m60-office-formats`
1e398a9) with LibreOffice and `pdftotext` present and mdb-tools absent.
Targeted builds only (qinda's disk is nearly full, so the suite was not
rebuilt whole): the 23 CTest targets M65 touches all pass, no warning from
the M65 files. QTest totals (test functions plus init/cleanup):
`tst_ooxml` 23 (1 skipped: the schema helper without
`QQO_OOXML_SCHEMA_DIR`), `tst_launcher` 8, `tst_diagram_model` 12,
`tst_diagram_ops` 13, `tst_diagram_output` 10, `tst_diagram_visio_read` 12,
`tst_diagram_visio_write` 6 (the LibreOffice check ran),
`tst_diagram_visio_roundtrip` 7 with `QQO_VISIO_CORPUS` (6 + 1 skipped
without), `tst_qindadiagram_host` 10, `tst_qindadiagram_menu_export` 4,
`tst_qindadiagram_ui` 9, `tst_base_types` 6, `tst_base_file` 12,
`tst_base_schema` 11, `tst_base_records` 9, `tst_base_query` 9,
`tst_base_io` 7, `tst_base_access` 7, `tst_qindabase_service` 9,
`tst_qindabase_interchange` 6, `tst_qindabase_host` 7,
`tst_qindabase_menu_export` 4, `tst_qindabase_ui` 13.

**Visio (automated)** — on hand-built packages (`visio_fixture.h`):

- `tst_diagram_visio_read`: masters lend cells, rows and text (deleted rows
  stay deleted); style chains; groups flatten with composed transforms;
  connectors glue through `<Connects>` and `PAR(PNT(…))`/`_WALKGLUE`; text
  runs, block and fields; every geometry row kind becomes a path; pictures
  keep their bytes; page names, sizes and scale; themed colours; the
  2003–2010 formats refused with the message.
- `tst_diagram_visio_write`: a fresh drawing's package (content types,
  properties with `RecalcDocument`, the six standard style sheets, explicit
  geometry, text, glued connectors with `<Connects>`, pictures, page size;
  cells before sections), template and macro kinds, DrawingDocument open /
  save / crash recovery, and **LibreOffice**: the fresh and the edited
  export converted to PDF headless, every label found by `pdftotext`.
- `tst_diagram_visio_roundtrip`: an unchanged drawing writes every shape
  element as read; an edit rewrites only what changed; a moved group moves
  as one; a resized page keeps shapes in place. Corpus (`QQO_VISIO_CORPUS`,
  run locally on two public Microsoft Visio 2013 sample files, never
  committed): **2 files, 2 pages, 41 shapes, 14 connectors, 0 failures**
  (read, write, re-read, compare).
- `tst_qindadiagram_host` `visioDrawingsOpenAndSave` (open, edit, save,
  reopen, the status-bar warnings) and `tst_qindadiagram_ui`
  `visioDrawingRenders`.
- Looked at: `qindadiagram_visio_1320` (the fixture in the window),
  LibreOffice's renders of the fresh and the edited export and of the two
  sample drawings after a round trip.

**Base (automated)**:

- `tst_base_types` `fileFieldsKnowWhatTheyHold` (the type row, signatures,
  summaries, sizes, names, the 32 MB bound) and `tst_base_records`
  `fileFieldsPersistUndoAndStayOutOfReads` (FILE columns, BLOB storage,
  reads that summarise, undo, refusals, filter by presence, a rebuild, no
  conversions, the form mirror leaves them out).
- `tst_base_access` over hand-written captures of mdbtools 1.0's output
  (`mdb-schema … postgres`, `mdb-export -b hex/-b strip`, `mdb-queries`):
  the schema parser (types, required, descriptions, keys, indexes,
  enforced and unenforced relationships), the query conversion (criteria,
  TOP, sorts, expressions, Between, aliases for renamed tables, and every
  refusal with its reason), a whole import through fake mdb-tools programs
  with the real command lines (design, AutoNumber values, Currency vs
  sub-cent decimals, dates, GUIDs, OLE bytes as a file field, a multi-field
  index, the cascade relationship, four converted queries that compile and
  run, five reported ones and the hidden `~sq_` one skipped, a one-step
  undo), and the no-schema fallback.
- `tst_qindabase_interchange`: Excel format codes and serials; an Excel
  sheet (a cover sheet first, a blank row, every cell kind) proposes the
  `id` key, Short text (a leading-zero code too), Date, Date & time,
  Currency, Decimal number, Yes/No, Long text and a mixed column as text,
  and imports as one step; `.xls` refused; a table of
  every type exported to `.xlsx` and imported again has the same types and
  values; file fields through the service (insert, preview, save, remove,
  undo, the 32 MB refusal, the datasheet's text, icon and filter, CSV
  export).
- `tst_qindabase_ui` `excelAccessAndFilesRender`; the ribbon gate (every
  new command on the ribbon) and `tst_qindabase_menu_export` (56 commands,
  each once, each triggering).
- Fixtures `qindabase_1280/520.txt` regenerated deliberately
  (`QQO_UPDATE_FIXTURES=1`): the hidden datasheet file panel and form file
  strip, the Home tab's File field group, three large Home buttons squeezed
  one step at 1280 px, three fewer grid rows at 520 px under the taller
  ribbon.
- Looked at: `qindabase_excelimport_1280` (sheet chooser, cell-typed
  columns, first rows), `qindabase_import_1280` (CSV with the preview),
  `qindabase_accessreport_1280`, `qindabase_filefield_1280` (icon in the
  cell, thumbnail panel, File field group enabled) and
  `qindabase_formfiles_1280`.

**Desktop checks (owner, not run here)**: open an exported `.vsdx` (fresh
and edited) in Microsoft Visio — no repair prompt, shapes, text, glue and
pictures as shown; open a `.vsdm` and check its macros are still there;
double-click a `.vsdx` in the file manager (MIME → QindaDiagram); with
`app-office/mdbtools` installed, import a real `.accdb` and an old `.mdb`
and read the report; open QindaBase's `.xlsx` export in Excel (dates are
dates, money shows its symbol); insert a large photo into a file field and
check the thumbnail and Save File As in the dark and the light theme.

## M64 Mail essentials gates (D-072)

Qualified on qinda (branch `m64-mail-essentials` over `m60-office-formats`):
the mail and launcher targets only (`ctest --preset dev -R "mail|launcher"`,
the lead qualifies the whole suite after merging): **25/25 CTest targets
passed (43.8 s), 468 QtTest cases, 0 failed** (counts include init/cleanup).
New M64 programs: `tst_mail_messages` 13, `tst_mail_contacts` 14,
`tst_mail_contact_csv` 8, `tst_mail_contact_store` 15, `tst_mail_msg` 49,
`tst_mail_html` 137, `tst_qindamail_files` 11, `tst_qindamail_people` 7,
`tst_qindamail_meetings` 5; extended: `tst_qindamail_host` 10,
`tst_qindamail_ui` 16, `tst_launcher` 8; unchanged and green: the M43/M44
mail, Exchange and QindaMail programs. The M64 programs passed five
repeated runs without a flake. Build warnings: none.

- **Library** (`tests/office_mail`, no network, every fixture written by the
  test): `tst_mail_messages` — the RFC 5322 reader (headers, encoded words,
  alternative/related structure, Content-ID, `start`, a calendar part, an
  attached message, garbage and 40-level nesting without a crash) numbers
  every part exactly as `QindaTK::Mime::parse` does; the writer is
  deterministic, CRLF-only and reads back through both readers (inline
  parts, attached messages, Exchange-only names as empty groups); mbox
  separators, `>From` unescaping, oversized and counted limits; the renderer
  resolves `cid:` pictures, blocks remote ones until allowed, shows attached
  pictures below and escapes and links plain text. `tst_mail_html` — the
  sanitizer against a hostile corpus (scripts, handlers in every quoting,
  obfuscated `javascript:`, CSS `url()`/`expression()`, `base`, `meta`,
  forms, SVG, tracking pixels, hidden preheaders), each output loaded into a
  `QTextDocument` whose resource requests are recorded: no http(s), file or
  script URL is ever asked for unless pictures are allowed. `tst_mail_msg` —
  MS-CFB v3/v4 (mini stream, DIFAT, every corruption and truncation failing
  cleanly), MS-OXMSG mapping, MS-OXRTFCP (the specification's vectors, CRC
  and bounds), MS-OXRTFEX de-encapsulation. `tst_mail_contacts` — vCard 2.1
  (Outlook: QUOTED-PRINTABLE, CHARSET, base64 photo), 3.0, 4.0, folding,
  round trips, unknown properties, Outlook/Google CSV, the store's bounds,
  merge, search and persistence.
- **App** (`tests/qindamail`): `tst_qindamail_files` — `.eml` kept with its
  source (formatted body, cid picture, attachments read from the source,
  same file = same message, survives a restart), Save Message As writes the
  original bytes (and a source-less POP3 message from its record), Save All
  with unique names and without in-body pictures, an mbox into its own
  folder (and "(2)" for a second one), pictures fetched only after Show
  pictures (an injected fetcher; no network) and "always for this sender"
  kept, links only for http(s), an IMAP message downloaded whole once
  (`RFC822.SIZE`, then `BODY.PEEK[]`) from a local IMAP server and its
  attachments then read from the kept source, an Outlook `.msg` (built by
  the CFB writer) opened as an ordinary message — formatted body, cid
  picture, attached message, the same file twice = one message, saved as
  `.eml` and read back, a truncated file refused with a notice — and the
  selected message printed to PDF. `tst_qindamail_people` — contacts beside the mail
  store, Add Sender (once), suggestions (contacts first, then mail, one entry
  per address, the TokenField's later-turn contract), a `.vcf` handed to the
  app opens People, the People workspace selects, edits, saves and starts a
  new contact in the real window, and typing "tur" in the composer's To
  field offers "Alan Turing <alan@example.org>", which a click turns into a
  recipient (`qindamail_composer_suggestions`). `tst_qindamail_meetings` — an invitation
  over IMAP: Accept puts it in the calendar and sends METHOD:REPLY with
  PARTSTAT=ACCEPTED through SMTP (local fakes), Tentative keeps one event,
  Decline removes it; an invitation opened from a file updates the calendar
  only, and its later CANCEL removes the meeting without sending anything; an event with invitees goes out as METHOD:REQUEST (organizer,
  RSVP attendees, SEQUENCE 0) and as "Updated invitation" with SEQUENCE 1
  the second time. `tst_qindamail_host` — 41 command ids with shipped icons, unique
  shortcuts (Ctrl+4, Ctrl+Shift+C, F12), print enabled by a selection, the
  People menu in the panel export (the desktop's validator accepts it, every
  id resolves). `tst_qindamail_ui` — HTML mail renders formatted on paper
  with the cid picture, the pictures bar blocks the remote banner until Show
  pictures is clicked (1280 and 520 px); an invitation shows its meeting
  card (the calendar part is not listed as an attachment) and Accept puts it
  in the calendar (`qindamail_meeting_1280`); the ribbon gate (coverage, icons,
  520 px squeeze) holds in Mail, Calendar, Tasks and People; fixtures
  `qindamail_{1280,520}.txt` regenerated (`QQO_UPDATE_FIXTURES=1`) and the
  diff reviewed.
- **Grabs reviewed** (`QQO_GRAB_DIR`, qinda, offscreen software renderer):
  `qindamail_html_blocked_{1280,520}` (HTML on paper: heading, bold,
  italic, the inline cid picture, the blocked banner's alt text, list, link;
  the pictures bar), `qindamail_html_pictures_{1280,520}` (the banner after
  Show pictures, the paper still wrapping the body), `qindamail_people_{1280,520}`
  (list with initials, the card with typed e-mail rows), and the existing
  mail/calendar/tasks/composer/account grabs. At 520 px the mail panes now
  stack (folders · list · reader) so the reader and its bar stay inside the
  window. Found and fixed while looking: Tk.Flex sizes children from their
  implicit size (paper, avatar, photo used `height:` and collapsed).
- **Owner's files** (laptop, parsed in place by a throw-away CLI built from
  the parser sources only, aggregate counts only): 23 `.eml` — 23 parsed,
  0 failures; 17 with an HTML body, 5 text only, 1 without a body; 6 with
  inline pictures, 9 inline parts, all 9 `cid:` pictures resolved; 2 with
  attachments (3); remote content blocked in 15; part numbering identical to
  `QindaTK::Mime::parse` in all 23; the RFC 5322 writer round trip identical
  (subject, bodies, attachments) in all 23; slowest sanitize 1 ms. The one
  `.msg` file there is neither an Outlook compound file nor a mail message,
  so the Outlook path is covered by the generated files of `tst_mail_msg`
  and `tst_qindamail_files` only.

**Manual, on the desktop** (with the owner): open an Outlook `.msg` from
the file manager (body, attachments, an attached message); import a vCard
exported from Outlook (2.1, with a photo) and an Outlook CSV; open an HTML
newsletter (pictures blocked, Show pictures, Always show from this sender,
then Stop); click a link (browser) and a `mailto:` link (composer); type two
letters of a contact in To and pick the suggestion; Print Preview and print
a message with an inline picture; Export as PDF; Save Message As, then open
the `.eml` in Outlook; import a Thunderbird mbox; accept a meeting invitation
from a real account and check the organizer's calendar; invite a colleague
from the event dialog and accept on their side.

## M62 PowerPoint interchange gates (D-070)

Qualified on qinda (branch `m62-powerpoint-interchange`, rebased onto
`m60-office-formats` 4c066e3) with `QQO_OOXML_SCHEMA_DIR=~/scratch/qqo/ecma-xsd`
and xmllint: **152/153 CTest targets passed, 369.50 s** (full
`cmake --build --preset dev`, then `ctest --preset dev`; no warning from
the M62 files). The one failure is not M62's: `tst_write_pagination`
`breaksStayOutOfTheFiles` (a reopened `.docx` lays out 0 keep-with-next
breaks where 1 is expected) fails the same way on 4c066e3 alone in the
same build tree. On 906a9f3, before that commit, the branch passed
152/152 (372.39 s). QTest totals (test functions plus init/cleanup), none
skipped — LibreOffice and the schema step ran: `tst_fodp` 7,
`tst_show_model` 8, `tst_pptx_read` 17, `tst_pptx_write` 9, `tst_ooxml` 21,
`tst_qindashow` 12, `tst_show_ui` 19, `tst_show_design` 10,
`tst_show_menu_export` 3, `tst_show_tools` 3, `tst_launcher` 8. Each step
commit below was also built on its own and passed these suites as they
stood at that step.

**Step 1 — `.fodp`** (automated: `tst_fodp`, `tst_show_model`):

- `roundTripIsLossless`: a deck with rotated shapes, lines and arrows in all
  four directions, alpha fills, invalid colours, spaces/tabs/line breaks,
  five alignments, three outline levels of bullets, an un-bulleted level,
  empty paragraphs and runs, builds, groups, extras and a master whose id
  starts with a digit reads back `==` the model, and writing it again
  reproduces the file byte for byte.
- `writesValidOdf`: every `draw:style-name` / `text:style-name` /
  `text:list` style / marker / master page the body names is defined; no
  `fo:` attribute on `text:span` / `text:p`; every `svg:` length has a unit;
  a rotated shape has `rotate … translate` and no `svg:x`.
- `readsPreM62Files` (unit-less cm read as cm, the unlinked style adopted,
  the old clockwise rotation kept) and `readsLibreOfficeStyles` (parent
  chains, `rotate(π/6) translate(5cm 5cm)` = 330° clockwise centred at
  (7.232 cm, 4.866 cm), a start-only marker, list levels without a label,
  `draw:g`, `text:s` in notes).
- `shippedTemplatesLoadAtSize`: both templates are 720×405 pt with every
  shape inside the slide and every length carrying a unit.
- `fodpConvertsToPptx` (LibreOffice live): after `.fodp → .pptx` the title
  box is at (40, 30) pt and the rectangle at (60, 140) pt with its fill.
- Looked at: LibreOffice's PDF of a regenerated template, and of a deck
  with a 30° clockwise rectangle, both arrow directions and filled shapes.

**Step 2 — the model and the renderer** (exercised through step 3's
fixture; the pre-M62 suites stay green with true point sizes):

- `rendersEverySlide`: every fixture slide draws on the master's ivory
  background with the master's logo, none blank; the grabs
  (`pptx_read_slide1..5.png`, `QQO_GRAB_DIR`) were looked at: placeholder
  text at the master's and layout's sizes, bullets and numbering, the merged
  table in the default style, the cropped picture, the nested group, the
  chart from cached values, the gradient rounded rectangle and the dashed
  connector.

**Step 3 — the native reader** (automated: `tst_pptx_read`, a package
built by hand in `pptx_fixture*.h`: master, layout, theme, five slides,
notes, a picture, a chart part, an unknown frame):

- `placeholdersInheritFromLayoutAndMaster`: a title takes the master's
  position and 44 pt, a body the layout's position and its 20 pt accent2
  level-1 style, `normAutofit` scale kept.
- `themeColoursWithLumMod` (accent1 at 75 % luminance through the colour
  map), `bulletsAndAutoNumbering` (`arabicPeriod`, `alphaLcParenR` from 3,
  bullet characters per level, a line break, superscript),
  `tableWithMerge` (grid span and `hMerge`, PowerPoint's default style
  supplied when only named), `croppedPicture`, `nestedGroup` (child
  transforms two levels deep), `chartFromCachedValues` (categories, two
  series, colours), `unknownFrameIsPreserved` (fragment and part kept),
  `styleMatrixConnectorAndHyperlink` (`p:style` fill/line/font references,
  connection sites, dash, tail end, a click hyperlink),
  `notesHiddenTransitionsBuilds`, `readsDeckAndMasters`,
  `refusesWhatIsNotAPresentation`.

**Step 4 — the native writer** (automated: `tst_pptx_write`, `tst_ooxml`):

- `roundTripIsEqual`: fixture → model → `.pptx` → model is `==`, twice.
- `unknownFrameComesBackByteForByte`: the unknown graphic frame is written
  as it was read.
- `editsAreWrittenAndTheRestKept`: a moved text box, a retitled placeholder
  (still a placeholder, still 44 pt from the master), a recoloured group
  member, an edited table cell, a deleted and an added slide — the edits
  read back, everything else `==`, the hidden slide untouched.
- `generatedDeckReadsBack`: a QindaShow deck (no source package) gets a
  generated master, layouts, theme and notes master and reads back.
- `packagesValidate` (`QQO_OOXML_SCHEMA_DIR`): the round trip, the edited
  deck, the generated deck and the fixture through `.fodp` validate against
  the ECMA-376 Transitional schemas (Markup Compatibility processed).
- `powerPointRules`: `sldId` ≥ 256, unique `sldMasterId`/`sldLayoutId` ≥
  2147483648, one layout relationship per slide, unique shape ids per
  slide, notes slides → notes master, a content-type override per part —
  for the fixture, an empty deck and the fixture through `.fodp`.
- `libreOfficeConvertsToPdf` (LibreOffice live): our `.pptx` of the fixture
  gives 4 pages (the hidden slide left out), the generated deck 2, and the
  fixture's `.fodp` 4.
- `tst_ooxml`: `keepsBracedGuidAttributes`, `copierResolvesMappedParts`.

**Step 5 — QindaShow** (automated: `tst_qindashow`, `tst_show_ui`,
`tst_launcher`):

- `documentPptxSave`: an untitled deck saves as a real `.pptx` (`PK`),
  reopens with its notes and hidden slide, and saves again carrying what it
  was opened from.
- `recoveryAndFiltersPreferPptx`: the journal is a `.pptx` under the
  journal's binary bound, a pre-M62 `.fodp` journal still restores, `.pptx`
  is the first save filter and `.ppt` opens.
- `controllerSkipsHiddenSlides`; `textBridgeKeepsRuns` (an edit keeps
  bullets, numbering, spacing, indents, baseline, underline, links).
- `powerPointDeckOnTheCanvas`: the fixture opens in the real window (5
  slides), slides 1, 2 and 4 grabbed show the ivory master; the title edits
  in place inside its 7.2/3.6 pt insets and an unchanged edit leaves the
  document clean; the table is not edited as text.
- `tst_launcher`: `.ppt` opens QindaShow.

**`.fodp` keeps the extended model** (automated: `tst_show_model`,
`tst_pptx_read`, `tst_fodp`):

- `clipboardCarriesEveryShape`: the clipboard codec that `qq:shape` uses is
  total for the M62 model — each of the fixture's 10 shapes (table, chart,
  group, preserved frame, connector included) pastes back `==` but for its
  identity.
- `fodpCarriesTheOfficeModel`: an Office arrow with adjust values, flip,
  gradient, dashed outline and rich text, a merged table, a hidden slide
  with its own background, a master with a gradient and a decoration, a
  layout — read back `==` and re-written byte for byte; LibreOffice sees a
  `right-arrow` and a hidden page.
- `savedAsFodpLooksTheSame`: every fixture slide saved as `.fodp` draws
  pixel for pixel as before, the file re-writes byte for byte, and the
  `.pptx` written from it keeps the master's logo and background.
- `readsLibreOfficeStyles`: LibreOffice's `right-arrow` and `ooxml-chevron`
  read as those presets; `presentation:visibility="hidden"` hides a slide.
- Looked at: LibreOffice's PDF of the fixture's `.fodp` (master background
  and logo on every page, the table, group and chart as pictures, the
  rotated rounded rectangle), and of every mapped ODF shape type.

**Real decks** (a laptop-only probe; the decks never leave the laptop and
only counts are recorded): the owner's 18 decks — 333 slides, 7834 shapes
(4312 text boxes, 1240 preset shapes, 816 ellipses, 794 rectangles, 584
pictures, 66 lines, 22 tables) — all read without a warning, round-trip to
an equal model and write packages with 0 schema errors; saved as `.fodp`,
333 of 333 slides draw pixel for pixel the same, all 18 re-write byte for
byte, and the `.pptx` written from the `.fodp` reads back and validates for
all 18. Seven sample decks (139 slides, 1336 shapes, 16 charts, 22 tables):
all read, round-trip equal and draw the same through `.fodp` (139 of 139);
5 of their written packages report schema errors (3, 6, 3, 2, 2), all in
chart parts carried verbatim, which fail identically in the samples
themselves (`c15:leaderLines` in a chart extension).

## M61 Excel interchange gates (D-069)

Qualified on qinda (branch `m61-excel-interchange`, merged with
`m60-office-formats` 2d47dfa) with `QQO_OOXML_SCHEMA_DIR=~/scratch/qqo/ecma-xsd`
and xmllint: **143/143 CTest targets passed, 342.37 s** (full
`cmake --build --preset dev`, then `ctest --preset dev`). The calc gates:
`tst_calc_io_excel` 9/9 and `tst_calc_io_package` 9/9 (7 test functions
each plus init/cleanup), the seven older calc_io programs with the schema
step on every package, `tst_calc_parser` 57, `tst_calc_structure` 18,
`tst_calc_functions` 5 (519 fixture rows), and the eight QindaCalc app
targets (`tst_calc_window`, `_sample` — skipped without `QQO_SAMPLE_XLSX` —,
`_ui`, `_rules_ui`, `_menu_export`, `_tools`, `_agent`, `calc_agent_process`).

- **Schema gate, every calc_io test.** `QQO_VERIFY_XLSX_SCHEMA(path)` follows
  each of the **35** package writes across the nine calc_io test programs;
  every part with an ECMA-376 root validates against the Transitional XSDs
  after Markup Compatibility preprocessing (`tests/calc_io/calc_io_schema.h`).
  One documented allowance: `xml:space` on SpreadsheetML `<t>`, which Excel
  writes and `sml.xsd` does not declare. Only the schema step skips without
  the XSDs or xmllint.
- `tst_calc_io_excel` (**7 functions**, packages built by hand the way Excel
  writes them, `excel_package.h`): untyped numbers, shared strings with rich
  and phonetic runs, multi-run inline strings, `t="b"/"e"/"str"/"d"`,
  `_x000D_`, typed cached results written back (`t="str"/"b"/"e"`); `_xlfn.`,
  `_xlfn._xlws.` and `_xlpm.` stripped and restored, an unknown prefixed
  function verbatim, typed formulas prefixed, no `;` in any saved formula;
  text starting with `=`, URLs, HTML and control characters stay text; a
  structured reference and intersection formulas (shared, token-shifted)
  stay formulas; `$A:$A` evaluates; shared formulas keep their parentheses;
  a formula on an earlier sheet depends on a later one; the spelling
  transforms both ways (nested LET, LAMBDA calls, structured references,
  array constants).
- `tst_calc_io_package` (**7 functions**): the source theme and properties
  survive (no "Qt Xlsx Library", Application QindaCalc); seven defined names
  keep scope, hidden, comment and body and evaluate (sheet-scoped,
  constant, formula, `_xlfn.` in a name); `'2024'`, `'Q1 Sales'`, `'It''s'`
  quoted in print areas, titles and names; table column names from their
  header cells (`name2`, `2024`, `Column4`); long and comma lists written as
  ranges, a range list on a later sheet kept live; comment VML with
  `<legacyDrawing>`, 1500 notes taking two shape-id blocks; a dialog chart
  plus a picture make a well-formed drawing.
- Engine: `tst_calc_parser` (printing keeps grouping — 25 data rows —,
  anchored whole columns/rows, sheet-name quoting), `tst_calc_structure`
  (quoted-sheet references follow inserts and renames; defined names
  evaluate and follow inserts, moves and removal), `tst_calc_functions`
  (**121** new LibreOffice-oracle rows in `functions_m61.tsv`, 519 rows in
  all; `excelRulesBeyondTheOracle` for TEXTBEFORE/TEXTAFTER and the cases
  where LibreOffice 25.2 departs from Excel's documented answer).
- QindaCalc: the undo snapshot and the recovery payload carry the defined
  names, the source theme and properties, and validation list sources; the
  QindaCalc app tests stay green.
- LibreOffice cross-check (qinda, manual, not committed): every package the
  two new tests save converts with headless LibreOffice 25.2.7 to CSV with
  the values the engine computed (names 6/60/0.07/3/1, shared formulas
  14/22/−5/−1723, quoted names, table headers, text kept as text).
- Laptop survey (optional gate; the owner's 29 `.xlsx`, read in place,
  aggregate counts only, `calc_io_survey`): before — 0 load failures, 15
  `#NAME?` cells, 8,261 text cells that read as numbers, 54–61 error cells
  varying by run; after — 0, 0, 2,880 (all stored as text by Excel itself),
  12 (all `=NA()`), identical on every run.
- Not verified: opening the saved packages in Microsoft Excel itself (none
  available here). On the desktop: open an Excel-made workbook, save, reopen
  in Excel and LibreOffice; check notes show, a long dropdown offers its
  items, the theme fonts and the title/author in File ▸ Info.
## M60 Word interchange gates (D-068)

Laptop (incremental, against Portage-installed QindaTK): `tst_ooxml` 18/18,
`tst_docx_read` 13/13, `tst_docx_write` 6/6 with every written package
validated against the ECMA-376 Transitional XSDs (`QQO_OOXML_SCHEMA_DIR`),
`tst_write_docx` 7/7 (+1 opt-in render), the QindaWrite suite 9/9.

**Real documents** (`tst_docx_corpus` over the owner's 590 `.docx`, 96 %
written by Microsoft Word; counts only, content never copied): 590 read, 0
failures, ~1 s per document worst case; 135,604 paragraphs, 8,792 tables, 545
pictures, 12,470 list items, 505 preserved objects. Every document written
and re-read with identical plain text, table count and picture count.

**Visual review** (`QQO_WRITE_RENDER_DOCX`, compared page by page with
LibreOffice's rendering of the original): a developer spec, a developer
brief, a 25-page training guide and a 14-page forms packet. Page counts
match on three; the guide differs by keep-with-next (known limit). Fixed on
the way: lost cell shading/merges when a cell's first paragraph format was
set, prefixed OPC package parts LibreOffice refused, first-block spacing,
collapsed empty paragraphs after tables, row heights, paragraph borders,
header overlap, cover-page headers, PDF page size (px passed as points).

**Headers and footers in place:** `tst_write_story` 4/4 — Edit Header
switches the editing control onto the default header, typing and PAGE /
NUMPAGES fields land there (the body untouched), Close returns, the saved
`.docx` reopens with "Acme — page {page} of {pages}"; the first page edits
its own slot when different-first-page is on; a double-click on the top
band opens the header, Esc closes, the bottom band opens the footer and a
double-click on the text returns; Page Setup replacing the header closes
the editor first (the control stays alive on the body). Looked at: the page
with the header open (header text, dashed boundary, "Header" tab, the body
washed out). Desktop check (owner): double-click a footer in a real
document, edit it, save, open in Word.

**Document styles:** `tst_docx_restyle` 3/3 — the document's own Heading 1
(16 pt, its theme colour, keep-with-next) replaces the paragraph's direct
centring, the run's own bold stays, the next paragraph's folded spacing
follows, the output says `<w:pStyle w:val="Heading1"/>` only and validates,
and it reads back as Heading 1. `tst_write_review` checks that a style
applied to a written line restyles the text itself (it used to change only
the paragraph mark).

**Pictures:** `tst_write_picture` 3/3 — hit test and selection, a corner
handle scales in proportion and a side handle stretches, each drag one
undo step; a click selects through the page item and a double-click asks
for the Picture dialog; size and alt text land in the saved `.docx`
(`descr`, `cx`); a Word picture resized keeps its crop and name with both
extents patched. `tst_write_ui` adds the Picture command (enabled only with
a picture selected, the dialog opens, `applyPicture` resizes). Looked at:
the dialog over a selected picture (handles, no selection tint).

**Comments:** `tst_docx_comments` 8/8 (XSD-validated on qinda) — ranges
over two paragraphs, reference marks, a reply and its resolved state read;
an untouched set copied byte for byte; an edited, resolved or new comment
written from the model; a comment whose text and mark were deleted dropped;
a comment marked only in the header kept through a rewrite.
`tst_write_comments` 3/3 through the real window — a Word document's
comment listed with its author and quoted text, Show Comments opens the
pane with its card, New Comment enabled only with a selection, a new
comment, a reply, a resolve and an edit land in the saved `.docx` (three
comments, the reply's parent, the resolved flag), Delete removes the thread
and keeps the text. The fixtures gain the hidden Comments dock (+7 rows
each) and `tst_write_menu_export` resolves the five new Review ids. Looked
at: the pane beside the page (the Word comment's card with its quote, the
new card, the commented text shaded). Desktop check (owner): open a
reviewed Word document, reply to a comment and resolve another, save, open
in Word — the thread, the reply and the resolved state as written.

**Word forms (D-080):** `tst_docx_controls` 7/7 — a block-level control
around two paragraphs and a table (the table tagged, its cell not), inline
check box, drop-down, date and bound plain-text controls read with kinds,
symbols, items, formats and text; an unchanged save writes every `w:sdtPr`
back identically and keeps the block control around the table; a ticked
box, a chosen item, a date and new text write `w14:checked="1"`,
`w:lastValue`, `w:fullDate` and drop `w:showingPlcHdr`/`w:dataBinding`; a
copied check box is written twice with one `w:id`; `controlAt` finds each
span. `tst_write_controls` 3/3 through the real window — a click beside the
check box does nothing, one on its symbol ticks it and Undo clears it; the
list opens at the click with its three items and an item replaces the
placeholder; the calendar opens and a day lands as `10/8/2026`; a click on
a text placeholder selects it and typing replaces it in ordinary ink; the
saved file carries every state. The fixtures gain `FormFieldPopup` (+1 row
each). LibreOffice renders the edited form (☒, Green, 10/8/2026). The
owner's 590 `.docx` (8 with content controls) read and round-trip with 0
failures, 0 text/table/picture changes and 0 schema-invalid packages
(`QQO_DOCX_CORPUS_VALIDATE`); qinda: 189/189 targets at 1d629a3. Looked
at: the list menu at the click; the calendar popover. Desktop check
(owner): open a Word form, tick a box, pick from a list and a date, save,
open in Word — the boxes, choices and date as set, no repair prompt.

**HTML autospacing:** `tst_docx_read` `autoSpacingFollowsHtmlRules` — 14 pt
between auto-spaced paragraphs (the larger of two, not the sum), none
between items of one list, none at a table cell's top and bottom. The
owner's worst-paginated document, a Word file converted from HTML (Word: 25
pages), went from 37 pages in Write to 24. Over the 567 documents: 383
exact / 101 off by one / 47 shorter by 2+ / 36 longer by 2+ (3,630 pages;
Word 3,666); on the 211 whose rendered page breaks agree with Word's count,
156 exact and 198 within one (were 141 and 186).

**Line metrics:** `tst_docx_read` `linesFollowWordsFontMetrics` — a laid-out
line pitch equals Word's single line for the drawn font × the paragraph's
own multiple, and the saved paragraph carries no spacing it did not have.
Page counts against Word's own (`QQO_WRITE_PAGES_CORPUS`, 567 documents):
before 369 exact / 94 off by one / 44 shorter by 2+ / 60 longer by 2+
(3,951 pages); with Word's line metrics 354 / 106 / 47 / 60 (3,910; Word
3,666).

**Fonts:** `media-fonts/crosextrafonts-carlito` and `-caladea` (metric
clones of Calibri and Cambria) are installed on both machines through
Portage; with them the 25-page guide lays out on 25 pages, as in Word.

**Pagination (keep rules):** `tst_write_pagination` 8/8 — a keep-with-next
paragraph, a run of them, an orphaned first line, widow control switched
off, a table split after its keep-with-next title row, no break in the
`.docx` or HTML, a reopened file laid out again, one Undo taking back an
edit together with its breaks and Redo intact, 3,000 paragraphs (75 pages)
in under 0.1 s. Against the page counts Word itself recorded in the four
review documents (`docProps/app.xml`: 5, 4, 25, 15) Write now lays out 5, 4,
23 and 14; LibreOffice 5, 4, 25, 14. The guide's first eight pages break
exactly where LibreOffice's do (they did not before); the rest of the gap
is line metrics: Calibri is not installed on either machine and its
substitute sets lines about 5 % shorter (11 pt: 17.0 px vs Word's 17.9 px).

**QindaNote Word export (M65):** `tst_note_word` 3/3 (XSD-validated; the
page's title, Heading 2, list, typed text, checklist, picture and handwriting
read back), the Note suite 16/16 with the ribbon/squeeze gate and the
`sessionExportWord` fixture row. Looked at: the exported page rendered by
LibreOffice (title, blue Heading 2 at 16.5 pt, bullets, ☑ line, picture,
italic "Handwriting" caption and the ink picture).

On the desktop (owner, Microsoft Word): open a `.docx` saved by QindaWrite
in Word — no repair prompt; styles, lists, headers with logo, page numbers,
footnotes and tracked changes as written. Open a QindaNote page exported as
Word: headings in the navigation pane, picture and handwriting present.

## M59 QindaNote AI gates (D-065)

Qualified on qinda with Portage-installed QindaTK r14 (the r13 agent SDK):
**134/134 full-suite targets passed, 374.70 s**. After the model-prompt and UI
refinements, **14/14 Note/live-notes targets passed, 55.77 s**. Reviewed the
1280x800 and 520x800 follow-up PNGs: preview, kind picker and Accept/Discard
fit; the narrow view preserves page space by hiding the other side panels.
Geometry evidence: `TextArea#sessionDraftPreview at 968,334 304x140.625`
in the 1280 px window; `at 212,411 300x140.625` in the 520 px window.
Final optional-capture cleanup passed the three live/session/follow-up targets
(22.40 s); the two geometry cases passed separately with QML warning failures
enabled.

Five synthetic workflows ran through the actual Note/model path using Ornith
on qinda; planning/coaching were rechecked after grounding refinements.
[Recorded inputs, outputs and observations](verification/qindanote-ai-20260929.json)
include the initial failures. The final planning sample retains the undated
promise, leaves the signing-fix owner unknown and preserves the proposed date
as a proposal. Models still sometimes add unrequested recap/boilerplate;
these examples do not establish a universal accuracy or latency guarantee.

- `tst_note_followup`: real Note window and HTTP provider; read-only questions
  include stored speech; bounded evidence retains head/tail and marks gaps;
  no mutation before review, source preserved, no duplicate Accept;
  human edits/navigation/cancel discard stale replies; missing/empty model
  output releases busy state; task preview precedes file creation, uses the
  recorded session date, and never launches Mail in the headless gate.
  Wide/narrow preview grabs and 520 px bounds are checked.
- `tst_note_agent`: native grants, typed snapshot including viewport/speech,
  read-only refusal, atomic valid/invalid batches, title/content undo together,
  replay deduplication, stale edit/undo ABA, human-edit protection, unfinished
  input refusal, preservation of recording/transcript/images through rewrite.
- `note_agent_process`: actual offscreen `qindanote` executable in isolated
  XDG state with no live D-Bus; Off/Read/Edit, CLI edit/receipt/retry/undo,
  atomic refusal and stale revision; MCP reads the live page.
- `tst_live_notes_edits`: ambiguous prefix deletion refused; `=== END` is a
  successful no-change final pass with one request and no retry/error.
- `tst_live_notes_extras`: malformed dates retain unscheduled commitments,
  unknown date anchors stay undated, long evidence marks omissions, and
  carriage-return text cannot introduce an iCalendar component.
- Existing Note live/session/markdown/input/UI/ribbon/menu gates remain.
  New follow-up controls are persistent actions; menu/ribbon reachability and
  structural fixtures at wide/narrow widths cover them.

Opt-in model review: `tst_note_followup realModelWorkflowReview` with
`QQO_NOTE_EVAL_BASE`, `_MODEL`, `_KEYREF`, optional `_BODY` and `_DIR`. It uses
synthetic design-review, planning, incident, coaching and task evidence,
records outputs/timing, and asserts generation leaves source pages unchanged.
Assess factual grounding separately from deterministic application behavior.
No microphones, real meeting recordings or personal calendar contents enter
this review. Heavy builds and model inference stay on qinda.
`QQO_NOTE_EVAL_KINDS=plan,questions` limits a follow-up review to changed kinds.

## M58 live-agent gates (D-064)

Qualified 2026-09-29 in a clean qinda build against Portage-installed QindaTK
r13: **129/129 CTest targets passed** (296.87 s). Heavy qualification moved off
the laptop at the owner's request to leave resources for video playback.

- `tst_calc_agent`: real Calc QtQuick scene, Unix socket + subprocess CLI;
  budget edit/recalculation/receipt/retry/named undo; human stale revision,
  validation rejects the whole batch, unfinished input blocks the action.
- `calc_agent_process`: actual **offscreen** `qindacalc` executable, isolated
  XDG directories and no live session bus; default Off, read-only refusal,
  Edit grant, CLI → formula result 7,632 → 9,882 → undo 7,632, MCP live read.
- `tst_assistant_edits`: delayed model response after human/local edits or
  editing revoked; no mutation; Undo change refuses a newer human edit.
- `tst_calc_tools`: built-in batch validation and bounds before any mutation.
- Existing Calc UI structural fixtures/grabs, command search/ribbon coverage,
  and panel-menu validator still pass. Grant commands are intentional shared
  session menu-only entries, not additional ribbon buttons.

The Mail, Books, Plan and Base structural fixtures were refreshed for the
current toolkit: NavigationView icon children and DataTable horizontal-scroll
containment. Calendar fixture names normalize dates; behavior tests still use
real dated targets. Reviewed Calc, Mail, Books, Plan and Base captures at wide
and 520 px widths. Books retains a pre-existing narrow dashboard header
overlap, outside the agent interface; M58 changes no QML geometry.

On-desktop follow-up: choose Off/Read/Edit from AI and the panel menu,
confirm checked state/status and immediate revoke; finish/cancel a formula
draft before retry; Edit → Undo and agent undo share native history. Desktop
launch remains explicit user consent, not part of the headless gates.

## On-desktop manual checklist (run inside a QindaQt session)

From the M0 acceptance criteria. **Verified 2026-09-14** on the live QindaQt
session (compositor queried over `org.qindaqt.Compositor1`; evidence in git
history):

- [x] Windows map as ordinary top-levels with the right identity:
      `applicationId=org.qindaqt.QindaWrite`, title `Untitled — QindaWrite`.
- [x] One process owns `org.qindaqt.QindaWrite` on the session bus; a second
      launch hands off over D-Bus and (with no paths) opens a second
      ordinary window in the primary process.
- [x] Global menu: each window exports a live `com.canonical.dbusmenu`
      endpoint at `/org/qindaqt/QindaOffice/QindaWrite/Menu/<n>` on the
      app's bus connection (identical announcement mechanism to QQTerm).
- [x] `gtk-launch org.qindaqt.QindaWrite` launches the Portage-installed
      binary (launcher path end-to-end).
- [x] Packaging: `emerge -1 gui-apps/qindaoffice` from the `qindaqt` overlay
      builds and installs all four apps + desktop files + icons + MIME
      (M10 acceptance; +charts USE flag drives QQO_WITH_CHARTS and pulls
      dev-qt/qtcharts).
- [x] M10 gates: 385-row function oracle green; chart part lands in the
      xlsx package (series range + title in chart XML); the paged view
      answers QAccessible queries (Document role, cursor/selection);
      TemplateLoader surfaces the bundled starters and a picked template
      opens detached from its path.

Still interactive-only (pointer/focus on a busy live desktop; the
app-side requirements above are the parts the suite owns):

- [ ] Meta+Shift-drag the two windows together — they group as **tabs**;
      drag a tab to a screen edge for a **split** (compositor-side; the
      desktop's own `compositor.hybrid-pointer-nested` harness covers the
      gesture).
- [ ] Eyeball the panel menu against the focused window, and the launcher
      Office category with the suite icons.
- [ ] Dirty a document, `Ctrl+W` — Save/Discard/Cancel consent; kill -9
      mid-edit and relaunch — the Recover dialog offers the journal entry.

## Milestone-specific gates

### M63 gates (QuickBooks essentials and Project XML preservation; D-071)

Automated on the desktop (`ctest --preset dev -R 'qindabooks|books_|plan'`),
`TZ=UTC`, "today" 2026-03-31, generated company files only.

- [x] `tst_qindabooks_essentials` — list import takes `customer` and
      `customers` (and `vendor`, `items`, `accounts`); a second import skips
      what is there; refused rows come back as `row 3: …`, `row 4: …` next
      to the rows that landed; an empty or missing file is refused with its
      reason; export takes the same keys.
- [x] `tst_qindabooks_dialogs` — Import Lists opens `listsDialog`; choosing
      Vendors puts the rows in the vendor list (not the customers); the
      result notice says added / skipped, turns `warning` with the refused
      row listed; Export Lists writes the chosen list. Grabs
      `qindabooks_lists_import`, `qindabooks_lists_refused` reviewed.
- [x] `tst_qindabooks_essentials` (step 2) — a PNG logo is stored, previewed
      as a `data:` URL and carried by the printable invoice; a text file is
      refused without touching the stored one; removal clears it. A
      statement for Feb–Mar shows balance forward 1,000.00, a −400.00
      payment and a 250.00 invoice, amount due 850.00, five aging buckets;
      an open period is the fiscal year to date; a reversed period and no
      customer are refused; the PDF is written; e-mail hands QindaMail
      `--attach <pdf> mailto:ap@acme.test?subject=Statement from …` with
      $850.00 in the body. A monthly schedule from Jan 31 makes 3 invoices;
      a name/amount edit keeps start and count; a weekly schedule from
      Mar 15 is refused (last invoice Mar 31); from Apr 6 it re-bases (3 due
      by Apr 20); paused → 0 due, "Paused"; resumed → 6 invoices; deleted →
      the 6 invoices stay, the audit has `delete`. A 1,000.00 year splits
      into twelve months that add up exactly; 12.345 is refused; saved
      budgets show in Budget vs. Actual (budget 1,000.00, actual 400.00);
      one bad month saves nothing. A 10 % rate on 100.00 owes 10.00 by
      Mar 31 (0 by Feb 28); paying from an expense account is refused;
      paying from Checking leaves 0 owed.
- [x] `tst_qindabooks_dialogs` (step 2) — each command opens its dialog
      (`logoDialog`, `statementDialog`, `recurringDialog` +
      `recurringEditor`, `budgetDialog`, `salesTaxPaymentDialog`); the logo
      preview loads; the statement shows one line and 1,200.00 due, mails
      through the launcher and its Preview makes the statement the print
      source with Print Preview enabled; a schedule made in the editor lists
      with its customer and next date, pauses, resumes and is deleted; a
      24,000 year spreads to 2,000.00 a month and saves; Pay Sales Tax offers
      the 40.00 owed and records it. Grabs `qindabooks_logo`,
      `qindabooks_statement`, `qindabooks_recurring_editor`,
      `qindabooks_recurring_list`, `qindabooks_budget`,
      `qindabooks_sales_tax` reviewed.
- [x] `tst_books_io_iif` (step 3, 7 cases) — the reader resolves quoted
      fields with doubled quotes, M/D/YY and MM/DD/YYYY, a redefined
      header, Windows-1252 bytes, a block without ENDTRNS and an SPL
      outside one (`line 7:`); a QuickBooks-shaped studio file (16 accounts
      incl. a sub-account, a hidden and a non-posting one, 2 customers, 2
      vendors, 4 items, a class, 13 transactions) previews without changing
      the company and is refused when committed strictly; imported with the
      refusals left out: 9 accounts added, 6 matched (A/R, A/P, Undeposited
      Funds, Sales Tax Payable, Interest Income, Opening Bal Equity), the
      non-posting and the discount item left out, the hidden account
      inactive, the tax agency on the invoice's tax split not made a
      customer; 11 posted — 10 documents (2 invoices, payment, deposit, bill,
      bill payment, check, card charge, credit memo, transfer) and 1 journal
      entry — and 2 refused with their lines (unknown "Petty Cash"; "do not
      balance"); every balance to the cent (checking 7,418.85, A/R 300.00,
      Undeposited Funds 0, A/P 0, Sales Tax Payable 81.25 …), invoice 1001
      paid, 1002 open 300.00 after the credit memo, the deposit emptied the
      undeposited queue, the bill paid; mapping "Petty Cash" posts the 12th;
      a second import posts 0 (11 already in the books); the Northwind
      fixture's own IIF export imports into a fresh company with every
      account balance and the account, customer and vendor list CSVs
      identical (a hidden account included), items intact, PART written,
      and re-imports as nothing; QuickBooks Online CSVs: accounts with
      "Accounts receivable (A/R)" skipped as the company's own, "Other
      Current Liabilities" and "Long Term Liabilities" typed, customers with
      "Full Name" as contact and "Phone Numbers", Street/City/State/ZIP
      joined, "Inventory" products as non-inventory.
- [x] `tst_books_io_export` — the ACCNT rows end with EXTRA and HIDDEN.
- [x] `tst_qindabooks_essentials` / `tst_qindabooks_dialogs` (step 3) — the
      preview posts nothing and lists the unmapped account; a strict import
      with a refusal imports nothing; mapped, the import makes a backup,
      posts 2 ($520.00 of debits), notifies once, and a second run posts 0.
      The dialog previews, shows the refusal and the account to map, maps it
      (0 refused, 2 to post), imports and says so. Grabs
      `qindabooks_iif_preview`, `qindabooks_iif_done` reviewed.
- [x] `tst_books_io_reports` (step 4) — the March P&L as `.xlsx`, read back
      with QXlsx: company and title in A1/A2, a bold header row, "Net
      Income" a bold double equal to the report's total under
      `"$"#,##0.00;-"$"#,##0.00`, "4000 Project Fees" a double and indented;
      Sales by Customer's share is the number 1.0 under `0.0%`; the General
      Ledger's dates are dates under `yyyy-mm-dd`.
- [x] `tst_qindabooks_essentials` (step 4) — every report of the catalog
      but the reconciliation report exports through
      `exportReport(…, "xlsx")` as a zip package.
- [x] `tst_plan_mspdi_keep` (step 5, new, 4 cases) — a Project-shaped file
      (`tests/plan_engine/data/project_keep_sample.xml`) reads priority 900
      / 100 and the deadline into the model, flags the task that finishes
      after its deadline, edits both and refuses priority 1200; the
      recurring exception, the work weeks and baseline 4 are reported; the
      export keeps Subject, Author, the project GUID, `HonorConstraints 0`,
      `CurrencySymbol €`, the `ExtendedAttributes` definition and an
      unknown project element in place, the calendar's GUID, its recurring
      exception inside `<Exceptions>` and its work weeks, the summary task's
      custom field, the task's Type 1 and FixedCostAccrual 1 (not our
      constants), contact, an unknown element (with its attribute) right
      after Contact, hyperlinks (escaped), custom fields, baselines 0 and 4,
      timephased data, the resource's notes, custom field and rate tables,
      the assignment's contour and delay — every Task child in Project's
      order; re-reading the export writes it byte for byte; a `.qplan` save
      and load writes it byte for byte; after a duration change the task's
      timephased data and remaining duration are dropped (and the
      assignment's), the user's own fields and the rate tables stay, and a
      changed rate drops the rate tables.
- [x] `tst_plan_mspdi` — unchanged expectations except two counts and one
      check the feature changes by design: the export holds 4 baselines and
      4 exceptions (baseline 5 and the recurring exception are kept), and
      re-reading our export may only repeat the original import's warnings.
- [x] `tst_plan_output` (step 5) — the task sheet as `.xlsx`, read back with
      QXlsx: a bold "Task Name" header; "Shortlist venues" indented, 3.0
      days, its start a date-time serial under `yyyy-mm-dd hh:mm`, 60 % as
      0.6 under `0%`, its cost a number under `"$"#,##0.00…`, 24 hours of
      work, priority 800, "Ana Ruiz".
- [x] `tst_qindaplan_service` (step 5) — Task Information saves priority 950
      and a deadline, refuses "high", clears the deadline with NA; Export
      Task Sheet as Excel writes a package; the sample opened through the
      host and exported as XML still carries its custom-field definition and
      values, priority, deadline, rate tables and recurring exception.
      `tst_qindaplan_menu_export` and `tst_qindaplan_ui` (ribbon gate with
      `planExportXlsxAction`) green; Plan fixtures unchanged.
- [x] `tst_qindabooks_ui`, `tst_qindabooks_host`, `tst_qindabooks_menu_export`
      — the new commands exist with icons, sit once in the menu tree and on
      the ribbon; the ribbon gate holds at 1320 and 520 px (grabs
      `qindabooks_ribbon_1320/520` reviewed); fixtures
      `qindabooks_1280/520` regenerated for the new ribbon slots and view
      buttons (`salesRecurring`, `salesStatements`, `accountsBudgets`,
      `accountsPayTax`, and step 4's `reportExcel`); grab
      `qindabooks_reports_1320` reviewed (the option bar wraps PDF/CSV/Excel
      onto a second line at 1320 px, as the strips do).
- [x] `tst_books_io_checks` (step 6, new, 6 cases) — the amount in words
      for 10 amounts (1,234.56 → "One thousand two hundred thirty-four and
      56/100", 0.05 → "Zero and 05/100", 21.00 hyphenated, empty thousands
      skipped, JPY without a fraction, KWD over 1000, a negative amount
      empty) and for the largest qint64 ("Ninety-two quadrillion …
      and 07/100"); a written check gives number 1042, 03/09/2026, the
      payee and its address block, `**449.99`, "Four hundred forty-nine and
      99/100", the printed note as memo (never the internal memo), "1000
      Checking", the two account lines; a bill payment with reference 1043
      prints as check 1043 for `**1,400.00` with both bills (date, number,
      bill amount, paid 1,200.00 and 200.00); a debit-card expense, an
      invoice, a missing id, a credit-card bill payment and a voided check
      are refused with their reason and no PDF is written; two checks make a
      two-page PDF whose text, read back with Poppler by band, has the date
      top right, payee and figures on one line, the words starred to the
      line's end, the address block, the memo line, and both stubs complete;
      a 30-line check's stubs say how many lines they left out. Gallery
      grab `books-check-voucher.png` reviewed.
- [x] `tst_qindabooks_host` (step 6) — Print Check is disabled with no
      selection and for a debit-card expense, enabled for a check; it emits
      the print request with the `"check"` source, a print job is created
      and Export PDF writes it; `exportDocumentPdf` (the editor's Save
      PDF…) writes the check and still refuses the expense; a `"document"`
      source naming the check becomes `"check"`, a `"check"` source naming the expense is cleared;
      voiding the selected check disables the command. Fixtures
      `qindabooks_1280/520` regenerated for the one new ribbon slot
      (`ribbonItem_booksPrintCheckAction`); grabs
      `qindabooks_ribbon_1320/520` reviewed (the Document group holds four
      commands at 520 px).
- [x] Full suite on qinda at `594590b` (`cmake --build --preset dev`, then
      `ctest --preset dev`): **142 of 142 passed** — the 137 before M63
      plus `tst_qindabooks_essentials`, `tst_qindabooks_dialogs`,
      `tst_books_io_iif`, `tst_books_io_checks` and `tst_plan_mspdi_keep`.
      QtTest totals of the M63 binaries (functions incl. init/cleanup and
      data rows): `tst_books_io_checks` 17, `tst_books_io_iif` 9,
      `tst_books_io_reports` 11, `tst_books_io_export` 7,
      `tst_qindabooks_essentials` 10, `tst_qindabooks_dialogs` 9,
      `tst_qindabooks_host` 11, `tst_qindabooks_ui` 7,
      `tst_qindabooks_menu_export` 4, `tst_plan_mspdi_keep` 6,
      `tst_plan_mspdi` 6, `tst_plan_output` 7, `tst_qindaplan_service` 11,
      `tst_qindaplan_ui` 8, `tst_qindaplan_menu_export` 4 — all passed.

### M57 gates (organised, typeset notes; D-063)

- `tst_live_notes_edits`:
  - the edit script parses (ops, targets, bodies, `=== END`, whole notes
    are not a script);
  - APPEND lands in a section opened long ago, above its subsections;
  - NEW UNDER re-levels to `###`, NEW (at end) stays above Timeline,
    Definitions and Action items;
  - REPLACE keeps the original level (or heading), DELETE takes
    subsections along, MOVE re-levels and refuses a move into itself;
  - SUMMARY, TITLE, TIMELINE and ACTIONS; notes grow from empty;
  - missing targets keep their content;
  - every layout has its own brief, and the ids are pinned;
  - the engine files an edit script, treats `=== END` as nothing new and
    refuses prose;
  - the final pass is one edit script (no FULL TRANSCRIPT), and `=== END`
    finishes it.
- `tst_live_notes`: the system prompt carries the layout, the style guide
  and the edit protocol; the plain style has no callouts; the M52
  final-revision case runs with `editMode = false`.
- `tst_note_markdown`: the markup is gone from the plain text and the
  typesetting is in the HTML; `C++` and `code` stay literal; a callout inside
  a bullet still becomes a box; the sample paints its callout tint. The grab
  `note_typeset.png` was checked: highlights in six colours, underline,
  sub/superscript, stand-out red, the AI aside, grey timestamps, labelled
  boxes, a ruled table.
- `tst_note_live` setup grab: Organize, Also keep / Your sections, Look and
  the timeline switch.
- Manual: `realPipelineEndToEnd` on the laptop's Qwen3.5-4B (run with
  `QTEST_FUNCTION_TIMEOUT=900000`; the harness now logs every status change
  and engine failure).

### Offline notes gates (D-062)

- `tst_ai_providers`: a bare key path is stored as `{file:…}`; a literal
  key and `{env:…}` are left alone.
- Manual, run 2026-09-27: `NoteLiveTest::realPipelineEndToEnd` with
  `QQO_LIVE_E2E_BASE=http://127.0.0.1:8080/v1 QQO_LIVE_E2E_MODEL=qwen3.5-4b`
  against the installed `sci-ml/qinda-notes-llm` passed (252 s, three
  revisions; numbers in live-notes.md). The service loaded from cold on the
  first request and unloaded when its proxy stopped.

### M56 gates (flag, prep, recording, review, hand-offs; D-061)

- `tst_live_notes_extras`:
  - key terms and action items parse;
  - action items become .ics (a VTODO each, a VEVENT only for dated
    items, escaping);
  - split by topic keeps the index's summary, definitions and actions;
  - Anki cards come only from Front/Back tables;
  - unclear spots with and without times;
  - section times;
  - the prompt's ‼, named speakers, `[[ai:]]`, reference material and
    earlier sessions;
  - the engine: flags on the heard and the next line, two screens per
    update, usage, a speaker-names restyle, the glossary deduplicated.
- `tst_office_speech::wavRecorderWritesAPlayableFile`: the header is
  patched mid-session, and the data is exact.
- `tst_qindamail_calendar::icsTasksBecomeTasks`: VTODO parsing (folded
  lines, escapes, priority), then tasks with due dates in the store.
- `tst_note_session`:
  - **duringASession:** F9 flags; speaker names reach the engine; usage;
    silence auto-pause and resume; the recording kept and exposed as
    `audioUrl`; grab `qindanote_live_session_m56`.
  - **reviewAndPageCommands:** sections and unclear spots from the notes;
    a Ctrl+click target on `[01:30]`; the transcript in the assistant's
    snapshot; fix an unclear spot; export Markdown; Anki refused without
    cards; a study page from the model placed after the notes; Anki
    export; split by topic (+2 pages, index); grab
    `qindanote_session_panel`.
  - **prepMaterialAndSeriesMemory:** .md and PDF prep read; earlier
    section notes; both reach the engine context; terms learned from them.
  - **movingAPageKeepsTheViewOnIt:** the Move Page regression.
- The QindaNote fixtures, ribbon squeeze and panel export include the new
  commands.

### M55 gates (speaker labels, D-060)

- `tst_office_speech::speakersStitchAcrossWindows`:
  - two windows against a local server; the second window's diarization
    numbers the speakers the other way round, and the one speaker who was
    silent in the overlap is still recognised through the voice samples;
  - the output is "Speaker 1: Hello there", "Speaker 2: hi",
    "Speaker 1: how are you", with nothing repeated;
  - requests carry `diarize=true`, `pcm_s16le_16` and the key header.
- `tst_live_notes`:
  - `[mm:ss] Speaker 2: …` lines;
  - the speaker rule in the prompt;
  - Laya's `noul` field parsed.

### M54 gates (live-notes briefing, D-059)

- `tst_live_notes::briefingShapesThePrompt`:
  - the default prompt keeps timestamps and action items and has no
    briefing;
  - a meeting briefing with focus, skip and custom text and the switches
    off/on changes the rules and appends SESSION BRIEFING;
  - the map round-trips;
  - six scenarios;
  - the engine sends the briefing.
- `tst_note_live::sessionWritesAndRevisesOneBlock`: Start opens the setup
  dialog (`liveNotesSetupRequested`, state still idle; grab
  `qindanote_live_setup`); the setup persists; with the prompt off, Start
  starts.
- The QindaNote fixtures gain only `IconButton#liveSetup`.

### M53 gates (the assistant edits documents; live-notes detail, D-058)

- `tst_assistant_edits`:
  - the ```qinda-edits parse (object, bare array, plain answer, cut-off
    block);
  - edit mode against a scripted SSE model (the snapshot goes out, the
    block is applied, the conversation shows the change and not the JSON,
    Undo);
  - with edit mode off, nothing is applied.
- `tst_write_tools`: `replace_text`, `replace_section` (the next section
  survives, and the heading stays a heading), `insert` at the end,
  refusals, one undo step, `replace_document`.
- `tst_calc_tools`: the snapshot shows formulas → results; `set_range`,
  `set_cells` (live `=SUM`), `clear`, a bad address refused, one undo step.
- `tst_show_tools`: `set_text`, `add_slide` with bullets and notes,
  `set_notes`, a missing box refused, one `SlideListCommand`, undo.
- `tst_note_live::assistantEditsThePage`: the snapshot numbering, rewrite
  plus insert plus rename, blocks reflowed without overlap, one undo step,
  `replace_page`.
- `tst_note_ui`: a reply inserted with no editor open is a rendered
  markdown block. The grab `qindanote_assistant` shows the **Edit the
  document** switch.
- `tst_live_notes::detailLevelsShapeThePromptAndRestyle`: level briefs in
  the system prompt; moving the slider rewrites with no new speech, and a
  large shrink is accepted.
- Fixtures for Note, Write, Calc and Show re-recorded. The diff is only
  `assistantEditMode`, `assistantUndoEdit`, `liveDetail` and
  `liveDetailName`.

### M52 gates (live notes, D-057)

Automated:

- `tst_office_speech` (15 cases). It covers:
  - the mixer (sum, saturation, a lagging stream treated as silence);
  - the segmenter (onset with pre-roll, 700 ms tail, clicks dropped, 20 s
    monologue cuts);
  - the WAV header;
  - capture through a fake `pw-record` (arguments for the sink monitor,
    100 ms chunks, pause keeps the meter);
  - a missing recorder;
  - ElevenLabs realtime against a local WebSocket server (query, key
    header, buffering before `session_started`, partial/committed, the
    final commit), a fatal `auth_error`, and no key at all;
  - the Whisper helper protocol with a fake helper (glossary prompt on
    every clip);
  - the real helper refusing a missing model;
  - HTTP transcription against a local server.
- `tst_live_notes` (13 cases) and `tst_ai_providers` (4). They cover:
  - the prompt (notes, context, ★/⤷ marks, glossary);
  - reply clean-up;
  - the TAIL splice (untouched middle, kept action items);
  - the plausibility guard;
  - the engine's cadence, never two runs at once, user edits winning,
    retry while the editor holds the block, and the final full revision;
  - the gate against a local System One server (filler not counted, a
    new important topic marked and triggering), and the gate failing open;
  - System One parsing;
  - key references, env and OpenCode's `auth.json`;
  - presets from the resource;
  - catalog round trip (a literal key refused);
  - `opencode.json(c)` import with a relative `{file:}`.
- `tst_note_live` (4 + 1 manual). It covers:
  - markdown blocks laid out and printed, with the highlight painted;
  - a whole session in the real window against a local SSE model
    (page titled from the topic, placeholder not treated as notes, a
    revision as an undo step, no write under the open editor, Stop with
    the final revision and the transcript stored on the block, action
    enablement);
  - the panel from AI ▸ Live Notes, with its grabs.
- `tst_note_ui` fixtures and the ribbon squeeze gate include the Live tab
  and the hidden panel. `tst_note_menu_export` validates the new AI-menu
  ids.

Manual, real pipeline:

- `QQO_LIVE_E2E_PCM=<16 kHz mono s16le> QQO_LIVE_E2E_BASE=… QQO_LIVE_E2E_MODEL=…
  QQO_LIVE_E2E_KEYREF='{file:…}' QQO_LIVE_E2E_BODY='{…}' tst_note_live
  realPipelineEndToEnd`.
- Run on 2026-09-27 with four minutes of podcast speech, local
  `distil-small.en` and Ornith on qinda. It passed (see live-notes.md).

On the desktop (not run yet, asks first):

- [ ] Microphone lecture.
- [ ] Desktop audio from a browser video.
- [ ] Both during a call.
- [ ] ElevenLabs with Gabbee's key found automatically.
- [ ] Pause/Resume.
- [ ] Edit the notes mid-session (your edit is kept).
- [ ] Stop (final revision).
- [ ] Export PDF shows the formatted notes.
- [ ] AI ▸ AI Providers… import from OpenCode.

### M40 gates (the ribbon, D-041)

- **Ribbon gate** (`tests/support/ribbon_gate.h`, one test per app,
  `ribbonReachesEveryCommandAndSqueezes`) at a desktop width and at 520 px:
  every command of the menu tree is reachable from the ribbon — a button
  or a dropdown entry — unless the app lists it as menu-only with a reason
  (Quit, Find Next/Previous, About, Restore Previous, and the colour
  dialogs the colour buttons open); every icon name the layout asks for
  exists in QindaTK; every item of every tab, contextual tabs forced on,
  is visible and inside the ribbon. This replaces the strips' squeeze
  tests: the ribbon squeezes (large → small → icon) and then wraps, it
  never scrolls or hides a command (QindaTK D-297).
- **Live binding**: Write's Bold button toggles the entry and shows its
  state; Calc's Undo follows the entry's enabled state; Note's tools stay
  one radio group; Write's Table and Show's Shape tabs appear only in
  their context.
- **Shell**: `tst_office_window` builds a ribbon from ids (an unknown id is
  skipped, never drawn), flips View ▸ Compact Ribbon, and opens Help ▸
  Search Commands… (Alt+Q) — the palette lists the menu tree's commands by
  section and activates by id.
- **Pixels reviewed**: all four apps at 1280 and 520, dark preset
  (validation gallery, `M40-*`).
- `--list-commands` on every app prints id, label, shortcut, icon and menu
  path — the inventory the ribbon layouts were written against.

Added per milestone in the milestone commit (formula tables, xlsx
round-trips, fodp→pptx conversion, provider smoke tests, pen-input checks,
presenter-view-on-second-screen, reduced-motion, packaging).

### M22 pen gates (automated: `tst_stroke_edit`, `tst_note_input`, `tst_note_ui`)

- office_ink: a pen outline follows its pressure width with round caps;
  the highlighter outline ignores pressure (constant width, flat caps);
  the stroke-mode eraser removes whole strokes and reports misses (no
  phantom undo step); the partial eraser splits a stroke at the gap and
  drops fragments under two samples; a pen stroke crossing a highlighter
  reads as the pen colour (highlighter under ink, one translucent fill).
- App: the eraser acts DURING the drag (stroke gone before release); one
  undo step per eraser gesture and none for an empty swipe; a held barrel
  MiddleButton borrows the eraser and reverts on release; a 20-sample
  stroke repaints only segment-sized rects (never the whole page); the
  page stays white under a dark palette (D-004 pin); P/H/E switch tools
  and type into a focused find field instead of switching; three quick
  edits coalesce into one debounced page write; the strip is icon-only
  for the shipped glyphs and keeps every command visible squeezed to a
  third of the window width.

Manual (QindaQt desktop, tablet):

- [ ] Pressure looks right (thin at a light touch, swelling under
      pressure); Settings ▸ Preferences ▸ Notes ▸ "Pen pressure changes
      width" off → uniform width; smoothing Off/Light/Strong felt.
- [ ] No lag at 5 000 strokes; with Draw with Finger OFF a palm resting
      on the screen does nothing and one finger scrolls the page.
- [ ] Touchscreen (D-053): one finger inks with Pen/Highlighter/Eraser;
      two fingers pinch-zoom about the fingers and pan, leaving no dot
      where the first finger landed; with the Text tool a finger tap opens
      or places a block and a drag scrolls; a touchpad pinch zooms.
- [ ] Recovered documents (D-055): kill an app with unsaved edits in two
      windows, relaunch — ONE dialog lists both with a line of text; untick
      one, Recover Selected opens the other; relaunch again — the unticked
      one is not offered. Closing the dialog discards everything listed.
- [ ] Global menu (D-054), in every Office app: click a panel menu — it
      stays open, items are clickable, and it still opens after copying
      text in another app and after editing a note block.
- [ ] Typing in a text or checklist block on a DARK theme: dark ink on the
      page with a thin outline, text does not jump when the block opens
      or closes, selection readable.
- [ ] Highlighter sits under pen ink; the eraser ring cursor tracks
      radius × zoom; the stylus eraser end and the barrel button erase
      while held and revert on release.
- [ ] Dark theme: the page is white, ink readable; strip icons legible at
      16 px on light and dark toolbars.
- [ ] Single-letter shortcuts switch tools; with the navigator TREE
      focused a letter switches tools instead of keyboard-searching (the
      tree does not take ShortcutOverride — accepted, D-026); Esc returns
      to Pen; Ctrl+wheel and the zoom combo agree.

### M11 UI completeness (automated: `tst_write_ui`)

- Main + formatting toolbars exist with persistent named actions
  (objectNames from the shell's `ActionId` table); Format/Insert menus
  exist after `finalizeAppMenuHooks()` and share the same action objects.
- Bold/italic/underline/strikethrough toggle and re-read from the cursor;
  font family/size combos apply and sync; alignment, bullet/numbered
  lists (toggle + convert), indent clamp at 0.
- Insert Table (2x3 bordered) and Insert Columns (1xN borderless) land as
  real `QTextTable`s; find wraps and reports, replace-all counts.
- Edit actions follow control availability; a pristine document has zero
  undo steps (Qt `setPageSize` undo quirk cleared); zoom actions move the
  view; the paper palette stays white-on-dark under any theme.

### M15 additions (automated: `tst_qindanote`)

- Pen width (1-24 px) and pen color flow into newly inked strokes
  (mouse-fallback inking verified stroke width/color).
- Page background choice (blank/ruled/grid/dotted) edits the envelope and
  reports pageEdited.
- Zoom (50-200%) scales the page rect and repositions text blocks while
  inked coordinates stay in page space (identical samples at 1x and 2x).

### M14 additions (automated: `tst_write_ui`, `tst_write_view`)

- Table menu + toolbar section: row/column insert+delete, merge, split -
  all persistent named actions enabled only inside a table; deletion keeps
  the cursor in the table.
- Page Setup dialog edits header/footer ({page}/{pages} tokens); the
  exported PDF carries header and per-page footer text on every page
  (poppler-verified); HTML save/reload round-trips page size, margin and
  header/footer metas.

### M13 additions (automated: `tst_show_ui`, `tst_show_model`)

- Shapes toolbar + Insert/Shape menus share persistent named actions
  (objectNames from the shell ActionId table).
- Bold/italic/fill/stroke mutate through the canvas with undoable
  before/after payloads; two undos walk a fill then a bold back.
- The shape clipboard round-trips through the JSON codec with a fresh id
  and a +12 pt offset; cut removes the source; paste selects the new shape.
- Insert actions land shapes through the normal undo path; editing actions
  follow selection and clipboard shape; Select All selects every shape.

### M12 additions (automated: `tst_calc_ui`)

- Undo/redo restores values AND re-evaluates formulas (dependency index
  rebuilt by the snapshot restore); sheet insert is undoable and the tab
  bar follows `structureChanged`.
- Rich clipboard round-trips formulas exactly; plain TSV pastes numbers,
  booleans, text and formulas; cut clears in one undo step.
- Bold/italic/underline, alignment, font family/size, text/fill colors and
  the number-format combo write `CellStyle`s through one-undo-step
  `StylePatch`es; the UI state re-reads the cursor cell.
- The find bar walks matches row-major with wrap; F3/Shift+F3 drive it.

### M11 on-desktop checklist (interactive)

- [ ] Under the QindaQt dark theme a .docx opens as a white page with dark
      text (no "white boxes on dark"), matching PDF export.
- [ ] Toolbar icons resolve from the Breeze theme (offscreen runs show the
      text fallback by design); `Ctrl+F` opens the find bar, Esc closes it.

### M1 additions (automated)

- `tst_single_instance` — two helper processes over a private session bus:
  primary acquires, secondary hands off and exits 0. Skips with an explicit
  message when the environment cannot spawn a bus or denies Qt name
  ownership (some sandboxed/elevated contexts).
- `tst_libre_office_converter` — live txt→pdf conversion against the
  installed soffice; skipped when the conversion pipeline is unavailable.
  Every soffice child — the converter's and the ones tests start through
  `tests/support/soffice_env.h` — runs with `SAL_USE_VCLPLUGIN=svp` and
  without `QT_QPA_PLATFORM`: a QindaQt session exports
  `SAL_USE_VCLPLUGIN=qt6`, and with the tests' offscreen platform soffice
  aborted before converting, which turned the live cases into failures or
  silent skips. Layout probes compare against laid-out line heights, never
  a literal, because the session's platform theme (`qindaqt`) picks the
  default font even under `offscreen`.
- `tst_office_application` — canonical-path uniqueness (duplicate open
  activates), pristine-untitled reuse, recents bookkeeping, 32-window bound.
- `tst_recovery_journal` / `tst_restore_inventory` / `tst_recent_files` /
  `tst_atomic_file_store` / `tst_plain_text_document` — store bounds,
  debounce, round trips, optimistic-revision inputs.

### M1 manual checklist (live desktop)

- [ ] Open a file from the file manager: it lands in a new ordinary window
      (or a pristine untitled one); opening the same file twice activates
      the existing window.
- [ ] Edit, close: Save/Discard/Cancel consent appears; Discard clears the
      recovery entry; Save clears it after writing.
- [ ] Kill the app mid-edit; relaunch: a Recover dialog offers the unsaved
      document; Restore brings it back dirty, Discard forgets it.
- [ ] Settings ▸ Restore Previous Documents (off by default); with it on,
      the previous session's paths reopen at launch.
- [ ] Second launch (`qindawrite file`) from a terminal while one instance
      runs: paths hand off over D-Bus; `--new-instance` bypasses.

### M16 shell-reachability audit

Automated by `tst_document_window`, `tst_calc_window`, `tst_recovery_journal`
and `tst_office_ai`.

Rationale: the milestone suites drive documents directly, so a command can
be fully wired and still be unreachable from the UI. This pass dumps every
window-scoped `QAction` of the four real windows under
`QT_QPA_PLATFORM=offscreen` (enabled state, objectName, shortcut) and
asserts the reachability contract. See D-011.

- A document installed through `setDocumentHost()` enables Save and Save
  As; before this pass they were disabled in all four apps, so File ▸ Save,
  the toolbar Save button and Ctrl+S did nothing.
- Close Window is `Ctrl+W` and Quit is `Ctrl+Q` (AGENTS.md rule 1), asserted
  as literal chords — `QKeySequence::Close`/`::Quit` resolve to Ctrl+F4 and
  the "Exit" media key under the keyboard scheme the qindaqt platform theme
  yields.
- Every menu action in QindaWrite/QindaCalc/QindaShow/QindaNote carries a
  non-empty objectName (rule 5); the QindaNote tool strip has the toolbar
  objectName `QMainWindow::saveState()` requires.
- QindaWrite has exactly one `&AI` menu (the inline actions live in the
  shell's AI menu next to Assistant…, as in QindaShow).
- An empty `recoverySnapshot()` is not written as a journal entry, so the
  stub engines (QindaCalc, QindaNote) cannot produce a Recover prompt whose
  restore always fails.
- `ModelCatalog::configFor()` resolves every id `modelIds()` hands out, and
  a corrupt `providers.json` is kept and reported instead of being replaced
  by the seed.
- QindaCalc: undo back to the saved position clears the dirty mark (dirty
  follows the `QUndoStack` clean index); chart insert is one undo command.

### M17 capability completeness

Automated by `tst_qindanote`, `tst_calc_window`, `tst_calc_io` and
`tst_office_ai`. Rationale in D-012: a declared capability that is a stub is
a defect, and the stubs were hiding a data-loss bug. Live-LibreOffice cases
skip with a message where `soffice` is unavailable.

- QindaNote: saving a notebook after opening ONE page leaves every other
  page's file intact (`save()` writes only loaded pages; Save As
  materialises unloaded ones from the source first).
- QindaNote: Edit ▸ Redo replays an undone stroke, follows the view's real
  history depth, and a fresh edit ends the redo branch.
- QindaNote: `recoverySnapshot()` round-trips sections, page titles, ink and
  page background; an untitled notebook recovers entirely from memory, a
  saved one re-attaches to its directory so unopened pages stay on disk.
- QindaCalc: `recoverySnapshot()` round-trips formulas (re-parsed and
  recomputed), typed values (the text `007` stays text), styles set on EMPTY
  cells, extra sheets, named ranges and chart specs.
- QindaCalc: an `.ods` saves and reopens through the LibreOffice bridge with
  its formula recomputed; `mimeTypes()` matches the `.desktop` MimeType list.
- calc_io: a named range survives the xlsx round trip, and `=SUM(NAME)` still
  evaluates after import (names are read before the cells, so the dependency
  index sees them).
- office_ai: `configFor()` resolves every id `modelIds()` hands out; a
  corrupt `providers.json` is moved to `.corrupt` and the picker still works.

### M44 gates (QindaMail core — `apps/qindamail` C++)

- **UI** (`tst_qindamail_ui`): the real `MailWindow.qml` over a seeded store —
  the inbox lists the messages with day headings, a click fills the
  reading pane (subject, attachment chip), the ribbon gate holds in the
  Mail, Calendar and Tasks workspaces at 1280 and 520 px (a command counts
  as reachable when some workspace's ribbon reaches it; Save/Save As and
  the edit commands are menu-only in a document-less window), the
  composer window and the account dialog open, structural fixtures
  `qindamail_{1280,520}.txt`. Grabs reviewed: `M44-iter4-*` in the
  validation gallery (inbox, calendar week with the now line, tasks,
  composer, account kinds, 520 px).
  `accountSignInUsesCredentialDialog` clicks the named `needsSignIn`
  account button with settled geometry and observed `clicked` delivery at
  1280/520 px. IMAP and saved Gmail app-password accounts open their usable
  password field; Google accounts display the actual missing-client error.
  A configured synthetic OAuth client reaches an intercepted browser adapter,
  then a private-flow cancellation proves that the correlated rejection stays
  visible in the same dialog. A second real window shares the service but
  receives neither the account-dialog intent nor its problem. Fixtures use
  private XDG data, an in-memory keyring and a private D-Bus session; no browser
  or external authentication/network operation runs.

Automated by `tst_qindamail_drafts`, `tst_qindamail_service`,
`tst_qindamail_accounts`, `tst_qindamail_calendar`, `tst_qindamail_host` and
`tst_qindamail_flow` (`ctest -R qindamail`). Each test runs a `MailService`
over its own temp tree with an in-memory keyring, `autoSync` off and the
network watcher off; the only servers are the tests' own POP3/SMTP fakes on
127.0.0.1. Rationale in D-042; reference in [mail.md](mail.md#qindamail-app).

- [x] Drafts: address lists, Re:/Fwd: rules (Re, RE, Re[2], AW / Fwd, FW),
      quoting, reply / reply-all (own addresses in any case and duplicates
      removed; a reply to one's own message goes to its recipients), forward
      header block, In-Reply-To and References.
- [x] `mailto:` — every example of RFC 6068 §6 (percent-encoded `%`, `?`,
      `&`, quotes, UTF-8 subject, IDN domain to ACE, `to=` merged with the
      path, `+` kept), other schemes refused.
- [x] Message list rules: dateText (time today, weekday this week, date
      before), day groups, multi-word search, unread / flagged / attachment
      filters.
- [x] The local account and its folders exist with no account; folder
      counts (per folder, all inboxes, `unreadTotal`); collapse/expand;
      account headers not selectable; conversations, filters and search
      through the service; the conversations setting survives a restart.
- [x] Selecting an unread message marks it read after `markReadDelayMs`,
      and not when the selection moves on first; `selectedMessage` carries
      `accountId`, `to`, `bodyTruncated`, `attachments`.
- [x] Local actions (flag, archive, move, delete → Trash → gone) answer
      `operationFinished` on a later turn; cross-account moves and unknown
      ids are refused.
- [x] Reply / forward / mailto drafts from the service; a forward owns
      copies of the attachment bytes (discarding it never deletes the
      message's); drafts persist; sending from the local account is refused
      and keeps the draft.
- [x] An opened `.eml` lands once in the local Inbox with its attachment
      bytes; opening an attachment writes only store bytes to a private file.
- [x] Accounts: add writes the password to the keyring only (never to
      `accounts.json`); remove wipes every secret, purges the account's
      cached messages/events and keeps its drafts; secret-named fields are
      refused.
- [x] Protocol objects per kind (gmail, imap preset, microsoft365, pop3,
      exchange): host, port, security, user, mechanism, the secret key SMTP
      shares, the calendar provider type and URL, the OAuth resource, the
      sent-mail rule; one IMAP provider per mailbox.
- [x] Credentials before every request: a stored password reaches the
      provider (a closed loopback port answers `offline`); no stored password
      is `needsSignIn` without a connection; the auth-rejection
      classification (IMAP, POP3, SMTP 535, HTTP 401, not TLS policy).
- [x] Calendar: create / update / delete (series, occurrence, following),
      the projection of a weekly rule with an exception clipped to the
      requested days, grid `shift-local` / `resize-local` in occurrence,
      series and following scopes (a second split included), refusals,
      visibility (persisted), recurrence preview, `.ics` export → import
      round trip (rule and EXDATE kept, re-import idempotent).
- [x] Tasks: add, move between columns, update, delete, persisted.
- [x] Host: 30 command ids with shipped Lucide icons, unique shortcuts, the
      `mailView` and `calendarMode` radio groups, enabled state from view and
      selection, the panel export accepted by the desktop's validator with
      every exported id resolving and every command in a menu, `mailto:` and
      `.ics` paths handed to the window, `MailWindow.qml` instantiates.
- [x] Flow against local servers: POP3 sync with the keyring password, a
      reply sent over SMTP and filed in the local Sent folder (draft gone),
      a wrong password retried once and then `needsSignIn` (cleared by a new
      password), an unreachable SMTP server leaves the message in the
      Outbox; no password in results, `store.json` or `accounts.json`.
- [ ] Lead lane: QindaMail fixtures and grabs (1280/520), ribbon squeeze,
      the launcher card.
- [ ] On the desktop: a real IMAP account (and Gmail/Microsoft 365 with a
      registered client ID, WP-089/WP-090), `qindamail mailto:…` from a
      browser, a `webcal:` link, the panel menu.

### M43 gates (mail accounts, autoconfig, OAuth, secrets — `office_mail`)

Automated by `tst_mail_accounts`, `tst_mail_autoconfig` and `tst_mail_oauth`
(`ctest -R mail`). Offline: every server is a `FakeHttpServer` on
127.0.0.1 and the keyring is in memory (`QQO_MAIL_SECRET_BACKEND=memory` in
the ctest environment). Rationale in D-043; reference in `docs/mail.md`.

Native imported Password compatibility (D-067) additionally runs
`tst_imported_password_codec` and `tst_imported_password_backend`. The latter
uses its own private D-Bus provider, including actual QtKeychain/libsecret
lookup precedence. These gates cover bounded QString framing, exact import
attributes, absent native-provider compatibility, owner changes, locked and
cancelled prompts, resource cleanup and preserved-original removal errors.
`tst_qindamail_accounts` verifies that incomplete cleanup removes the account
durably and emits the existing visible warning; successful cleanup emits no
warning. Synthetic results do not prove real wallet unlock or server login.

- [x] `jsonRoundTripKeepsUnknownFields`: unknown keys at the root, in an
      account, in `incoming` and in a calendar entry survive load → update →
      save; the typed fields round-trip exactly.
- [x] `validation` (12 rows): kind, address, host, port, clear text to a
      non-loopback host, `xoauth2` without a provider, a calendar on POP3,
      `http:` CalDAV, Microsoft with a password, sync interval, and a
      secret-named key nested or at the top — each refused with its field
      path, and nothing written.
- [x] `noSecretOnDisk`: with a password in the (in-memory) keyring, the
      written `accounts.json` contains neither the password nor the word.
- [x] `removeWipesSecrets`: removing an account removes every service's
      secret for it and leaves another account's alone.
- [x] `corruptFileIsMovedAside`, `newerSchemaIsReadOnly`: never overwrite
      what cannot be read or what a newer build wrote.
- [x] `presets` (14 rows) and `everyPresetMakesAValidAccount`; `draftFor`.
- [x] `clientRegistryOrder`: account → user file → system file; a
      secret borrowed for a matching ID; a broken file reported; the
      "no client ID" text with the Gmail app-password hint.
- [x] `secretsInMemory`: `check` never returns the value, `applySecret`
      reaches `setSecret`, unknown services and bad ids refused, a missing
      secret service reported (`unavailable`) with nothing stored.
- [x] `qmlTypesRegistered`: `import QindaOffice.Mail` creates all four
      types and wires them.
- [x] `parseGmail` / `parseOutlook` / `parseFastmail` /
      `parsePlaceholdersAndSkipPlainText` over ISPDB-shaped fixtures
      (`tests/office_mail/fixtures`), `parseRejectsGarbage` (5 rows).
- [x] `presetNeedsNoNetwork`, `fallbackOrder` (preset → autoconfig →
      well-known → ISPDB, the address sent only to step 2),
      `firstSourceWins`, `invalidDocumentFallsThrough`, `nothingFound`,
      `unreachableIsOffline`, `httpsOnly`, `oversizedAndSlowAnswers`,
      `cancelAndBadInput`.
- [x] `pkceMatchesRfc7636AppendixB` (the RFC's verifier → challenge
      vector) and verifier/state shape.
- [x] `authorizationRequest`, `fullFlowMicrosoft`,
      `googleOneTokenAndClientSecret`: the test plays the browser; the
      fake token server checks code, verifier against the challenge,
      redirect URI and client; the refresh token lands in the keyring and
      the loopback port closes.
- [x] `secondResourceSameRefreshToken`: the Graph token comes from a
      refresh-token grant with `offline_access Calendars.ReadWrite` and the
      same refresh token (`+/=` intact); a rotated refresh token is stored.
- [x] `stateMismatchRejected` (no code exchange), `deniedConsent` (nothing
      reflected into the page), `refreshFromKeyringThenCached`,
      `concurrentRefreshesShareOneGrant`, `invalidGrantNeedsReauthorization`
      (token forgotten and wiped), `serverTroubleIsOffline`,
      `timeoutAndCancel`, `applyAccessToken`, `configureFromRegistry`,
      `xoauth2Format` (Google's published example).
- [x] `tokensNeverLogged`: every message of a full flow with all debug
      categories enabled, and every result, is free of the tokens.
- [ ] On the desktop (needs a registered client ID; QindaTK WP-089/WP-090):
      sign in to a real Gmail and Microsoft 365 account from the account
      dialog (M44), confirm the browser page, the keyring entry (service
      `qindaoffice-mail` in Seahorse or KWalletManager) and a mail sync
      with the token.

### M43 gates (Exchange)

Automated by `tst_mail_exchange_mapping`, `tst_mail_exchange_ews`,
`tst_mail_exchange_ews_writes`, `tst_mail_exchange_graph` and
`tst_mail_exchange_graph_writes` (`ctest -R exchange`), against in-process
EWS and Graph endpoints on 127.0.0.1 — no real network. Rationale in D-044;
behaviour in [mail.md](mail.md#exchange-and-microsoft-365-calendars).

- [x] Initial sync maps a single meeting (organizer, attendees, join URL),
      a read-only all-day event, a weekly Denver series across the 8 March
      2026 DST change with one deleted and one moved occurrence, and a
      second-Tuesday series (`recurrenceRaw`); the toolkit projection puts
      the series at 16:00Z, then 15:00Z, the moved instance at its new time
      and nothing on the deleted day (EWS and Graph).
- [x] A second sync, and a sync after reloading the store file into a new
      store and adapter, fetches and writes nothing.
- [x] Graph pages every collection through `@odata.nextLink`; an exception
      moved outside the window is an override, not a deletion.
- [x] A local edit is written with the stored `ChangeKey` /
      `If-Match: <etag>` and only the changed field; a server edit plus a
      local edit is a conflict with nothing overwritten; `resolveConflict`
      `local` writes over the recorded version (a further server edit gives
      412 and a fresh conflict), `remote` adopts the server copy.
- [x] Local create (EWS `CreateItem` / Graph `POST` with `transactionId`)
      in Europe/Zurich sends the Windows zone and keeps the record's zone;
      `removeRemote` deletes conditionally; a server deletion removes the
      record, or makes a conflict when it was edited here.
- [x] Occurrence edits: a moved occurrence is `UpdateItem` on
      `OccurrenceItemId` `InstanceIndex` 4 / `PATCH` of the instance; an
      exclusion is `DeleteItem` #5 / `DELETE` of the instance; restoring a
      deleted occurrence is a conflict with a reason.
- [x] 401 → `rejected` with `authRejected`; 429/503 `Retry-After` and EWS
      `BackOffMilliseconds` → `offline`, `retryable`, `retryAfterSeconds`;
      refused connection → `offline`; plain `http:` to a non-loopback host
      and a foreign `@odata.nextLink` → `rejected` before the token is sent.
- [x] The password, `user:password` base64 and the tokens appear in no
      result, transcript, captured log line (Qt network categories and the
      adapter's own category enabled) or saved store/recovery file.
- [x] Windows↔IANA mapping (preferred zone kept), DST wall times, pattern
      mapping incl. Exchange's month-end semantics, Retry-After forms, XML
      DTD/depth refusal (`tst_mail_exchange_mapping`).
- [ ] Against a real Exchange 2016+/Exchange Online mailbox (WP-089/WP-090):
      `InstanceIndex` addressing, the Graph `$filter`, all-day events created
      in Outlook, attendee notifications from Graph edits.

### M48 gates (Mercury and Stripe connections)

Automated by `ctest -R 'books_connect|qindabooks_connections'` against a
fake Mercury + Stripe API on 127.0.0.1 (`tests/books_connect/
fake_bank_api.h`, the documented response shapes; the fake checks the
Bearer credential and echoes a refused key in its error, as Stripe does),
an in-memory keyring, `TZ=UTC`, "today" 2026-03-31. No real network.
Rationale in D-048; the model in
[books.md](books.md#bank-and-payment-connections-mercury-stripe).

- [x] `tst_books_connect_mercury`: v3 storage — the company uuid is
      stable, `Connections::save` refuses token-shaped values and unknown
      providers, a payment link is current only at the open balance; a
      refused token (401) is not stored and not echoed; the accepted one
      is stored under `<uuid>/mercury`, sent as `Bearer`, never in the
      file; linking creates "Mercury Checking ••1234" or reuses a bank
      account (not an expense account), unlinks; sync over three pages
      (offset paging, `start` 90 days back, `order=asc`) imports the
      three `sent` transactions and skips pending/failed, dates on
      `postedAt`, payee/memo/FITID/cents exact; a second sync imports 0
      and starts from the cursor; the pending one imports once when it
      posts; rules suggest on synced lines; http to a non-loopback host
      is refused before any request; disconnect removes the token and
      keeps the feed; a lost token reports "needs its API token again";
      no token in captured logs.
- [x] `tst_books_connect_stripe`: a refused key is not stored and the
      echoed key is redacted; connect creates "Stripe (USD)" (bank) and
      "Merchant Fees" (expense), payouts default to the Mercury-linked
      account, reconnecting creates nothing new; a six-transaction
      history over three pages (`starting_after`, `expand[]=data.source`):
      charge with invoice metadata → invoice Paid and the gross line
      matched, fees categorized to Merchant Fees (3.20 + 1.75 + 15.00 +
      0.05), a plain sale, refund and dispute left New, the payout booked
      as a transfer and matched, the Mercury "STRIPE TRANSFER" deposit
      matched to it — **Mercury first and Stripe first** — never income;
      re-sync imports and applies nothing; after reviewing the rest the
      clearing account = Stripe available + pending (35.00) and the trial
      balance balances with no integrity problems; another company's
      invoice metadata is left for review with a note; a payment link
      sends `unit_amount` = open balance, `currency`, `product_data[name]`,
      metadata and `payment_intent_data[metadata]` (invoice, company uuid,
      number), `restrictions[completed_sessions][limit]=1`, form-encoded,
      with Idempotency-Keys per invoice + amount; a second request for the
      same amount reuses the link without a request; after a partial
      payment a new link for the new balance with a new key.
- [x] `tst_books_io_forms::payOnlineLinkIsPrinted`: a stale link is not
      printed; a current one prints "Pay online: <url>" under PAYMENT
      INSTRUCTIONS (PDF text via Poppler).
- [x] `tst_qindabooks_connections`: the QML API end to end —
      `connections()` states, `connectMercury` (refused, then accounts
      with `balanceText`), `linkMercuryAccount`/`mercuryLinks`,
      `connectStripe`/`stripeSettings`/`saveStripeSettings` (a non-bank
      clearing account refused), `createPaymentLink` →
      `paymentLinkFinished` and `document(id).paymentLinkUrl`,
      `emailDocument` launches `qindamail --attach <pdf> mailto:…` with
      "Pay online: <url>" in the body, `syncConnections` →
      `connectionSyncFinished` (status `syncing` meanwhile; a second
      request fails fast with its own id), `disconnect` removes the
      keyring entry; no token in any result, log or the company file; a
      company without connections never asks the keyring.
- [x] `tst_qindabooks_host`: `booksSyncBankAction` (F5, `refresh-cw`,
      "Sync Mercury & Stripe") and `booksConnectionsAction` (`link`,
      emits `connectionsRequested`) exist, first in the Banking menu;
      `tst_qindabooks_menu_export` passes the validator with them.
- [x] `tst_qindamail_host::attachHandOff`: `--attach` (both spellings,
      repeatable, relative paths made absolute) folds into one
      `qindamail-compose:` token; the draft has the mailto: fields and the
      files imported into the attachment store (a PDF's bytes and MIME
      type); a missing file is named in `missingAttachments`; a mailto:
      `attach=` is ignored.

Remaining for the lead: `ConnectionsDialog.qml` (a registered stub), the
two commands on the ribbon's Banking tab (the ribbon gate exempts them
until then — remove the exemption in `tst_qindabooks_ui` with that
change), and the on-desktop checks: connect a real read-only Mercury token
and a Stripe test-mode restricted key, sync, pay a test-mode payment link
with Stripe's test card and see the invoice turn Paid on the next sync.

### M50 gates (QindaDiagram, D-050)

Automated by `ctest -R diagram` (headless, temp dirs and XDG dirs).
Rationale in D-050; the model and API in [diagram.md](diagram.md).
QindaDiagram builds against a QindaTK prefix that has D-302
(`ShapePalette` tiles, category menu, drag reports — QindaTK branch
`work/diagram-shape-palette`).

- [x] `tst_diagram_model` (12): ids never reused (a hand-edited file's
      next id is skipped); the catalogue has every stencil the brief names
      in seven categories, each with a non-empty outline and a thumbnail;
      paper names both ways; case-insensitive shape data; the `.qdiagram`
      round trip keeps every field and unknown members on the root, a page
      and a shape (written back out); newer, foreign, malformed and
      page-less files refused; atomic save/load (a missing directory
      fails with a message); clipboard fragments carry connectors between
      copied shapes only; `DrawingDocument` — one undo step per real edit,
      no step for a no-op, merged nudges, dirty follows the clean state,
      recovery is exact and dirty; save/load resets the stack; themes
      restyle colours and fonts, keep widths and label sizes.
- [x] `tst_diagram_ops` (13): deleting a shape frees its connectors in
      place; a lane carries the shape inside it and a free end follows
      its shape, locked shapes stay; align/distribute (equal gaps,
      outermost stay); rotate/flip about the selection; z-order in both
      directions; one-level groups expand selections; duplicate and paste
      re-glue copies (ports kept) and free foreign ends; shape/text/
      connector patches, format painter, data; find (text and data) and
      replace; pages (add, duplicate with fresh ids, move, rename, the
      last page stays) and fit-to-contents; glue picks a port, then the
      outline, else free.
- [x] `tst_diagram_output` (10): orthogonal routes are Manhattan, leave
      along their port and detour round a blocker; dynamic glue faces the
      other end; straight meets the outline; curved is a cubic; line jumps
      hop the later connector only; layered layout ranks a flowchart with
      a loop down one axis and re-glues bottom → top, left-to-right too;
      the org chart from CSV (quoted names, data columns, an unknown
      manager reported, a hanging team of four); draw.io import (lane
      children in page coordinates, HTML labels, `UserObject` data, edge
      style/dash/labels, a free end; compressed and non-draw.io files
      refused); PNG (page size, selection with transparency), SVG, the
      paged print painter, bounds including icon captions; the stencil
      sheet grab.
- [x] `tst_qindadiagram_host` (9): 101 `diagram…` commands with labels
      and icons, no shared shortcut; enablement and the Shape/Connector
      ribbon contexts follow the selection; the tool radio group mirrors
      the service; pages, paper and orientation checks, themes; styles,
      grow font, format painter (refused without a source), data; the
      canvas adapter's proxies, a move and a resize each one undo step,
      stale edits refused; copy in one window, paste in another (glue
      kept), Copy as Picture; CSV and draw.io import through the service;
      PNG/SVG export; the print callback's PDF; save and reopen.
- [x] `tst_qindadiagram_menu_export` (2): the exported tree — File Edit
      View Insert Format Arrange Design AI Settings Help — passes the
      desktop's validator with and without a selection; every `diagram…`
      command appears exactly once and every exported id resolves and
      fires (90+ pulled).
- [x] `tst_qindadiagram_ui` (6): the real `DiagramWindow.qml` over three
      seeded drawings — a volunteer-onboarding flowchart across three
      swimlanes, a nonprofit's org chart built from CSV, an office network
      in a container — at 1320 and 520 px (drawers below 900 px, minimap
      over the network page); a decision's inspector (geometry, fill,
      line, effects, quick styles) and a server's shape data; typed search
      across stencils; a panel drop lands (centre-snapped) where released;
      the connector tool glues both ends; arrow-key nudges merge and undo;
      F2 edits text in place and Escape stores it; the ribbon gate at 1320
      and 520 (the assistant menu-only: nothing to offer a drawing yet);
      structural fixtures `qindadiagram_{1280,520}.txt`.
      Grabs reviewed: `M50-iter1…4-*` (flowchart, org chart, network at
      1320/520, inspector, shape data, stencil search and the Network
      stencil, new drawing, text editing, ribbons, the stencil sheet).

Remaining for the lead: the launcher card; the on-desktop checklist
(drag shapes with a real pointer and tablet, print a page and preview
it, open two drawings side by side and paste between them, the panel
menu, dark and light themes).
### M51 gates (QindaPlan)

Automated by `ctest -R "plan"` (headless; the app tests pin "today" to
Monday 30 March 2026 and seed the "Spring fundraiser gala": 4 phases, 36
subtasks, 7 milestones, 6 resources with one overallocation, a baseline
and three days of slippage). Rationale in D-051; the model in
[plan.md](plan.md).

- [x] `tst_plan_calendar` (7): working-time arithmetic on the standard
      calendar (a day ends 17:00 the same day, lunch, weekends, negative
      spans), exceptions and a resource calendar over its base (holidays
      inherited, a working Sunday, a vacation), degenerate calendars;
      duration, date and money text both ways.
- [x] `tst_plan_schedule` (15): hand-computed dates for FS chains, parallel
      paths with slack, SS/FF/SF with lag, lead and 50% lag, all eight
      constraints (an FNLT conflict reported and honoured, negative slack
      upstream), ALAP by free slack, summaries and summary links, milestones,
      inactive/manual/started tasks, cycles and dangling links ignored with
      issues, the link checks, resource calendars, work and integer cost;
      2,000 tasks schedule in ~30 ms (< 1 s gate).
- [x] `tst_plan_edit` (9): sheet fields as text with Project's typing
      semantics, the predecessor and resource-name languages, outline
      edits, link/unlink, % complete and actuals, manual/auto, respect
      links, Gantt moves, Update Project, assignments.
- [x] `tst_plan_file` (8): byte-identical `.qplan` round trip, unknown
      fields kept at every level, newer version refused, repairs with
      warnings, atomic save, the task clipboard, CSV.
- [x] `tst_plan_resources` (9): per-day overallocation and the usage table
      (days and weeks), leveling within slack (finish unmoved) and beyond
      it, started and pinned tasks untouched, baselines and variances, the
      four reports (late vs slipped).
- [x] `tst_plan_mspdi` (6): a Project 2016-shaped file imported field by
      field and scheduled (both exception forms, all link types with lag,
      lead and %, a resource calendar, material, baseline, manual and
      inactive tasks), export → import identical, well-formed schema order,
      malformed input refused with a line number.
- [x] `tst_plan_output` (6): Gantt pagination (pages wide × row pages,
      hidden subtasks), Gantt and report PDFs, a report table that repeats
      its header across pages, the Timeline PNG.
- [x] `tst_qindaplan_service` (10): the gala schedules as documented
      (gala night Thu 14 May, three days behind its baseline, inside its
      Finish No Later Than; Marcus overallocated from 7 April); sheet edits
      and the blank-row commit as single undo steps with dirty tracking;
      ribbon operations; assign/level/clear; calendars, baselines, Update
      Project, reports; `.qplan`, MSPDI, CSV, PNG and PDF out and back; the
      Gantt/sheet/calendar/usage projections; a Gantt drag previewed and
      committed through the engine and undone.
- [x] `tst_qindaplan_menu_export` (4): the panel export passes the
      desktop's validator with and without a project; every `plan…Action`
      appears exactly once and every exported command resolves and fires.
- [x] `tst_qindaplan_ui` (7): the real `PlanWindow.qml` at 1280 and 520 px
      — Gantt (critical bars, summaries, milestones, baselines, today and
      status markers, the Timeline strip), in-place sheet editing committed
      through the engine, resource sheet, usage with the overallocation
      banner, calendar, overview report; Task Information opens on the
      selected task; the ribbon gate at 1320 and 520 px (no menu-only
      commands beyond the shared ones); structural fixtures
      `qindaplan_{1280,520}.txt`.
- [x] Grabs looked at (validation gallery, `M51-*`): the Gantt with the
      critical path and baselines, sheet editing, resource sheet, usage
      with the overallocation, calendar, overview report and Task
      Information at 1280 and 520; the printed Gantt, reports and Timeline
      (`M51-iter*-output-*`); the toolkit's Gantt example (`M51-tk-*`).
- [ ] On the desktop: print the Gantt two pages wide to a real printer;
      open an exported `.xml` in Microsoft Project and in ProjectLibre;
      import a real Project-saved XML with resource calendars.

### M49 gates (QindaBase — `libs/base_engine`, `apps/qindabase`)

Automated by `ctest -R "base_|qindabase"` (headless; temp dirs and XDG
dirs per test, so backups never touch the user's). Rationale in D-049;
the file, types and API in [qindabase.md](qindabase.md).

- [x] `tst_base_types` (3): type ids and declared types round-trip; foreign
      affinities map; currency parses and prints exactly (grouping,
      parentheses, too many decimals refused, INT64_MIN, 0.10 + 0.20 =
      $0.30, QML doubles, zero-decimal currencies); every type parses what
      people type and refuses the rest with a reason.
- [x] `tst_base_file` (10): the `.qbase` identity (application_id at
      offset 68 = "QBAS", journal DELETE); a plain SQLite file opens
      byte-for-byte untouched until consent, then keeps its user_version;
      non-databases refused; orphans reported by the integrity check;
      backups never over a file, automatic backups named and kept; compact;
      settings and unknown keys survive; captured changes undo and redo a
      cascaded delete exactly; a changed record refuses a stale undo;
      whole-database images restore; statement splitting.
- [x] `tst_base_schema` (9): create/read back (captions, defaults, Date(),
      indexes); refused designs (clashes, reserved names, two AutoNumbers,
      bad defaults, dropping a referenced table); a table rename keeps
      relationships; renames and added fields by plain ALTER; type changes
      through the rebuild convert exactly (REAL 19.99 → 1999 cents, text
      that does not fit kept and counted in the preview, date-time → date
      backed up) and undo/redo as images; required/unique checked against
      the data; dropping a field backs up and keeps enforcement; a
      relationship refused while an orphan exists, then SET NULL works;
      a foreign file's declared types survive a rebuild without adding
      system tables.
- [x] `tst_base_records` (6): sort, filter (every operator, OR filters via
      the app), truncation; typed cell edits with named refusals, lookups
      by display name, multi-cell steps; required fields on new records;
      delete impact (blocked / cascades) and undo of a cascade; tab-separated
      paste (quotes, updates + appends, one step, bad cell names its row);
      exact totals (sum $158.46, average $39.62, count, max date).
- [x] `tst_base_query` (7): criteria convert by type (> 20 on currency =
      2000), Between/In/Like/Is Null/Not, bare words, quotes stay literal;
      Or rows and cross-field criteria; joins + totals + Where + HAVING,
      money expressions in minor units; parameters typed by their field;
      hand-written SQL inspected (DDL and PRAGMA refused); update/delete/
      append previewed, refused on a changed count, run and undone;
      unknown design members round-trip.
- [x] `tst_base_io` (5): saved objects (case-blind clash, rename, delete +
      undo, a table rename rewrites their sources); CSV detection (`;`,
      BOM, quoted delimiters, leading-zero codes, currency, dates, Yes/No)
      and all-or-nothing import, skip-bad-rows, keyed ids, no half-imported
      table; every template builds and its queries run; form mirror and
      report snapshot shapes; Access import through fake mdb-tools, and
      the message when they are missing.
- [x] `tst_qindabase_service` (7): templates and navigation; table design
      plan (readable SQL, bookkeeping summarised) → apply → undo/redo; a new
      table and a relationship; renames carry forms and reports along;
      queries (select into the datasheet, parameters asked, update with a
      confirmed count and undo, saved drafts, SQL console); forms and
      reports open as toolkit design files (DocumentManager), records saved
      through the form exactly, wizards; CSV import as one step, CSV and
      XLSX export of what is shown; maintenance; plain SQLite consent.
- [x] `tst_qindabase_host` (5): 52 command ids with icons and labels, no
      shared shortcut, all disabled without a database; enablement follows
      the view and selection, contextual tabs; Undo/Redo over recorded
      steps with the text of the step, a stale step clears the history
      with a message, the designer's history while one is open; a dirty
      design dirties the window and Save flushes it; Save As copies and
      switches; a report is the print source (PDF export), leaving it
      clears the source.
- [x] `tst_qindabase_menu_export` (2): the exported tree passes the
      desktop's validator with and without a database — File Edit View
      Records Create External Data Design Database Tools AI Settings Help —
      every `base` command appears exactly once, every exported id resolves
      and fires.
- [x] `tst_qindabase_ui` (10): the real `BaseWindow.qml` over the Donors &
      Donations starter seeded with 40 donors, 4 campaigns and 120
      donations: the start gallery and an untitled database; the datasheet
      (lookup names, totals row, an edit undone from the Edit menu, filter by
      selection, the PivotTable); the table designer with the relationship
      and a pending plan; the query designer with a join and totals, run;
      a form over 120 records; a report (120 rows, printable, PDF); the
      relationships diagram and the SQL console; the plan dialog, CSV import
      proposal, report wizard; the ribbon gate at 1280 and 520 px (Replace
      menu-only); structural fixtures `qindabase_{1280,520}.txt`.
      Grabs reviewed: `M49-iter*` (start, home, datasheet, pivot, table
      design, query design and result, form, report and design,
      relationships, SQL, plan dialog, import, report wizard; 1280 and
      520). `M49-iter3-tkbranch-*` are the same run against QindaTK
      `work/base-db-controls` (money in reports, aligned form labels, the
      schema badge as a file name).
- [ ] When QindaTK `work/base-db-controls` lands: rerun
      `tst_qindabase_ui` with `QQO_UPDATE_FIXTURES=1` — the toolkit adds
      `formStatus`, `formType_*` and `schemaDatabase` object names, so
      `qindabase_1280.txt` changes at its FormDesigner lines (every other
      QindaBase gate already passes against that branch).

Still on the desktop: open a real Access file with mdb-tools installed;
print a report to a real printer; two databases side by side.

### M47 gates (the QindaBooks app)

- **UI** (`tst_qindabooks_ui`): the real `BooksWindow.qml` over a seeded
  company (customers, products, four invoices — one overdue, one paid —
  a bill, an expense, an imported bank statement): the dashboard's
  figures, every workspace renders, the invoice editor opens with totals
  the service computed, the ribbon gate at 1320 and 520 px (edit commands
  and Save As menu-only), structural fixtures `qindabooks_{1280,520}.txt`.
  Grabs reviewed: `M47-iter2-*` (dashboard, sales, expenses, banking
  review feed, accounting, reports, invoice editor).

Automated by `ctest -R qindabooks` (headless, temp dirs and XDG dirs,
"today" pinned to 2026-03-31). Rationale in D-047; the API in
[books.md](books.md#qindabooks-app).

- [x] `tst_qindabooks_service`: create from a template (one company per
      window: a second create is refused), templates and currencies;
      settings round trip incl. fiscal July, cash basis, prefixes, notes,
      accent (bad month/currency refused); money text both ways (locale
      form, parentheses, excess decimals refused); customers/vendors
      (address, deactivate), items (`price` text, `type`), a bank account
      with an opening balance, payment/expense/income account lists,
      system accounts cannot be deactivated, classes, tax rates reusing
      their agency; an invoice with a 10% discount and 10% tax on the
      taxable line (900.00 − 90.00 + 36.00 = 846.00, due date from Net
      30, one totals entry per input row) saved as INV-1001, read back,
      listed/filtered/searched, edited in place (940.00); drafts only for
      invoices, draft → posted, estimate → invoice (estimate closed), void
      (zero balance, audit rows); bills (overdue status), an expense on
      the card (edit refused), two bills paid in one run (one payment per
      vendor, 50.00 left); a payment across two invoices (paid +
      partial, 200.00 open); unbalanced journal entry refused with the
      difference, balanced posted; transfer; deposit from Undeposited
      Funds; lock date refuses an earlier invoice; `changed()` fires on a
      commit and not on a refusal.
- [x] `tst_qindabooks_banking`: CSV import (3 lines, re-import 0 + 3
      duplicates), categorize, exclude/restore, a rule applied and
      accepted, rule edit replaces, a recorded payment offered and
      accepted as a match; OFX card statement; a CSV the detector cannot
      read returns `needsMapping` with its header, and imports with an
      explicit mapping; reconciliation refuses to finish at 800.00
      difference, finishes at zero, register shows reconciled lines with
      deposit/payment text and running balance, report, undo; P&L map
      (section/account/total rows, `total:net_income`, cash basis, month
      columns) with account drill-down to the invoice, Balance Sheet
      balances, A/R aging `party:<id>` drill-down, unknown report and
      reversed period refused; range presets for a calendar and a July
      fiscal year (two different "todays"); KPIs equal the P&L, trends
      with `…Chart` doubles, expense mix, 7 ratios (net margin 73.3%);
      report PDF/CSV, invoice PDF, IIF, journal CSV, customer list CSV
      imported into another company; timestamped backup and Backup…
      never over a file; e-mail launches `qindamail --attach <pdf>
      mailto:` with the subject (M48: the PDF is attached); monthly recurring from
      Jan 31 generates 3 invoices once.
- [x] `tst_qindabooks_host`: all 40 command ids (38 + the two M48 ones) exist with icons and
      labels, Ctrl+I and Ctrl+1…6, New reads "New Company…"; no shortcut
      is shared; commands disabled until a company exists and the setup
      request fires once; the workspace radio group keeps exactly one
      checked from the menu and from `currentView`; every command emits
      its request; Save As switches the window and the service to the
      copy (original untouched) and Save is a successful no-op; report
      and document print sources export PDFs (twelve month columns
      prefer landscape), clearing disables Print, Print Document prints
      the selection; the (stub) `BooksWindow.qml` instantiates.
- [x] `tst_qindabooks_menu_export`: the exported tree (with and without a
      company) passes the desktop's validator — File Edit View Sales
      Expenses Banking Accounting Reports AI Settings Help — every books
      command appears exactly once, every exported id resolves and fires.

Remaining for the lead's QML merge: fixtures and grabs of the real
window at 1280/520, the ribbon over these ids (squeeze), the launcher
card, and the on-desktop checklist (open two companies side by side,
print an invoice and a report, e-mail an invoice into QindaMail).

### M45/M46 gates (bookkeeping engine and I/O)

Automated by `ctest -R books` (headless, temp dirs). Rationale in D-045
and D-046; the worked example is in [books.md](books.md).

M45 — `books_engine`, `books_document`:

- [x] `tst_books_money`: 0.1 + 0.2 = 0.30 exactly; overflow detected
      (never wraps); INT64_MIN prints; JPY/KWD exponents; half-up vs
      half-even vs toward/away from zero; allocation sums exactly and each
      part is within one minor unit; locale parse (grouping, symbol,
      parentheses, trailing minus, German decimal comma) refuses excess
      fraction digits; **no `double`/`float`/`qreal` in the engine
      sources** (scanned).
- [x] `tst_books_company`: create/reopen (one file, no `-wal`); refuse a
      foreign file, a newer schema ("newer version"), a missing file;
      migrate a v1 file to v2 with a `.pre-v2.bak` copy and an audit row;
      `VACUUM INTO` backup never overwrites; settings, counters, logo;
      currency fixed once posted; all four templates name every system
      role and balance (also with opening balances); nonprofit functional
      groups; chart rules (duplicate, cross-class parent, delete with
      postings refused, deactivate only at zero balance, inactive account
      refused).
- [x] `tst_books_ledger`: balanced/unbalanced/one-line/unknown-account/
      currency-mismatch; lock date refused, override without reason
      refused, with reason stored + audited; void = zero effect with lines
      and trail kept; edit = void + repost with the same document id and a
      failed edit changing nothing; triggers refuse UPDATE/DELETE of
      posted lines, un-void and audit edits; opening balances.
- [x] `tst_books_sales`: estimate → invoice (10% discount, 6% + 1.25%
      two-agency tax on 1,800.00 = 130.50, total 2,065.50) → partial
      payment → over-applications refused → overpayment credit → credit
      applied → taxed credit memo → two refunds: A/R 0, Sales Tax Payable
      123.25 (State 102.00, City 21.25), statement of 7 lines closing at 0;
      drafts, edits under payments, voided payment reopens the invoice;
      sales receipt to Undeposited Funds; recurring Jan 31 monthly → Feb 28,
      Mar 31, Apr 30, re-run creates nothing.
- [x] `tst_books_purchases`: bills with vendor terms, vendor credit
      applied, two bills paid in one run (one payment per vendor), card
      charge and check, edit below payments refused.
- [x] `tst_books_banking`: transfers and registers (bank and card sign);
      deposits from Undeposited Funds, no double deposit, void returns
      them; bank feed dedup (hash + occurrence keeps two identical
      coffees, FITID beats text), rules by priority/direction/amount,
      accept suggestions, match suggestions ranked by date and payee,
      exclude/restore/categorize as Check; reconciliation refuses to
      finish at a non-zero difference, report numbers, reconciled lines
      block void, next session starts at the reconciled balance, undo.
- [x] `tst_books_reports` (fixture Q1 2026): P&L accrual (net −200.00)
      and cash (−720.00); months, classes, prior year and prior period
      with % change; Balance Sheet balances on both bases at four dates;
      net income rolls into Retained Earnings next year; cash flow ties to
      the change in cash on both bases and for February alone; trial
      balance debits = credits (11,720.00); aging exactly 30 days late in
      1–30, 31 in 31–60, 91 in 91+; balances, open invoices/unpaid bills
      as of dates; sales by customer/item on both bases; expenses by
      vendor/category; sales tax liability; budget vs actual; GL, journal,
      transaction list; cash recognition of a 3-way split payment is exact.
- [x] `tst_books_analysis`: the seven ratios against hand arithmetic,
      unavailable ratios, expense mix, monthly trends with month-end cash,
      trailing-average forecast.
- [x] `tst_books_perf`: 50,000 journal lines; P&L and Balance Sheet each
      < 1 s (measured ~0.5 s and ~0.07 s in a Debug build).
- [x] `tst_books_document`: never dirty, own commits refresh the byte
      revision, Save no-op, Save As copies and switches (and replaces an
      existing target), recovery refused, untitled scratch company dirty
      until saved.

M46 — `books_io`: see the list below (tests `tst_books_io_*`).

- [x] `tst_books_io_forms` (8): invoice PDF is one page and its Poppler
      text holds company, number, customer, address, total, balance due,
      discount/tax labels and "Page 1 of 1"; an 80-line invoice runs to 5
      pages with the header repeated and every line printed once;
      estimate, sales receipt, credit memo, statement (aging strip); void
      refused; a QPrinter through `asPrintCallback` paints the same page.
- [x] `tst_books_io_reports` (10): P&L PDF text; CSV round trip of P&L,
      Balance Sheet, Trial Balance, A/R aging detail to the minor unit;
      12-month P&L goes landscape; multi-page journal repeats its header;
      CSV quoting and delimiter detection.
- [x] `tst_books_io_bank_import` (28): amount edge cases, date-format
      detection (day > 12, month names, 2-digit years), both CSV fixtures,
      explicit mapping with `invertSign`, per-row errors, OFX 1.x SGML,
      QFX 2.x XML credit card, QIF; each fixture imported twice through
      `Banking::importLines` adds nothing the second time.
- [x] `tst_books_io_export` (7): journal CSV balances; list CSV
      export → import round trip; IIF headers, account types and every
      TRNS…ENDTRNS block summing to zero; timestamped backups keep the
      newest N and each opens clean.
- [x] Grabs looked at (validation gallery inbox, `M46-books-*.png`):
      invoice, invoice page 2, statement, Profit and Loss, Balance Sheet.

- [ ] On the desktop (M47 app lane): print an invoice to a real printer
      and compare with its PDF; open an IIF export in an accountant's
      QuickBooks.

### M39 gates (floating images) and the parity close-out

Automated by `tst_calc_io_images`, `tst_calc_window` and `tst_calc_sample`.
Rationale in D-040.

- [x] `imagesRoundTrip`: a real JPEG survives byte-for-byte (the point of
      not using QXlsx's insertImage), with its format, anchor and extent;
      the media part, the content-type default, the drawing relationship
      and the `<xdr:pic>` anchor are package-inspected.
- [x] `picturesAndChartsShareOneDrawing`: a sheet with both gets one
      drawing part holding both anchors, and both come back on re-read.
- [x] `imagesFollowStructuralEdits`: anchors move with inserted rows and
      columns, ignore edits on other sheets, and go with a removed sheet.
- [x] `imagesInsertPaintAndRecover`: Insert ▸ Image… lands a picture at
      the cursor as ONE undo step, the grid paints it (the pixel inside
      its rect is the image's own colour), and the recovery payload
      brings the bytes back.

The close-out for the whole parity run (M32–M39), sample-gated:

- [x] `sampleSurvivesOurOwnSaveAndReopen`: the author's workbook, saved by
      us and read back, is still the author's workbook — all 11 sheets,
      106,464 cells, 106,402 styles, 8 conditional formats, 9 validations,
      6 tables, 4 charts, 6 series, the frozen panes, the print areas, the
      active tab and the recomputed Forecast value, with zero error cells
      on both sides. This gate is what caught the blank-but-styled cells
      the writer had been dropping (2,114 of them).
- [x] `whatWeWriteOpensInLibreOffice`: our package converted by headless
      LibreOffice and read back — the external judge that our own reader
      and writer do not merely share a bug.
- [ ] On the desktop, with real Excel: open the saved file and confirm the
      look. The one check no test here can make.

### M38 gates (array arithmetic)

Automated by `tst_calc_functions` and `tst_calc_window`. Rationale in D-038.

- [x] `arrayArithmeticBroadcasts`: the sample's own shape
      (`SUMPRODUCT(A1:A5,(1+0.1)^A1:A5)`), every operator broadcasting a
      scalar against a range and a range against a range, the
      `--(A1:A5>2)` coercion idiom, row-against-column stretching, and
      `TEXTJOIN` over a computed array.
- [x] Same gate, negative half: a shape mismatch is `#VALUE!`, an array in
      scalar position is `#VALUE!` unless it is 1x1, and an error element
      is the whole aggregate's answer.
- [x] `fixtureRows` stays green — the 300+ LibreOffice oracle rows are the
      check that broadcasting did not change any scalar answer.
- [x] `opensARealExcelWorkbook`: the sample workbook now reports **zero**
      error cells across all eleven sheets (was 12, pinned since M32).
- [ ] On the desktop: open the sample, read Forecast B26:C28 against the
      values Excel shows.

Recalculation cost (D-039), same milestone:

- [x] `orderingTheDirtySetStaysCheap`: 3000 formulas packed into one row
      band, all reading ranges that cover it — the shape where the sort
      used to rebuild the same candidate set once per dirty cell. Budget
      3s; measured 10.5s before the fix and ~1.1s after, so the gate
      separates the two worlds with room on both sides.
- [x] `undoBackToTheSavedStateClearsDirty`: undo and redo each land the
      right cell value, which is what makes skipping the push-time redo
      safe.
- [x] `sampleRulesEnforceAndImport` (sample-gated): one edit to
      `Controls!B5` in the 106k-cell workbook, start to finish, in 7.8s
      of test — it used to trip QtTest's 300-second watchdog.

### M37 gates (view & print state)

Automated by `tst_calc_io_view` and `tst_calc_window`. Rationale in D-037.

- [x] `viewStateRoundTrips`: frozen panes (`state="frozen"`, xSplit/
      ySplit), hidden sheets (`state="hidden"`), the active tab, the
      selection cell and page setup (landscape/fitToWidth) survive.
- [x] `printNamesAreLocalDefinedNames`: `_xlnm.Print_Area/Titles` write
      with `localSheetId`, read into per-sheet fields, and never enter
      the formula name table.
- [x] `freezeToggleIsUndoableAndRecovered`: the freeze toggle lands on
      the sheet (undo clears, redo restores) and the recovery payload
      carries it.
- [x] `hiddenSheetsLeaveTheTabStrip`: a hidden sheet drops out of the
      strip, activating a view index reaches the right engine sheet, and
      hiding the active sheet activates a visible one.
- [x] `printHonorsThePrintArea`: the exported PDF (read back through
      Poppler) contains rows inside the area and none outside it.
- [ ] On the desktop: freeze row 1, hide a sheet, save, reopen — the
      pane, the tab and the print layout come back as left.

### M36 gates (Excel tables + the custom chart renderer)

Automated by `tst_calc_io_tables`, `tst_calc_structure` and
`tst_calc_window`. Rationale in D-036.

- [x] `tablesRoundTrip`: the table part, content-types, rels and the
      `<tableParts>` element are package-inspected; the reimported table
      keeps name, range, style, header and column names.
- [x] `tablesFollowStructuralEdits`: row insert above/inside moves and
      grows a table; removing its span drops it.
- [x] `sampleTablesImportAndBand` (QQO_SAMPLE_XLSX): six tables import by
      name; the Orders header paints dark and a stripe row differs from
      its unbanded neighbour on screen.
- [x] `chartWindowsRender` (QQO_SAMPLE_XLSX): a chart window paints real
      series pixels (several distinct colours in the plot area) — the
      QtCharts ChartView path segfaulted under QGuiApplication before the
      custom ChartItem existed.
- [ ] On the desktop: insert a chart through the dialog (its preview is
      the same ChartItem) and open the sample — four chart windows render
      live data and stay put on edits.

### M35 gates (chart import + StackedBar export)

Automated by `tst_calc_io_charts`, `tst_calc_structure` and
`tst_calc_window`. Rationale in D-035.

- [x] `chartImportReadsKindsAndGrouping`: a StackedBar chart part reads
      back as StackedBar with title, anchor and both series' cat/val refs.
- [x] `stackedBarExportWritesGrouping`: the rangeText path's package
      carries `<c:grouping val="stacked"/>` (post-pass).
- [x] `chartsSurviveRoundTrip`: a series-authored chart generates its
      part + drawing anchor (content-types, rels, `<drawing r:id>`
      inspected) and reimports with kind, title and series intact.
- [x] `chartSeriesFollowTheEdit`: row insert moves series refs and the
      anchor; removing the data band drops the series and then the chart.
- [x] `sampleChartsImport` (QQO_SAMPLE_XLSX): four charts land — two on
      Dashboard, two on Forecast — with titles and six series; the host
      exposes all four for the chart windows.
- [ ] On the desktop: the Dashboard and Forecast chart windows render
      their series live (ChartView over the imported refs).

### M34 gates (rules & annotations round-trip)

Automated by `tst_calc_io_rules`, `tst_calc_rules` and `tst_calc_window`.
Rationale in D-034.

- [x] `condFormatsRoundTrip`: a cellIs rule with a dxf survives
      (package-inspected dxf fill as `bgColor`), evaluates after reimport,
      and keeps stopIfTrue.
- [x] `expressionAndDataBarSurvive`: the formula text round-trips, reparses
      and evaluates shifted; the bar colour and live fraction survive.
- [x] `validationsRoundTrip`: list (with message) and whole-number rules
      enforce after the round trip; unmappable operators/styles are
      skipped with a count, never approximated.
- [x] `autoFilterRoundTrip`: the element lands after `</sheetData>`,
      re-imports active with its allowed values, and hides the same rows.
- [x] `commentsRoundTrip`: the comments part, rels and content-type
      override exist; author/text return at the right cells.
- [x] Engine: expression rules shift per cell, data bars scale over the
      range, stopIfTrue truncates, merge is first-wins-per-field (the M21
      later-wins test flipped, D-034).
- [x] `sampleRulesEnforceAndImport` (QQO_SAMPLE_XLSX): 8 conditional
      formats and 9 validations arrive; Controls!B5 rejects "Bogus" with a
      reason and accepts "Upside"; the Monthly data bar measures live.
- [ ] On the desktop: data bars paint on Monthly/Product Analysis/
      Customer Analysis; the "REORDER"/"FOLLOW UP" rows colour; a comment
      hover shows the imported text.

### M33 gates (full style round-trip)

Automated by `tst_calc_io_styles` and `tst_calc_window`. Rationale in D-033.

- [x] `fullStyleRoundTripsOnValueFormulaAndEmptyCells`: every CellStyle
      field (custom number format, bold/italic/underline/strikeout, font
      size/family, fg/bg, H+V alignment, wrap, border mask) survives on
      all three cell kinds; the package carries the fill as `fgColor`
      (the colour external consumers paint).
- [x] `customNumberFormatsRoundTrip`: the sample workbook's own codes
      (`0.0%;[Red](0.0%);–`, `$#,##0.00;[Red]($#,##0.00);–`) serialize.
- [x] `themeColorsResolveToRgb`: a `theme="4" tint="-0.25"` colour
      resolves through the package's own palette.
- [x] `tst_calc_window::sampleDashboardKeepsItsDarkTitle` is GREEN (was
      red by design at M32): the sample's navy banner fill imports.
- [x] `recoverySnapshotRoundTripsTheWholeWorkbook` covers strikeout and
      vAlign; old payloads decode vAlign as centre.
- [x] `largeSheetStaysResponsive` stays green with workbook-wide style
      dedup (`formatKey()`) and the hoisted format cache.
- [ ] On the desktop: the sample workbook shows fills, wraps and banner
      colours on every sheet (visual pass over the M32 state).

### M32 gates (sheet tabs render; worker-thread loads)
Automated by `tst_calc_window` (QindaOffice) and QindaTK's
`qindatk_qml::ControlsStructure`. Rationale in D-032.

- [x] QindaTK `test_tab_strip_accepts_cpp_sequence_model`: a C++
      `QStringList` sequence (`Qt.application.arguments`) yields tab
      delegates — fails without the QindaTK 7fdc60c fix, passes with it;
      QindaTK `ctest --preset dev` stays 14/14.
- [x] `tst_calc_window::allTabsReachableAtSmallWidths`: 13 long-named
      sheets at 900 px — the strip overflows, and every tab scrolls into
      the viewport when activated.
- [x] `tst_calc_window::windowResponsiveAfterSampleLoad`
      (`QQO_SAMPLE_XLSX`): the window retitles, shows one tab per sheet
      (11), and paints (grab pixel check) after an awaited worker-thread
      load; wall time logged.
- [x] `tst_calc_window::opensARealExcelWorkbook` counts error cells by
      `isErr` (the literal text cell `#` is not an error) and pins the 12
      known array-arithmetic `#VALUE!` cells until M38 tightens it to 0.
- [x] `tst_calc_window::sampleDashboardKeepsItsDarkTitle`: red by design
      (QEXPECT_FAIL) until M33 lands cell fills — Dashboard!A1:P2 is a
      navy merged banner whose fill the importer drops today.
- [x] The structural fixtures `qindacalc_{1280,520}.txt` record
      `QQuickTabButton#tab_0` — the first fixture with a visible tab.
- [ ] On the QindaQt desktop with the packaged QindaTK: the installed
      `qindacalc` opens the sample workbook with all eleven tabs shown
      and responsive while loading (needs the `dev-libs/qindatk` rebuild
      carrying 7fdc60c).

### M31 gates (launcher, Widgets removal, packaging, rules)

- [x] `tests/qindalauncher/tst_launcher`: the catalog (four distinct apps
      with role and blurb, which app owns a suffix, the four recent lists
      merged newest-first with missing files skipped and an unparsable list
      survived without being rewritten), the controller's published apps
      and recents, and the real `LauncherWindow.qml` — a card per app whose
      accessible name a screen reader can read, the recent row present, the
      empty state hidden, the Open button there. `QQO_GRAB_DIR` saves the
      window (reviewed: four mascot cards, the recent document, the Open
      button).
- [x] `ctest --preset dev` green (45 tests) with no Qt Widgets UI in the
      suite: no `Qt6::Widgets` link outside `third_party`, and
      `grep -rn QApplication apps libs tests` is empty. `libQt6Widgets.so`
      is still loaded because QtPrintSupport links it (D-031).
- [x] `libs/office_shell` and `tests/office_shell` deleted; the
      `QQO_WIDGETS_APPS` option, the `QQO_WIDGETS_AI` define, the Widgets
      assistant panel and the Widgets proposal dialog gone with them.
- [x] `AGENTS.md` rewritten for the QtQuick suite (rules 3, 4, 8 restated;
      the QML rules and the headless-first verification section added).
- [x] The INSTALLED binaries run, not just the build tree: every executable
      over `office_quick` carries an RPATH to the QindaQt.Tokens backing
      library (`$ORIGIN`-relative first, as the desktop's own QML apps are
      linked, then the absolute directory). `objdump -x /usr/bin/qindacalc |
      grep RUNPATH` must name it, and `<prefix>/bin/<app> --version` must
      answer instead of dying on a missing `libqindaqt_tokens_qml.so`.
- [ ] Packaging (operator, needs root and a clean tree):
      `tools/make-dist.sh 0.1.0_p<date>-r5 /var/cache/distfiles`, then
      `ebuild qindaoffice-0.1.0_p<date>-r5.ebuild manifest` in the overlay
      and `emerge -1 dev-libs/qindatk gui-apps/qindaoffice`; all five
      binaries (`qindawrite`, `qindacalc`, `qindashow`, `qindanote`,
      `qindaoffice`), their desktop files and the QindaTK QML module
      installed.
- [ ] On the QindaQt desktop, the whole suite: the panel menu drives each
      app end to end; portal file dialogs open; the tablet inks in
      QindaNote; the QindaShow player takes the second screen; light and
      dark themes and comfortable density all read correctly.

### M30 gates (QindaShow on QindaTK)

- [x] `tests/qindashow`: `tst_qindashow` (fodp round trip, pptx through
      LibreOffice when present, the canvas item mutating the model with
      its undo payloads and topmost-first hit-testing, the host wiring
      deck / notes / transition / undo and reopening a saved deck, the
      controller revealing builds before slides and reporting the build
      duration, outline parsing, deck PDF, the text bridge),
      `tst_show_ui` (named strips and menus, styling undoable, the shape
      clipboard, insert commands, edit commands following the selection,
      find across titles / shapes / notes with case sensitivity, the
      canvas painting the page from a window grab, align against the
      slide, bring-to-front in one step, move slide, Replace sweeping the
      deck in one step, a sorter thumbnail per slide, the player window
      advancing through the shared controller and the editor landing on
      the slide the show ended on, pristine host, structural fixtures at
      1280 and 520), `tst_show_design` (masters paint and supply default
      type only, a build animation changes what is drawn mid-reveal,
      snapping pulls an edge to the slide centre and the toggle follows
      the canvas, grouping survives the fodp round trip, a multi-shape
      gesture is one undo step, masters round-trip through the dialog
      seam, animation entries follow the selection), `tst_show_menu_export`
      (validator accepts the ten menus; every command resolves; one AI
      menu with the inline actions, deck generation and the seven
      animation entries).
- [x] `QQO_GRAB_DIR=<dir> tst_show_ui` saves 1280×800 and 520×700 PNGs;
      reviewed before the commit (sorter thumbnail, page centred on the
      canvas, both strips, notes panel, "Slide 1 of 1   Shapes: 1").
- [ ] On the QindaQt desktop: the panel menu drives QindaShow end to end;
      F5 opens the player on this screen and the presenter view on the
      second one; Escape ends both.

### M29 gates (QindaWrite on QindaTK)

- [x] `tests/qindawrite`: `tst_write_control` (the editing engine on its
      own: keys, mouse hit and drag, Shift-select, clipboard round trip,
      undo, input-method preedit/commit, pinned format), `tst_write_view`
      (3-page document paginates identically on screen and in the PDF
      through Poppler; typing through the window reaches the document;
      zoom is the viewport's transform; styles survive HTML; accessible
      text over the item; header/footer in the PDF), `tst_write_ui`
      (named strips, menu tree, char/paragraph formatting through the
      entries, font combos, tables and columns, find/replace with wrap and
      one-step undo, edit commands follow the control, paper palette
      pinned and white in a window grab, zoom commands, table operations,
      page setup round trip, the 29-command strip wraps at 560 px, pristine
      host, structural fixtures at 1280 and 520), `tst_write_typography`,
      `tst_write_review` (footnotes, TOC, tracked changes through the
      window's key path, ruler markers through the host and `Tk.Ruler`,
      footnote dialog round trip), `tst_write_menu_export` (validator
      accepts the twelve menus; every command resolves; one AI menu with
      the inline actions), `tst_write_formats` and the pagination probe on
      `QGuiApplication`.
- [x] `QQO_GRAB_DIR=<dir> tst_write_ui` saves 1280×800 and 520×700 PNGs;
      reviewed before the commit (ruler numbered from the text column,
      markers on the margins, paper white, strip wrapped in three rows).
- [x] QindaTK `tst_qml`: `test_ruler_markers_drag_and_tabs`.
- [ ] On the QindaQt desktop: the panel menu drives QindaWrite end to end;
      Wayland text-input composes into the page (preedit visible).

### M28 gates (QindaCalc on QindaTK)

- [x] `tests/qindacalc`: `tst_calc_window` (document, host, recovery, ods
      bridge, pristine host; `QQO_SAMPLE_XLSX=<path>` runs the real-workbook
      diagnostic), `tst_calc_ui` (named strips, undo/redo recalculates,
      clipboard, formatting through the entries, find/replace with wrap,
      the cursor outline on the cursor cell from a window grab, the strip
      wraps at 560 px, D-015 grid commands, sheets, freeze panes,
      structural fixtures at 1280 and 520), `tst_calc_rules_ui`
      (validation through the dialog seam, conditional formatting as one
      undo step, autofilter hides rows, comments survive undo and
      recovery), `tst_calc_menu_export` (validator accepts the ten menus;
      every command resolves; the template ids are in the tree).
- [x] `tests/calc_io`: `absolutePartNamesStillOpen` (a package rewritten
      to absolute targets opens with its styles); `tests/calc_engine`
      fixture rows for `SUMIFS`, `RANK.EQ`, `FORECAST.LINEAR`, `SUBTOTAL`,
      `LET`.
- [x] `Northstar_Excel_Demo.xlsx` (11 sheets, 95 k cells, 10 k formulas)
      opens in ~10 s with 12 `#VALUE!` cells (array arithmetic in
      `SUMPRODUCT`, D-028 limitation); it did not open before.
- [ ] On the QindaQt desktop: the panel menu drives QindaCalc end to end;
      a wheel over the grid scrolls, Ctrl+wheel zooms.

### M27 gates (QindaTK conversion, foundation + QindaNote)

Build with the QindaTK checkout on the prefix path:

```
cmake --preset dev -DCMAKE_PREFIX_PATH=<QindaTK>/install
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

QtQuick tests run under `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`
(pinned via ctest `ENVIRONMENT`).

- [x] `tests/office_quick`: `tst_action_model` (entries, radio groups,
      trigger by id), `tst_menu_export` (the desktop's validator accepts the
      exported tree; every exported command resolves and fires in the
      model), `tst_document_host` (async open/save/close flows), `tst_office_window`
      (the real `OfficeWindow.qml` over a host: bands, menu bar from the
      tree, tool strip buttons follow their entries, View ▸ Show Menu Bar),
      `tst_print_service` (PDF from the render callback, Poppler preview of
      the same PDF, printer model).
- [x] `tests/qindanote`: `tst_qindanote` (engine + host: reopen, lazy pages
      never blanked, recovery, structure commands through the host, find
      across unopened pages, replace all), `tst_note_blocks` (layout,
      placed blocks, checklist state and nesting, merged typing undo, image
      bytes, tap-to-type), `tst_note_input` (pen through
      `QWindowSystemInterface`: inks with pressure in document units, a
      finger inks only with Draw with Finger on, two fingers zoom with no
      ink, a Text-tool tap opens a block, undo/redo, live eraser as one undo step, barrel-button
      eraser, zoom is a view transform, tool shortcuts lose to a text
      field, paper white under the dark preset, debounced page writes),
      `tst_note_ui` (icon-only addressable strip, slots follow the tool,
      wrap instead of hide, navigator opens pages, assistant panel follows
      AI ▸ Assistant…, structural fixtures at 1280 and 520),
      `tst_note_menu_export` (hard-asserts V5).
- [x] `tests/office_ai`: `assistantControllerSendsAndStreams` over the mock
      SSE server (no Widgets).
- [x] The three Widgets apps still build and pass behind `QQO_WIDGETS_APPS=ON`.
- [x] **Verified 2026-09-17 on the live QindaQt session (desktop host):**
      QindaNote from the build tree exports
      `/org/qindaqt/QindaOffice/QindaNote/Menu/1` on the session bus with
      all 8 menus (98 items, radio states correct); `busctl --user call
      org.qindaqt.QindaNote <path> com.canonical.dbusmenu Event isvu -- <id>
      clicked s "" 0` toggles Show Navigator (toggle-state 1→0), New Page
      enables Move Page Up, and Quit on a pristine window exits the process
      (V5 fixed end to end). The desktop reports `appearance.theme =
      qinda-dusk`; `DesktopAppearance` publishes it (log line
      "QindaQt tokens applied").
- [ ] Manual, needs a tablet at the desk: pen with pressure and tilt, palm
      ignored, barrel button erases; paper white under the dark theme
      (offscreen: `NoteInputTest::pageIsPaperUnderTheDarkPreset`).

Fixtures: `tests/qml_fixtures/qindanote_{1280,520}.txt` are structural
dumps (named items and visibility, no text or geometry);
`QQO_UPDATE_FIXTURES=1 ctest -R tst_note_ui` rewrites them after an
intentional chrome change.

### M21 the next tier, and the suite's launcher

Automated by `tst_calc_rules`, `tst_calc_rules_ui`, `tst_write_review`,
`tst_show_design`, `tst_note_blocks`, `tst_launcher` and the pristine-window
cases now in all four app suites. Rationale in D-018b (open reuse), D-019
(Calc rules), D-020 (Write document features), D-021 (Show design layer),
D-022 (Note blocks) and D-023 (the launcher).

**Opening.** A freshly created window of every app is pristine, so File ▸
Open replaces its blank document instead of opening beside it; the window
the Open came FROM is preferred; a window that has been typed into is never
displaced.

**QindaCalc.** A blank cell satisfies no comparison and an empty input is
never refused. Two conditional formats compose field by field. A refused
value emits `inputRejected` rather than vanishing. A filtered row leaves
the grid entirely (the row below moves up into its place) and the header
row is never hidden. Comments survive undo AND the recovery payload. All
four rule kinds follow an inserted row.

**QindaWrite.** Footnote markers number by document order and renumber when
one is inserted in the middle; a note prints on its marker's page; an
orphaned note is pruned. The contents lists headings with the page numbers
the view paints, never lists itself, and Update replaces the table in
place. A tracked insertion is rejectable and a tracked deletion is
acceptable, exactly inverse. The ruler's markers describe the cursor's
paragraph. Enter after a heading starts body text, and a Heading 1 at the
very start does not push a blank page ahead of itself.

**QindaShow.** A master really paints the background and supplies defaults
only to runs that never chose. A half-played animation draws something that
is neither the background nor the final fill. Snapping takes a candidate
inside the threshold and nothing outside it, and does nothing when off. A
group survives the fodp round trip. A multi-shape gesture is one undo step.

**QindaNote.** Three kinds of block stack without overlapping, each as tall
as its kind needs. A checklist item's text and tick reach the envelope. An
image block carries its bytes, not a path.

**Launcher.** Four distinct apps with distinct binaries and desktop ids;
every suffix routes to the app that opens it and a suffix-less path to
QindaNote; recents merge the four lists, skip files that are gone, and
survive a `recent.json` that is not JSON without rewriting it; each card
carries an accessible name and description.

### M21 packaging gate (Gentoo)

The ebuild is the only supported install path, and it must build with no
network access at all — Portage's default `FEATURES` include
`network-sandbox`. Verify before tagging a snapshot:

- [ ] `tools/make-dist.sh <version> <distdir>` exits non-zero on a dirty
      tree. A tarball that matches no commit is a tarball nobody can
      reproduce.
- [ ] The tarball contains `third_party/QXlsx/QXlsx/CMakeLists.txt`. The
      submodule must be VENDORED, not referenced: `src_unpack` cannot fetch
      it under `network-sandbox`.
- [ ] `ebuild <ebuild> clean unpack` leaves the tree at `${WORKDIR}/${P}`
      and its `.dist-commit` names the commit the archive was cut from.
- [ ] `emerge -1 gui-apps/qindaoffice` runs with **no** `MAKEOPTS` or `-j`
      override; Portage's configured job and load limits are the limits.
- [ ] After merge, all five binaries exist (`qindawrite`, `qindacalc`,
      `qindashow`, `qindanote`, `qindaoffice`), and the five desktop files
      and the hicolor SVGs are installed.
- [ ] `packaging/gentoo/gui-apps/qindaoffice/` and the `qindaqt` overlay
      hold the SAME ebuild. The in-repo copy is the source; a drift means
      the overlay is packaging something the tree does not describe.
- [ ] The install image contains **no** `.a`, `.so*`, `.h` or `.cmake`
      files (D-025). Check against an eclass-like configure, not the dev
      preset — `cmake.eclass` forces `BUILD_SHARED_LIBS=ON`, so a build
      that is static locally can still ship a shared library under
      Portage:

      ```
      cmake -S . -B /tmp/ic -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=ON \
            -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib64
      cmake --build /tmp/ic && DESTDIR=/tmp/img cmake --install /tmp/ic
      find /tmp/img \( -name '*.a' -o -name '*.so*' -o -name '*.h' \
                     -o -name '*.cmake' \) | wc -l     # must be 0
      ```
- [ ] `ldd` on the installed `qindacalc` names no `libQXlsxQt6`: the
      vendored copy must be linked in, not planted in `/usr/lib64`.
- [ ] Two machines cutting the SAME commit produce byte-identical
      archives. The Manifest pins one hash and `pkg_nofetch` tells the
      operator to run `make-dist.sh`, so a non-reproducible tarball means
      the second machine's own archive is rejected as a Manifest
      mismatch:

      ```
      sha512sum <distdir>/qindaoffice-<pvr>.tar.xz          # on each host
      grep SHA512 .../gui-apps/qindaoffice/Manifest
      ```

      A plain `tar -cJf` does NOT satisfy this — readdir order, the
      creation times of `.dist-commit` and the staging directories, the
      embedded uid/gid, and multi-threaded xz all vary per machine.

### M21 manual checklist (live desktop)

- [ ] `gtk-launch org.qindaqt.QindaOffice` opens the launcher with four
      mascots; clicking a card starts that app, and closing the launcher
      leaves it running.
- [ ] QindaCalc: colour a range by rule, put a list validation on a column
      and try to type something else, filter a header row, hover a
      commented cell.
- [ ] QindaWrite: insert two footnotes and check the numbers on the page
      and in the PDF; build a contents list and re-paginate; record a few
      changes and accept half of them; drag the ruler's indent markers.
- [ ] QindaShow: give the deck a master, animate a bullet and play it,
      drag a shape near the slide centre and watch the guide, group two
      shapes and move them.
- [ ] QindaNote: add a checklist and an image, then print — the page prints
      what the screen showed, in the same places.

### M20 office-suite completeness

Automated by `tst_preferences`, `tst_calc_structure`, `tst_calc_ui`,
`tst_write_ui`, `tst_write_typography`, `tst_show_ui` and `tst_qindanote`. Rationale in D-014
(settings, status bar, dead entries), D-015 (QindaCalc structure), D-016
(QindaShow canvas and arrange), D-017 (QindaNote structure and layout) and
D-018 (QindaWrite typography).

**Shell.** Settings ▸ Preferences… is enabled and opens a dialog whose OK
writes through and whose Cancel does not; the `Preferences` store bounds
what it reads back and survives a reopen. Every window has a status bar from
construction with a PERMANENT right-hand field a transient `showMessage()`
cannot displace. View ▸ Show Toolbars / Show Status Bar apply and persist.
The File menu ends with Quit however many app entries were inserted.
Auto-save is off by default and, when on, skips (with a status message)
rather than putting a dialog on screen for a document with no path or a file
changed on disk.

**calc_engine.** Inserting rows moves cells AND rewrites every formula that
points at them — including cross-sheet ones that name the edited sheet — and
a straddling range grows with the insert; deleting turns a reference into
the gap into `#REF!` while a straddling range shrinks; styles on EMPTY
cells, explicit geometry, merges, named ranges and chart anchors all follow.
Renaming a sheet requalifies every reference to it; deleting one breaks only
its own references and leaves the rest working after the index rebuild; the
last sheet cannot be deleted. Sort moves rows with their formulas (relative
references shift by the row delta), keeps the header row and sinks blanks in
both directions. `recalcAll()` refreshes cells off the diagonal (it used to
read the packed key transposed and skip them).

**QindaCalc.** The grid starts on A1, not on a default-constructed
`{-1,-1}`; the status field reads the address, Sum, Average and Count of the
selection. Delete clears the selection in one undo step. A header click
selects the whole row or column bounded by the used range. Insert/Delete
Rows are one undo step each and the dependents re-evaluate. Sort Ascending
keeps a text header on top. AutoSum totals the contiguous run above the
cursor. Merge works from the selection and Unmerge from any cell the merge
covers. A sheet rename keeps cross-sheet formulas working. With a pane
frozen, row 1 stays visible after scrolling to the bottom and still
hit-tests as row 1 — painting and clicking walk the same band lists.

**QindaWrite.** The font-size combo is never blank. Superscript reaches the
selection's char format; line spacing writes `ProportionalHeight` and the
menu's check mark re-reads it from the document. The word count follows the
document and narrows to the selection, and appears in the status field.
Insert Date puts text in and Insert Horizontal Rule leaves a block carrying
`BlockTrailingHorizontalRulerWidth` (so it survives the HTML round trip).

**QindaShow.** The canvas paints a white page (an empty slide used to render
as a flat dark-grey view). Align Centre with one shape selected centres it
on the SLIDE and is one undo step. Bring to Front is one command whatever
the distance. Move Slide Up reorders the deck and follows the selection.
A deck-wide Replace is one undo step however many slides it touched.

**QindaNote.** A fresh window opens on a real, writable page and says so in
the status field. The page view no longer forces the window tall — a 900x500
window stays 900x500. Deleting a page removes its file as well as its index
entry. Move Page reorders within the section and refuses past either end.

### M19 the rest of the standard commands

Automated by `tst_calc_ui`, `tst_show_ui` and `tst_qindanote`. Print,
Print Preview, View ▸ Zoom and Edit ▸ Find/Replace were created disabled by
`createStandardActions()` and wired only in QindaWrite, so in the other three
apps they were menu entries the app did not implement.

- QindaShow prints through the same pure `renderSlide` the PDF export uses;
  QindaNote through `note_page_render.cpp`, a page renderer with no widget or
  palette in it, covering the whole notebook (pages never opened are read
  from disk first); QindaCalc paginates its used range by rows, never
  splitting a row across pages.
- Zoom works in all four and is presentation state only — it never touches
  stored column widths, row heights or the slide model, so it cannot dirty a
  document. QindaCalc's grid geometry and hit-testing both go through
  `colWidthPx()`/`rowHeightPx()`, which is what keeps painting and clicking
  agreeing at any zoom.
- QindaCalc's find bar gained Replace and Replace All. Replace edits the
  cell INPUT, never the formatted display text, and a whole Replace All
  sweep is ONE undo step however many cells it touched.
- QindaShow's find searches slide titles, shape text and speaker notes and
  selects the matching slide. QindaNote's searches page titles and typed
  text blocks, reading pages that were never opened first — a search that
  skipped them would report "not found" for text that is really there.

### M18 toolbar reachability

Automated by `tst_calc_ui` and `tst_write_ui`. Rationale in D-013: a command
behind an extension chevron is a command the user never sees.

- Squeezing the window to a third of its width grows the format strip a row
  instead of dropping commands; every strip command still has a visible
  button inside the strip (QindaCalc: 8 commands; QindaWrite: 16+).
- Every strip command carries a non-empty objectName.
- The cursor outline is painted on the cursor cell, not one header offset
  down and to the right of it.

### M20 manual checklist (live desktop)

- [ ] Settings ▸ Preferences…: set a document font and a default zoom; a
      newly opened window of the same app uses both. Turn off Show Toolbars
      and open a second window — its toolbars are off too.
- [ ] Turn auto-save on at 1 minute, edit a saved document and wait: the
      status bar reports the auto-save and the window title loses its dirty
      mark, with no dialog at any point.
- [ ] QindaCalc: right-click the grid and the sheet tab; drag a column
      boundary and double-click it; freeze at B2 and scroll — the first row
      and column stay put; select a numeric block and read Sum/Average/Count.
- [ ] QindaShow: the slide is a white page centred in the canvas at any
      window size, and the current slide's thumbnail shows the slide (not a
      blue rectangle).
- [ ] QindaNote: shrink the window to half the screen height — the page
      scrolls instead of the window refusing to shrink.
- [ ] Panel global menu: activate the new commands by objectName (Insert
      Rows, Sort Ascending, Bring to Front, New Section, Word Count).

### M16 manual checklist (live desktop)

- [ ] `Ctrl+S` saves in each of the four apps; `Ctrl+W` closes the window
      and `Ctrl+Q` closes the current window only.
- [ ] Panel global menu: activate *New from Template*, *Export PDF*,
      *Bring forward* and the AI inline actions from the panel — each
      resolves by objectName instead of being inert.
- [ ] Launcher and task switcher show the suite icons (the hicolor SVGs are
      installed by `make install` / the ebuild) — four mascots, one family.
- [ ] QindaCalc: type a name into the Name Box with a range selected — it
      defines the name; typing it again jumps back to that range; Ctrl+Z
      removes the definition.
- [ ] QindaNote: open a multi-page notebook, view one page, Ctrl+S, reopen —
      the pages you never opened still have their ink.
- [ ] Formatting strips show ICONS, not words: the suite's own
      hicolor/scalable/actions set resolves under the QindaQt icon theme
      (which inherits hicolor and carries none of the format family).
- [ ] Narrow any app window until the strip wraps — no "»" chevron appears
      and no command disappears.
- [ ] QindaCalc with a chart open: type into cells — the chart re-renders
      in place; its window does not disappear, move or jump to the front.
