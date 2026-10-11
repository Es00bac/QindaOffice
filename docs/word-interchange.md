<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Word interchange (QindaWrite, `office_docx`)

M60 / D-068. QindaWrite opens and saves Microsoft Word documents natively —
no LibreOffice, no HTML in between — and a saved file opens in Word with the
document's own styles, numbering, theme and everything Write does not edit.
Long documents retain their full page stack while the screen paints only a
bounded visible tile (D-109); this does not change the saved Word file.

## Formats

| Format | Open | Save |
| --- | --- | --- |
| `.docx` Word document | native (`office_docx`) | native — **the default Save format** |
| `.dotx` Word template | native | native (template content type) |
| `.docm` / `.dotm` macro-enabled | native | native; the source's VBA project is carried, never run |
| `.doc` Word 97–2003, `.rtf`, `.odt`, `.ott`, `.fodt`, `.wpd`, `.wps` | LibreOffice → `.docx` → native reader | native `.docx` → LibreOffice (`.doc`, `.rtf`, `.odt`) |
| `.html` | QTextDocument HTML + `qindawrite-*` metas | HTML with pictures embedded as `data:` URIs |
| `.md`, `.txt` | as before | as before (pictures dropped) |

LibreOffice conversions run on a private copy in a temporary directory, so
nothing is written beside the user's file and read-only folders work.

Crash recovery journals the document as `.docx` bytes with its pictures, up
to the journal's 16 MiB binary bound; a larger document is not journalled
and the status bar says "Too large for crash recovery … save often".

## The model

The body is a `QTextDocument`; `Docx::WordParts` (`docx_model.h`) holds the
rest and `WriteDocument` owns both:

- **Sections** (M88, D-096; `docx_sections.h`, below) — every `w:sectPr`:
  page size and orientation, four margins, gutter, header and footer
  distances, Different First Page (`w:titlePg`), page numbering
  (`w:pgNumType`: restart, format, chapter style), columns (`w:cols`), how
  the section starts (`w:type`), vertical alignment; what is not modelled
  (`w:lnNumType`, `w:docGrid`, page borders, paper source) stays in its
  markup. Layout ▸ Page Setup edits a section's page (Letter, Legal,
  Tabloid, Executive, A3–A5, B5, #10 envelope or custom; inches or cm)
  with Word's "Apply to": this section, this point forward, whole document.
- **Headers and footers** — rich `QTextDocument`s per slot (default, first
  page, even pages) and per section, with pictures and PAGE / NUMPAGES /
  SECTIONPAGES fields. A slot a section does not have is linked to the
  previous section's (Word's Link to Previous). An untouched header is
  copied back byte for byte. They are edited in place as in Word
  (`page_story_editor.h`): double-click the top or bottom margin band, or
  Insert ▸ Edit Header / Edit Footer; the page's own section and slot open
  (first page, even page) — a linked one edits the previous section's, as
  the band's "Same as Previous" says — every formatting command, Undo and
  the clipboard work on it, Page Number and Number of Pages insert fields,
  and Esc, Close Header and Footer or a double-click on the text return.
  The Page Setup text fields edit the caret's section's default slot as a
  plain pattern (`{page}` / `{pages}`) and warn when that would replace a
  richer header.
- **Footnotes and endnotes** — Write's markers and note texts; an unedited
  note keeps Word's own markup, and an endnotes part whose notes are all
  still referenced and unchanged is copied byte for byte (M77, below).
  Each kind's options (number format, start, restart, place) are
  `WordParts::footnoteOptions` / `endnoteOptions`.
- **Mail merge** — `WordParts::mailMerge` (`docx_mailmerge.h`): Word's
  `w:mailMerge` as the merge type, data source, filters and sorts, e-mail
  settings and Match Fields; kept as read while unchanged (M77, below).
- **Floating pictures** — a picture's wrap and position (`kPictureFloat`,
  `docx_float.h`), shown with Qt's floating frames or drawn over and under
  the text (M77, below).
- **Comments** — `WordParts::comments` (`docx_comments.h`): author,
  initials, date, text, the comment a reply answers and the resolved state
  (Word's `commentsExtended`). In the text a comment is its range (each run
  carries the comment's id, `kCommentIds`) and its reference mark, a
  zero-width character (`kCommentReference`). Review ▸ Comments
  (`write_comments.h`, the Comments pane) lists the body's comments in text
  order with their replies: New Comment (Ctrl+Alt+M) on the selection,
  Reply, Edit, Resolve / Reopen, Delete (the thread and its replies; the
  text stays), Previous / Next; a click on a card selects its text, and
  commented text is shaded (darker for the active comment; display only).
  New comments carry the track-changes author (the login name). While no
  comment changed, the source's comment parts are copied byte for byte;
  otherwise `comments.xml` and `commentsExtended.xml` are written anew —
  unchanged comments from their own markup, comments marked only in a
  header, a note or a kept text box kept as they were — and Word's
  `commentsIds` / `commentsExtensible` caches are left out (Word rebuilds
  them).
- **Pictures** — the original bytes (`WordParts::media`, resource names
  `qwmedia:…`) travel with the document: Insert ▸ Image stores the file's
  bytes, not its path. A click selects a picture and shows Word's eight
  handles (`page_picture_tool.h`): a corner scales it in proportion (Shift:
  freely), a side stretches it, each drag one undo step; Format ▸ Picture
  (or a double-click) sets its size, locks or frees the aspect ratio,
  resets it to the picture's own size, and edits its alt text
  (`wp:docPr/@descr`). A Word picture keeps its own drawing when resized —
  crop, effects, name and anchoring — with only its extents patched.
- **Content controls (forms)** — Word's `w:sdt` (`docx_controls.h`): check
  boxes, drop-down lists, combo boxes, dates, plain and rich text, and the
  gallery wrappers Word puts around a table of contents or a cover page.
  The content stays ordinary text; its runs carry the control's key
  (`kContentControls`), or its paragraphs and tables do for a block-level
  control (`kBlockControls`), and `WordParts::contentControls` keeps each
  control's own `w:sdtPr`. A click fills in a form as in Word
  (`write_controls.h`, `FormFieldPopup.qml`): on a check box's symbol it
  ticks or clears it, on a list it opens the items, on a date a calendar
  (written in the control's `w:dateFormat`), and on a placeholder it
  selects the placeholder so typing replaces it, in ordinary ink. On save
  each control is written back from its own markup, patched for what
  changed: `w14:checked` follows the symbol in its text, `w:lastValue` the
  chosen item, `w:fullDate` the date; edited content drops
  `w:showingPlcHdr` and `w:dataBinding` (Word would otherwise refill a
  bound control from its XML on open and undo the edit). A control copied
  within the document is written again without its `w:id`.
- **Lists and styles** — Word's own model (M73, D-082; below): every list
  paragraph's list format carries its `w:abstractNum`, `w:num`, level and
  start overrides; the styles part is `WordParts::styles` once a style
  command changed it.
- **The source package** — what Write does not model is carried from it on
  save: styles (until a style command changes them), numbering
  definitions (every untouched list's, byte for byte), theme, settings,
  fonts, the endnotes part while its notes are unchanged, custom XML,
  glossary, people, VBA (macro kinds only), and the comment parts while no
  comment changed.

## Sections (M88, D-096)

Word's sections, under Word's names, on Word's model:

- **Layout ▸ Breaks ▸ Section Break** (Next Page, Continuous, Even Page,
  Odd Page; also Page ▸ Breaks): the paragraph splits at the caret and the
  part before it becomes a section of its own — the section's settings
  and markup, and its headers and footers; the part after starts at the
  break (its page numbers continue) and links its headers to them ("Same
  as Previous"). Backspace at the start of a section, or Delete before its
  break, takes the break away as in Word: the section joins the next one
  and takes its settings, and the headers the next one showed through its
  links go to it; joining two paragraphs keeps the second one's break.
- **Layout ▸ Orientation** (Portrait, Landscape; Page ▸ Orientation) turns
  the caret's section's paper and its margins with it; **Page Setup** acts
  on this section, this point forward (a next-page break at the caret
  first) or the whole document, and sets how the section starts.
- **Header & Footer** (Insert, and Word's contextual tab while one is
  open): **Link to Previous** for the open slot of a section (off copies
  what it showed into a story of its own; on drops it), **Different First
  Page** per section, **Different Odd & Even Pages** (the document's, as
  in Word), **Format Page Numbers…** (1, 2, 3 / - 1 - / a, b, c / A, B, C /
  i, ii, iii / I, II, III; continue from the previous section or start at
  a number). PAGE shows the page's number in its section's format (a
  field's own `\* roman` wins), NUMPAGES the document's pages,
  SECTIONPAGES the section's.
- **Each page is its section's** (`write_sections.h`): its size and
  orientation, margins, header and footer, number, on screen (pages of
  different sizes stacked and centred, as Word's Print Layout), on paper
  and in PDF (each page's size set before the next page starts). The
  ruler follows the caret's section.

How it is laid out: Qt paginates a document with one page size, so the
body is laid out on a canvas whose text column is the widest section's
and whose text area is the tallest's (its margins the last section's, so
a one-section document lays out as before). A narrower section's
paragraphs and tables carry a display-only right-margin pad
(`kSectionPad`), so their lines wrap at its own width; in a shorter
section the pagination pass breaks the page where its own text area ends
— before a paragraph, or between two table rows (Qt repeats the header
rows) — and a section that starts on a new page gets a page break, with
a blank page before an odd or even page start that would land on the
wrong page. Each page shows its section's part of the canvas page it was
laid out on. Child `QTextFrame`s would give a section its width, but a
selection across two of them takes both whole frames
(`tst_write_probe_sections`), so sections are not frames.

Every command is a persistent action (`writeSectionBreakNextPageAction`,
`writeSectionBreakContinuousAction`, `writeSectionBreakEvenPageAction`,
`writeSectionBreakOddPageAction`, `writeOrientationPortraitAction`,
`writeOrientationLandscapeAction`, `writeLinkToPreviousAction`,
`writeDifferentFirstPageAction`, `writeDifferentOddEvenAction`,
`writePageNumberFormatAction`) in the menus (Page ▸ Breaks, Page ▸
Orientation, Insert) and on the ribbon (Layout ▸ Page, Insert ▸ Header &
Footer, the Header & Footer contextual tab), and one undo step.

## Styles in a Word document

Write's own styles (Format ▸ Style, Home ▸ Styles ▸ More styles: Normal,
Title, Heading 1–6, Quote, Code) apply the
document's OWN style of that name when it has one (`docx_restyle.h`): the
paragraph shows the document's template look (style chain, theme colours,
spacing folded as the reader folds it), its runs keep their own direct
formatting from the preserved `w:rPr` (text typed in Write takes the
style's look), and the paragraph is written as `<w:pStyle w:val="…"/>`
plus what is not formatting (numbering, section break, the mark's run
properties) — so Word shows its own style, not Write's built-in look as
direct formatting. A document without that style, or one not from Word,
gets Write's built-in style. A restyled table-cell paragraph is shown
without its table style's layers (Word, which has them, shows them).

## Lists, numbering and styles (M73, D-082)

Word's commands, under Word's names, on Word's model:

- **Bullets** (Home ▸ Paragraph, Format ▸ Bullets): the library ● ○ ■ ◆ ➢
  ✓ – in Word's own fonts (Symbol, Courier New, Wingdings), No Bullets and
  **Define New Bullet** — any symbol (Word's, or any character typed or
  pasted in any installed font, or — M89 — picked from QindaTK's character
  map under Symbol… in the bullet's font), its colour and alignment. The
  Bullets button applies the last bullet picked, or takes the bullets off.
  **Insert ▸ Special Character** is the same character map (Unicode blocks,
  recent characters, the code point; it stays open for the next one).
- **Numbering**: the library 1. 1) I. A. a) a. i., No Numbering and
  **Define New Number Format** — number style (1, I, i, A, a, 1st, One,
  First, 01, none), the text around the number ("Step %1:"; on a level
  that also shows the levels above it, Word's own "%1.%2."), start value
  and alignment. **Restart at 1**, **Continue Numbering** and **Set
  Numbering Value** (start a new list at a value, or continue the list
  before).
- **Multilevel List**: the library 1. 1.1. 1.1.1., 1) a) i), bullet levels
  (➢ ▪ •) and Article I. / Section 1.01 / (a); **Define New Multilevel
  List** — per level the number style or a bullet, the format text with the
  levels it includes, start, the level it restarts after, legal-style
  numbering, the number's alignment and position, the text indent and what
  follows the number (tab, space, nothing); **Change List Level** (levels
  1–9); **Adjust List Indents** (number position, text indent, follow
  number with, a tab stop).
- Keys as in Word: **Tab / Shift+Tab** at an item's start move it down or
  up a level (in the middle of an item they type a tab); Increase / Decrease
  Indent do the same on items; **Enter** on an empty item moves it up a
  level and, at the first level, ends the list; **Backspace** at an item's
  start takes its number away and leaves the text where it was.
- A library pick on items of one list of that kind changes that list (as
  Word's library does); on other paragraphs it makes a new list.
- **Numbers as Word shows them** (`docx_list_labels.h`): computed from the
  paragraphs' list formats in document order — one counter set per
  definition, or per instance when it overrides a start, `w:lvlRestart`,
  `%1.%2.` in each level's format, legal numbering — and drawn at left −
  hanging in the level's font and colour, on screen, paper and PDF and in
  headers; a symbol font's private-use bullet is drawn as its Unicode
  look-alike. Qt's own marker is off for these paragraphs. The text after
  a number starts at the left indent, or at the next tab stop when the
  number is wider than the hanging indent, or after a space or nothing.
- **Saved as Word writes them** (`docx_writer_numbering.cpp`): an untouched
  list keeps its `w:num` and `w:abstractNum`; a restart is a new `w:num`
  over the same `w:abstractNum` with `w:lvlOverride/w:startOverride`;
  Continue Numbering is the same `w:numId` again; a new or changed
  definition is a new `w:abstractNum` (with a `w:nsid`) and `w:num`. A
  paragraph at its level's indents, or numbered by its style, has no
  `w:ind` or `w:numPr` of its own.
- **Styles pane** (Home ▸ Styles ▸ Styles Pane, Alt+Ctrl+Shift+S, also
  View ▸ Panes): the document's paragraph, character, linked and table
  styles and Word's built-ins it lacks, in Word's order; a click applies
  one (a built-in joins the document, as in Word); Show Preview draws the
  names in their styles' fonts; "Only styles in use".
- **The gallery** (Home ▸ Styles): the document's quick styles
  (`w:qFormat`), then Word's built-in quick styles it lacks, in sixteen
  stable slots; Write's own ten styles stay under More styles.
- **New Style** from the selection (paragraph, character or linked; based
  on; style for the following paragraph; add to the gallery), **Modify
  Style** (name, base, next, gallery, the quick font row, and Font…,
  Paragraph…, Tabs…, Borders… — M70's own dialogs — and Numbering), **Update
  Style to Match Selection**, **Delete Style** (the document's own styles;
  its paragraphs fall back to its parent, styles based on it to that
  parent too). A style is written as a real `w:style` — `w:name`,
  `w:basedOn`, `w:next`, `w:link`, `w:qFormat`, its own `w:pPr` / `w:rPr`,
  all else it said kept; a linked style is a paragraph and a character
  style naming each other. Modifying a style re-derives every paragraph
  and run that uses it, or a style based on it: what they took from the
  style follows it, what they say themselves stays, and they are still
  written as `<w:pStyle/>` / `<w:rStyle/>` alone.
- **Character styles** are `w:rStyle`; a linked style on part of a
  paragraph applies its character half. **Clear Formatting** keeps the
  style (and the list); **Enter** at a paragraph's end gives the next one
  the style's "style for following paragraph".
- **Format Painter** (Home ▸ Clipboard): a click paints the next text
  selected with the mouse (a click paints the word under it), a double
  click paints until Esc; Ctrl+Shift+C / Ctrl+Shift+V copy and paste
  formatting. The paragraph's look travels when the copied selection held a
  whole paragraph or nothing. (Track Changes is Word's Ctrl+Shift+E.)
- **Navigation pane** (View ▸ Navigation Pane): the headings by outline
  level (heading styles, `w:outlineLvl`), with their numbers; a click goes
  there, an arrow collapses or expands, Move Up / Move Down move a heading
  with its whole section (one undo step).

Every command is a persistent action (`writeBullet0`–`6`,
`writeNumbering0`–`6`, `writeMultilevel0`–`3`, `writeListLevel1`–`9`,
`writeDefine…Action`, `writeRestartNumberingAction`, …,
`writeQuickStyle0`–`15`, `writeStylesPaneAction`, `writeNewStyleAction`,
`writeModifyStyleAction`, `writeUpdateStyleAction`,
`writeDeleteStyleAction`, `writeFormatPainterAction`,
`writeCopyFormatAction`, `writePasteFormatAction`,
`writeNavigationPaneAction`) in the menus and on the ribbon, and one undo
step.

## Formatting tables, paragraphs and text (M70, D-079)

Word's own commands, under Word's names, on Word's model:

- **Table Design** (contextual tab while the caret is in a table): Shading
  (a colour slot and the Shading / No Shading commands; written `w:shd
  w:val="clear" w:color="auto" w:fill="RRGGBB"`), Borders — All, Outside,
  Inside, Top, Bottom, Left, Right, Inside Horizontal, Inside Vertical, No
  Border — drawn with the pen the Line Style, Line Weight (¼–6 pt) and Pen
  Colour slots choose. On some cells a border goes into those cells'
  `w:tcBorders`, and the cell across an outer edge takes the same side, as
  Word keeps a shared edge; on the whole table it goes into `w:tblBorders`
  and the cells' own sides it covers are dropped. An edge already drawn
  with the pen is taken away (Word's toggle).
- **Table Layout**: Select Cell / Row / Column / Table, Properties, Delete
  Rows / Columns / Table, insert rows and columns (a new row or column —
  Tab in the last cell included — takes its neighbour's cell formats, as in
  Word), merge (a selection across
  cells) and split, AutoFit Contents / Window / Fixed Column Width (`w:tblW`
  auto or pct, `w:tcW` auto, pct or dxa, `w:tblLayout`), Distribute Rows /
  Columns, the nine cell alignments (`w:vAlign` and the paragraphs'
  `w:jc`), Sort (by a column as text, number or date, ascending or
  descending, a header row kept; whole rows move with their formats), Repeat
  Header Rows (`w:tblHeader`; Write's layout repeats them on every page) and
  Convert to Text; Insert ▸ Table ▸ Convert Text to Table (tabs, commas,
  paragraphs or another character).
- **Table Properties**: preferred width (automatic, a length or a
  percentage), alignment, indent, fixed layout, default cell margins
  (`w:tblCellMar`); row height at least or exactly (`w:trHeight`), "allow
  row to break across pages" (`w:cantSplit`), repeat as header row; column
  width (`w:gridCol`, `w:tcW`); a cell's vertical alignment and own margins
  (`w:tcMar`).
- **Format ▸ Paragraph** (also Home ▸ Paragraph and the context menu):
  alignment, outline level (`w:outlineLvl`), indents and the first-line or
  hanging indent, spacing before and after with Auto, line spacing (single,
  1.5, double, at least, exactly, multiple), "don't add space between
  paragraphs of the same style" (`w:contextualSpacing`), and widow/orphan
  control, keep with next, keep lines together and page break before — which
  `write_pagination` lays out at once.
- **Format ▸ Tabs**: stops left, center, right, decimal or bar with a
  leader (dots, hyphens, underline), set, cleared or all cleared, and the
  document's default stop (`w:defaultTabStop`). A stop the paragraph's
  style sets and the user removed is written `w:val="clear"`. Leaders are
  drawn on the page, on paper and in PDF.
- **Format ▸ Borders and Shading**: the selected paragraphs (`w:pBdr`,
  `w:shd`) or, in a table, the selected cells or the whole table — a
  setting (none, box, all, grid, custom), style, colour, width, each edge,
  and the fill. Since M89 (D-097) the border colour's **Automatic** and the
  fill's **No Color** are entries of the colour button itself (QindaTK
  D-318; Word's `auto` and no `w:shd` fill), and an automatic border can be
  chosen again after a colour.
- **Format ▸ Font** (Ctrl+D): family, style, size, colour, underline style
  (single, words, double, thick, dotted, dash, dot-dash, dot-dot-dash, wave)
  and colour, strikethrough and double strikethrough, superscript,
  subscript, small caps, all caps, hidden; spacing expanded or condensed
  (`w:spacing`), scale (`w:w`), position raised or lowered (`w:position`),
  kerning (`w:kern`). The font and underline colours' **Automatic** is the
  colour button's own entry (M89, D-318). **Change Case** (Shift+F3 cycles lowercase →
  UPPERCASE → Capitalize Each Word → Sentence case) keeps each letter's
  formatting.

What Qt's formats cannot hold is kept in Word's own values beside them
(`docx_properties.h`, offsets 60–81; `docx_borders.h` for borders) and is
part of the import snapshots: an untouched value is written from the source
markup, a changed one re-derived — a border side by side, so the side the
user did not touch keeps its source element. Each command and each dialog's
answer is one undo step; the dialogs apply only what the user touched.
Insert ▸ Table makes Word's Table Grid table; a table from HTML or an older
Write takes Word's model (collapsed edges, its frame line as
`w:tblBorders`) at its first formatting command.

## Mail merge, envelopes and labels, endnotes, floating pictures (M77, D-086)

Word's Mailings tab, under Word's names, on Word's model:

- **Start Mail Merge**: Letters, E-mail Messages, Envelopes (Envelope
  Options: the document becomes the envelope with an «AddressBlock»),
  Labels (Label Options: the document becomes the label sheet, «Next
  Record» before every label but the first), Directory, Normal Word
  Document. The type is `w:mainDocumentType`.
- **Select Recipients**: Type a New List (Word's columns, New / Delete
  Entry, add or remove a column; saved as a CSV file), Use an Existing List
  (CSV or text with a header row — comma, tab, semicolon or pipe — or an
  Excel workbook's sheet through calc_io, Word's Select Table when it has
  several), Choose from QindaMail People (everyone, one of its contact
  groups — members expanded, nested groups too — or a category, read from
  QindaMail's address book without changing it). **Edit Recipient
  List**: the records with Word's check boxes, sort by a column heading,
  Filter and Sort (four rows of And / Or, field, comparison, value; three
  sort keys), Find Duplicates, Find Recipient.
- **Write & Insert Fields**: Highlight Merge Fields (display only),
  Address Block (Word's name formats, company, postal address, the
  country rule — never, always, only when not the home country — and
  formatting by country; a preview of the current record), Greeting Line
  (salutation, name format, punctuation, the greeting for a recipient
  without a name), Insert Merge Field (the data source's columns in twenty
  menu slots, and the dialog with Address Fields and Database Fields),
  Rules (If…Then…Else, Skip Record If, Next Record, Next Record If, Merge
  Record #, Merge Sequence #), Match Fields, Update Labels.
- **Preview Results**: the page shows the merged copy of a record
  (read-only), First / Previous / the record number / Next / Last, Find
  Recipient, Check for Errors (fields the data source lacks, recipients
  without an address, a document without merge fields).
- **Finish & Merge**: Edit Individual Documents (a new untitled window, a
  section per record), Print Documents (the shell's print dialog), Merge
  to PDF, Send E-mail Messages (each recipient's message handed to
  QindaMail as a composer draft — `mailto:` with a plain-text body, or the
  merged document attached — and never sent from QindaWrite); all records,
  the current one, or a range.
- **Merge fields in the text**: a run whose `kFieldInstruction` is Word's
  field-code view of the field — nested fields in braces (`IF { MERGEFIELD
  Country } = "US" "" "{ MERGEFIELD Country }"`), line breaks as `\n` — and
  whose text is the result Word shows («First_Name», «AddressBlock», «Next
  Record»). The merge (`docx_merge.h`) evaluates them as Word does (above,
  D-086).
- **Saved as Word saves a merge**: the main document keeps its fields;
  `w:mailMerge` is kept as read while unchanged, else written with
  `w:dataType` textFile, the query `SELECT * FROM <path>` with Word's
  WHERE and ORDER BY clauses for the filters and sorts, and `w:dataSource` an external
  relationship to the CSV (`file:///…`); a workbook sheet as Word's ACE
  connection string and `` `Sheet$` `` query. Recipients left out and a
  People source are QindaWrite's own `w:docVar`s. **Word on open** asks
  "Opening this document will run the following SQL command: SELECT * FROM
  … Data from your database will be placed in the document. Do you want to
  continue?"; Yes connects to the data source (asking for it when the path
  does not exist on that machine, as a Linux path on Windows), No opens the
  document as an ordinary one with its fields.
- **Envelopes** (Mailings ▸ Envelopes): delivery and return address (the
  return address remembered, or omitted), Size 10, 9, 11, 12, 14, Monarch,
  6 3/4, DL, C4, C5, C6, B5 or a custom size, the delivery address at
  Word's Auto place or from the left / top. Add to Document makes it the
  first section (its own page size and Word's paper code; an empty document
  becomes the envelope), New Document opens it in a window, Print prints
  it. Written as Word writes them: Envelope Return and Envelope Address
  styles, the envelope's `w:sectPr`.
- **Labels** (Mailings ▸ Labels): Avery US Letter (5160, 5161, 5162, 5163,
  5164, 5165, 5167, 5168, 5195, 5366, 5371, 8160, 8163) and A4 (L7159,
  L7160, L7161, L7162, L7163, L7165, L7166, L7167, L7168, L7169, L7173,
  L7651, J8160) with Word's dimensions; a full page of the same label or
  one label at a row and column; New Document or Print. The sheet is
  Word's fixed-layout table (spacer columns between labels with a gap,
  exact row heights that cannot split, no borders) on the sheet's margins
  with no bottom margin; on screen and in PDF each row is padded to the
  sheet's pitch with its labels centred, so a printed sheet lines up.
- **Endnotes** (References ▸ Insert Endnote, Alt+Ctrl+D): a mark numbered
  i, ii, iii and its note, edited by a double-click like a footnote,
  emptied to remove it; **Footnote and Endnote** sets each kind's number
  format (1 2 3, a b c, A B C, i ii iii, I II III, * † ‡ §), Start at,
  Numbering (continuous, restart each section, each page for footnotes)
  and place; **Convert** turns all footnotes into endnotes, all endnotes
  into footnotes, or swaps them. Each is one undo step (the marks and the
  note texts together). The notes follow the body's last line under a
  short rule, running onto more pages as needed, on screen, paper and PDF.
- **Floating pictures** (Picture Format ▸ Wrap Text, Position; Layout ▸
  Arrange): In Line with Text, Square, Tight, Through, Top and Bottom,
  Behind Text, In Front of Text; Move with Text / Fix Position on Page;
  Position and Wrapping (wrap side, distances, horizontal and vertical
  position as an alignment or an offset relative to the margin, page,
  column, character, paragraph or line). A picture switched from In Line
  stays where it was; a floating picture is dragged into place by its body
  (Word's four-way cursor); each change is one undo step. Square, Tight,
  Through and Top and Bottom are Qt's floating frames (the text flows to
  the side, below the picture for Top and Bottom); Behind and In Front take
  no room in the line and are drawn under or over the text.
- **Written as Word writes them**: an unchanged floating picture keeps its
  drawing byte for byte; a changed one is written as `wp:anchor` with
  `wp:simplePos`, `wp:positionH` / `wp:positionV` (`wp:align` or
  `wp:posOffset`), the wrap element (`wp:wrapSquare`, `wp:wrapTight` /
  `wp:wrapThrough` with their polygon, `wp:wrapTopAndBottom`,
  `wp:wrapNone` with `behindDoc`), the distances and flags; back to In Line
  as `wp:inline`.

Every command is a persistent action (`writeMerge…`, `writeMergeField0`–
`19`, `writeEnvelopesAction`, `writeLabelsAction`,
`writeInsertEndnoteAction`, `writeNoteOptionsAction`,
`writeConvertNotesAction`, `writeWrap…`, `writeMoveWithTextAction`,
`writeFixPositionAction`, `writePictureLayoutAction`) in the menus (the
Mailings menu, Insert, Format ▸ Wrap Text) and on the ribbon (Mailings,
References, the Picture Format contextual tab, Layout ▸ Arrange).

## Reading (`docx_reader*`)

Paragraph and run formatting is resolved the way Word resolves it
(`docx_styles.h`): document defaults → table style → paragraph style chain
→ numbering level → direct formatting; runs add the character style chain.
Word's quirks that documents rely on are kept: a table style beats the
default paragraph style (Table Grid cells have no space after), numbering
from a style yields to the style's own indent, `contextualSpacing`, theme
fonts and colours (`w:themeColor` with tint/shade).

Lists number and restart as Word numbers them: one counter set per
abstract definition (per instance when it overrides a start), a new
`QTextList` wherever Word restarts a level (its Qt prefix and start serve
HTML and Markdown), and the numbers drawn by `docx_list_labels.h`, so the
editing path and the reader show the same thing (M73).

Tables keep their grid, `gridSpan` / `vMerge` merges, borders (collapsed,
per side), shading, cell margins, vertical alignment, header rows, and the
table style's conditional formats (header row, banding, first column…).

Fields: PAGE, NUMPAGES, DATE, MERGEFIELD, REF, PAGEREF and the other
single-result fields keep their instruction on the result text and are
written back as `w:fldSimple`; Word's merge fields and rules keep their
nested codes and are written as complex fields where Word writes them so
(M77, below); HYPERLINK fields become links; TOC and other
multi-paragraph fields keep their result as text and links.

Tracked changes become Write's revision marks (blue insertions, red
struck deletions) and are written back as `w:ins` / `w:del`.

### Preserved, shown approximately

Kept verbatim and written back unchanged; the status bar says so on open:
text boxes and shapes (a box with their text), charts, SmartArt, OLE
objects, ink, equations (shown as their linear text). Floating pictures
and endnotes are modelled since M77 (below).

### Layout adjustments (display only, never written)

Qt's text layout differs from Word's in ways documents notice; each gap is
closed with state the writer ignores (D-068):

| Word does | Qt does | Adjustment |
| --- | --- | --- |
| adds space after + space before | keeps the larger | the sum is folded into the next block's top margin |
| applies the first paragraph's space before | drops a frame's first top margin | a zero-height synthetic spacer block |
| gives an empty paragraph after a table one line | lays it beside the table | its height moves to what follows |
| keeps a row's `trHeight` minimum | has no row heights | the row's first cell is padded (top and bottom alike when its text is centred, so it stays centred), again after a merge or Update Labels (`refreshRowHeights`, M77) |
| draws paragraph borders | has none | `drawParagraphBorders` paints them |
| pushes the body below a tall header | overlaps | the root frame margin grows |
| lays each section out on its own page size and margins (M88) | one page size | a canvas as wide as the widest section's text and as tall as the tallest's; a narrower section's paragraphs and tables get a right-margin pad (`kSectionPad`), a shorter section's pages break where its text area ends (before a paragraph, between two table rows); each page shows its section's part of the canvas (`write_sections.h`) |
| starts a section on the next, an even or an odd page (`w:type` of the section that starts) | — | a page break before its first paragraph or table, and a blank page before an odd / even start that would land on the wrong page (`write_pagination`) |
| spaces paragraphs marked for HTML autospacing (`w:beforeAutospacing` / `w:afterAutospacing`) by HTML's rules: 14 pt, none between items of one list or at a table cell's edges, adjacent spaces collapsing to the larger | uses the spacing values stored beside the flag | the reader computes Word's gap and folds it like the first row |
| keeps a keep-with-next paragraph (headings) on the page of the next one | breaks anywhere | a page break before the run (`write_pagination`) |
| keeps a table's keep-with-next leading rows together | splits between any rows | a page break before the table |
| never leaves a paragraph's first line alone at a page bottom (widow/orphan control, on unless the file turns it off) | does | a page break before the paragraph (a two-line one moves whole) |
| sets a single line from the font's Windows metrics (OS/2 win ascent + descent, plus the hhea gap beyond them) | ascent + descent + leading, rounded up | a LineDistanceHeight of the difference (`docx_metrics.h`; the paragraph's own spacing kept in `kLineSpacingOwn`) |

The pagination breaks (`Docx::kSyntheticBreak`, which keeps the block's own
break flags; a table row's on the last paragraph of a cell of the row
before) and the section pads are laid out again after edits from the
first changed paragraph on (from the start when a pad changed), joined to
the user's last undo step so Undo, Redo and the modified flag never show
them; the `.docx` and HTML writers ignore them.

## Writing (`docx_writer*`)

Each paragraph, run, table, row and cell read from Word carries its source
markup and a snapshot of the values the reader mapped. On save, properties
whose values still match the snapshot are written from the source markup;
changed ones are re-derived (D-068 rule 3), and children are sorted into
schema order. An untouched cell keeps its own `w:tcW`, `w:vMerge` spelling
and borders while the grid and its span are as read (M70). Lists keep their numbering instance while it is as read;
restarts and new or changed lists are written as Word writes them (M73,
above). The styles part is the edited one once a style command ran.
New content gets complete definitions: built-in styles mirroring
`StyleCatalog` (and Word's other built-ins on first use), nine-level
numbering, `Table Grid`, footnote styles.

Sections (M88): each section's `w:sectPr` is written from its markup
while its settings are as read — byte for byte apart from relationship
ids, its header and footer references in their order, their parts copied —
and patched element by element where they changed (`w:pgSz` keeps its
paper code when the paper only turned, `w:cols` its column list while the
count is the same); a section made in Write starts from the markup of the
section it was split from. A section whose headers or footers changed, or
whose links did, gets its references written again from its stories, a
slot it does not define left out (Word's Link to Previous); an empty
story with Link to Previous off is written as an empty part. A paragraph
that no longer ends a section (its break taken away, or a copy Enter made
before the real one) is written without the `w:sectPr` its markup had.

The output is ECMA-376 Transitional with Word's prefixes; Microsoft
extension namespaces are declared `mc:Ignorable`; package parts use the
default namespace (LibreOffice refuses prefixed `Relationships`).

Text that came from HTML or Markdown carries Qt's relative size steps
(headings, `<big>`, `<font size>`) instead of point sizes; the writer emits
the size the text shows at (`displayedPointSize`, Qt's ×0.7…×2.4 table over
the document's default font). Qt's generic family names ("Sans Serif",
"serif", "monospace") are written as Calibri, Times New Roman and Courier
New, which Word has and fontconfig maps back (`wordFontFamily`).

## Other apps writing Word

QindaNote ▸ *Export as Word Document* (page menu, Live ▸ Export, the
Session Review panel) and *Open in QindaWrite* write a page through
`office_docx` (`apps/qindanote/src/note_word_export.*`, D-068 QindaNote).

## Known limits

- Sections (M88): a paragraph that would run past a shorter section's page
  (a landscape section of a portrait document) moves to the next page
  whole — a page break before a paragraph cannot split it, Word splits it
  — so such pages can end a few lines early; a table row or a paragraph
  taller than a shorter section's page runs past it and is clipped at its
  foot. A continuous section break with other left or right margins on
  the same page is shown with that page's left margin (its width is its
  own). Columns (`w:cols`) are kept and written but laid out in one column
  (Qt's text layout has none). Chapter numbers in page numbers
  (`w:chapStyle`) are kept, not shown. Floating pictures placed relative to
  the page or a margin use the widest and tallest section's page. A
  section's page-number restart applies on its first page of its own (a
  continuous section starting mid-page restarts on the next page). Odd /
  even page section starts go by the physical page; even-page headers by
  the page number shown. A paragraph pasted into the middle of a section's
  last paragraph can leave the break before the pasted text's last
  paragraph; deleting a selection across a section break keeps the break
  at the joined paragraph (Backspace and Delete across one take it away
  as Word does). The blank page an odd / even section start leaves is
  shown in the view (Word's Print Layout hides it; both print it).
- Pagination follows Word's keep rules and line metrics (above), read from
  the font actually drawn: install the metric-compatible clones of Office's
  fonts (`media-fonts/crosextrafonts-carlito` for Calibri,
  `crosextrafonts-caladea` for Cambria; both machines have them) or a
  Calibri document is set in Liberation Sans, wider and with other
  metrics. Measured over the owner's 567 documents Word itself paginated
  (`pageCountsAgreeWithWord`): 383 lay out on exactly Word's page count and
  101 within one page; Write's total is 1 % shorter (3,630 pages to Word's
  3,666). On the 211 whose recorded count Word laid out last (its rendered
  page breaks agree with it): 156 exact (74 %), 198 within one page (94 %).
  What is left is mostly fonts: Aptos, Office's default since 2023, has no
  metric-compatible free clone, so Aptos documents lay out about 13 %
  shorter. Since M88 each section is laid out on its own page, and a
  continuous section break no longer starts a page.
- A widow (a paragraph's last line alone at a page top) is left alone when
  the paragraph has three lines or more: a block format cannot break inside
  a paragraph. Keep rules inside a table apply to its leading rows only.
- Text boxes and shapes are not editable as objects. Floating pictures
  (M77): Qt floats a picture at its anchor line, at the column's left or
  right; one centred in the column has text on one side only, one above
  its anchor line or at a fixed place on the page is shown at its line
  (Behind and In Front are drawn exactly where they are), Tight and
  Through wrap the picture's box, not its outline, and an inline picture
  is not dragged to move it in the text. A label (or any exact-height row)
  holding more lines than fit grows on screen and in Write's PDF; Word
  clips it.
- Mail merge (M77): Word's Ask, Fill-in and Set Bookmark rules are not
  offered; merge fields in headers and footers are kept but not merged;
  Preview Results is read-only; left-out recipients are a QindaWrite
  `w:docVar` Word keeps but does not apply; e-mail goes out through
  QindaMail as plain text or with the merged document attached (no HTML
  body); an Address Block's lines are line breaks in one paragraph (Word
  makes paragraphs).
- Endnotes (M77): endnotes placed at the end of each section are written
  so and shown at the end of the document; footnotes restarting each page
  are written so and numbered continuously in Write.
- Formatting Qt cannot draw is kept and written but shown approximately
  (M70): double, thick, words-only and heavy underlines as one line, double
  strikethrough as one line, hidden text shown (and printed), bar tabs not
  drawn, an exact row height as "at least" (Qt cannot clip a row), Word's
  automatic text colour on a dark fill shown black (Word shows it white).
  After a border command on the whole table, a table style's conditional
  borders (header row lines, banding) are not redrawn until the document is
  opened again. Paragraph borders do not reach HTML export (table shading
  and borders do). Rows with vertically merged cells are not sorted.
- Comment text is plain in Write: an edited comment is written as plain
  paragraphs (an untouched one keeps its formatting, links and mentions).
  Comments marked in headers, footers, notes or text boxes are kept on
  save but not listed in the pane.
- Lists and styles (M73): picture bullets and Word's "link level to style"
  (a list level's `w:pStyle`) are not offered; a table style applied from
  the Styles pane shows its whole-table borders and shading at once and its
  conditional rows and columns after the next open; where the text after a
  number starts is laid out at list commands and at open, not while
  typing; list items pasted from another document join a list of this one
  with the same `w:numId`; the Navigation pane moves sections with Move
  Up / Move Down, not by drag; style definitions are not edited while a
  header or footer is open; Modify Style's Numbering offers none, bullets
  or 1. 2. 3. (the list libraries apply any definition to the paragraphs).
- Row- and cell-level content controls (repeating sections) are read as
  their content and not written back as controls; controls in headers and
  footers are kept but not clickable; a control whose whole content is
  deleted goes with it (Word keeps an empty one). Legacy form fields
  (`FORMTEXT`, `FORMCHECKBOX`, `FORMDROPDOWN` with `w:ffData`) are not
  modelled: a text field keeps its text, a check box is lost on save.

## Gates

`tests/office_docx`: `tst_docx_read` (styles, spacing, lists, tables,
pictures, preservation, layout, headers, notes, links, revisions),
`tst_docx_write` (new document, unchanged markup, carried parts,
edited notes and tables — XSD-validated with `QQO_OOXML_SCHEMA_DIR`),
`tst_docx_tables` and `tst_docx_paragraphs` (M70: shading, borders on
cells and on the table, table / row / cell properties, AutoFit and
Distribute, a Qt table taking Word's model; spacing folded as the reader
folds it, outline and break switches, tab stops with leaders, paragraph
borders and shading, every font effect — each written, XSD-validated and
read back equal; an edited Word table's untouched cells keep their markup;
LibreOffice renders a sample, `QQO_GRAB_DIR` keeps the PDF),
`tst_docx_lists` and `tst_docx_styles` (M73: the list libraries written,
validated and read back with the same numbers, restart and continue as Word
writes them, an untouched list's numbering kept, multi-level and legal
labels; a modified style re-deriving its paragraphs and written as a real
`w:style`, a new style from a selection, character styles, Delete Style,
Clear Formatting, style numbering; LibreOffice renders both samples),
`tst_docx_controls` (content controls: kinds, block and inline, round trip,
patched check box / list / date / bound text, a copy without its id),
`tst_docx_comments` (ranges, replies, resolved state; untouched set copied,
edited / new / deleted comments written, header-only comments kept),
`tst_docx_corpus` (`QQO_DOCX_CORPUS` lists real documents: read, write,
re-read, compare; aggregate counts only), M77's `tst_docx_mailmerge`,
`tst_docx_envelopes`, `tst_docx_notes` and `tst_docx_floats` (below and in
verification.md), and M88's `tst_docx_sections` (a portrait / landscape /
portrait document with its own margins, headers, a page-number restart and
two columns read, written, schema-valid and read back equal; untouched
sections byte for byte; sections made in code with linked and unlinked
headers; odd and even page breaks; LibreOffice renders each page at its
section's size). `tests/qindawrite/tst_write_docx`
covers WriteDocument (layout, headers, pictures, recovery, HTML, legacy
formats through LibreOffice), `tst_write_comments` the Comments pane and
its commands through the real window, `tst_write_mailings`,
`tst_write_endnotes` and `tst_write_wrap` (M77) Word's Mailings, endnotes
and floating pictures through the real window, `tst_write_tables` and
`tst_write_formatting` Word's formatting commands and dialogs through the
real window (M70), `tst_write_lists` and `tst_write_styles` the list
libraries, dialogs and keys, the Styles pane, gallery, New / Modify /
Update / Delete Style, Clear Formatting, the Format Painter and the
Navigation pane through the real window (M73), `tst_write_sections` and
`tst_write_probe_sections` (M88: Qt's frame facts; a section break and a
landscape section with Undo, each page's size on screen and in PDF,
headers with Link to Previous, a roman page-number restart, keep rules in
a shorter section, a Word document with three sections, grabs),
`tst_write_controls` filling in a
form (check box, list, calendar, placeholder) through the real window and renders listed documents to PDF for
review (`QQO_WRITE_RENDER_DOCX` + `QQO_GRAB_DIR`). `tests/qindanote/tst_note_word`
exports a page with every block kind and ink, validates it, reads it back
(`QQO_GRAB_DIR` keeps the `.docx` for a look in Word or LibreOffice).
