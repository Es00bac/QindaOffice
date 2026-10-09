<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Word interchange (QindaWrite, `office_docx`)

M60 / D-068. QindaWrite opens and saves Microsoft Word documents natively —
no LibreOffice, no HTML in between — and a saved file opens in Word with the
document's own styles, numbering, theme and everything Write does not edit.

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

- **Page layout** — paper size, orientation, four margins, header and footer
  distances, gutter. Layout ▸ Page Setup edits all of it (Letter, Legal,
  Tabloid, Executive, A3–A5, B5, #10 envelope or custom; inches or cm).
- **Headers and footers** — rich `QTextDocument`s per slot (default, first
  page, even pages) with pictures and PAGE / NUMPAGES fields; "different
  first page" in Page Setup. An untouched header is copied back byte for
  byte. They are edited in place as in Word (`page_story_editor.h`):
  double-click the top or bottom margin band, or Insert ▸ Edit Header /
  Edit Footer; the page's own slot opens (first page, even page), every
  formatting command, Undo and the clipboard work on it, Page Number and
  Number of Pages insert fields, and Esc, Close Header and Footer or a
  double-click on the text return. The Page Setup text fields still edit
  the default slot as a plain pattern (`{page}` / `{pages}`) and warn when
  that would replace a richer header.
- **Footnotes** — Write's markers and note texts; an unedited note keeps
  Word's own markup.
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
- **The source package** — what Write does not model is carried from it on
  save: styles, numbering definitions, theme, settings, fonts, endnotes,
  custom XML, glossary, people, VBA (macro kinds only), and the comment
  parts while no comment changed.

## Styles in a Word document

The style gallery's Normal, Title, Heading 1–6 and Quote apply the
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

## Reading (`docx_reader*`)

Paragraph and run formatting is resolved the way Word resolves it
(`docx_styles.h`): document defaults → table style → paragraph style chain
→ numbering level → direct formatting; runs add the character style chain.
Word's quirks that documents rely on are kept: a table style beats the
default paragraph style (Table Grid cells have no space after), numbering
from a style yields to the style's own indent, `contextualSpacing`, theme
fonts and colours (`w:themeColor` with tint/shade).

Lists number and restart as Word numbers them: one counter set per
abstract definition, a new `QTextList` wherever Word restarts a level, and
multi-level text such as `%1.%2.` drawn with the parent's number as prefix.

Tables keep their grid, `gridSpan` / `vMerge` merges, borders (collapsed,
per side), shading, cell margins, vertical alignment, header rows, and the
table style's conditional formats (header row, banding, first column…).

Fields: PAGE, NUMPAGES, DATE, MERGEFIELD, REF, PAGEREF and the other
single-result fields keep their instruction on the result text and are
written back as `w:fldSimple`; HYPERLINK fields become links; TOC and other
multi-paragraph fields keep their result as text and links.

Tracked changes become Write's revision marks (blue insertions, red
struck deletions) and are written back as `w:ins` / `w:del`.

### Preserved, shown approximately

Kept verbatim and written back unchanged; the status bar says so on open:
text boxes and shapes (a box with their text), charts, SmartArt, OLE
objects, ink, equations (shown as their linear text), endnote
references. Floating pictures are shown in line but keep their anchoring.

### Layout adjustments (display only, never written)

Qt's text layout differs from Word's in ways documents notice; each gap is
closed with state the writer ignores (D-068):

| Word does | Qt does | Adjustment |
| --- | --- | --- |
| adds space after + space before | keeps the larger | the sum is folded into the next block's top margin |
| applies the first paragraph's space before | drops a frame's first top margin | a zero-height synthetic spacer block |
| gives an empty paragraph after a table one line | lays it beside the table | its height moves to what follows |
| keeps a row's `trHeight` minimum | has no row heights | the row's first cell is padded |
| draws paragraph borders | has none | `drawParagraphBorders` paints them |
| pushes the body below a tall header | overlaps | the root frame margin grows |
| shows no header on a header-less cover section | one header set | different first page with empty slots |
| spaces paragraphs marked for HTML autospacing (`w:beforeAutospacing` / `w:afterAutospacing`) by HTML's rules: 14 pt, none between items of one list or at a table cell's edges, adjacent spaces collapsing to the larger | uses the spacing values stored beside the flag | the reader computes Word's gap and folds it like the first row |
| keeps a keep-with-next paragraph (headings) on the page of the next one | breaks anywhere | a page break before the run (`write_pagination`) |
| keeps a table's keep-with-next leading rows together | splits between any rows | a page break before the table |
| never leaves a paragraph's first line alone at a page bottom (widow/orphan control, on unless the file turns it off) | does | a page break before the paragraph (a two-line one moves whole) |
| sets a single line from the font's Windows metrics (OS/2 win ascent + descent, plus the hhea gap beyond them) | ascent + descent + leading, rounded up | a LineDistanceHeight of the difference (`docx_metrics.h`; the paragraph's own spacing kept in `kLineSpacingOwn`) |

The pagination breaks (`Docx::kSyntheticBreak`, which keeps the block's own
break flags) are laid out again after edits from the first changed
paragraph on, joined to the user's last undo step so Undo, Redo and the
modified flag never show them; the `.docx` and HTML writers ignore them.

## Writing (`docx_writer*`)

Each paragraph, run, table, row and cell read from Word carries its source
markup and a snapshot of the values the reader mapped. On save, properties
whose values still match the snapshot are written from the source markup;
changed ones are re-derived (D-068 rule 3), and children are sorted into
schema order. Lists keep their numbering instance unless their format
changed. New content gets complete definitions: built-in styles mirroring
`StyleCatalog`, nine-level numbering, `Table Grid`, footnote styles.

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

- One set of headers/footers and one page layout for the whole document;
  a multi-section document keeps its sections on save while untouched.
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
  shorter; documents with several sections (one page layout in Write) run
  longer.
- A widow (a paragraph's last line alone at a page top) is left alone when
  the paragraph has three lines or more: a block format cannot break inside
  a paragraph. Keep rules inside a table apply to its leading rows only.
- Floating pictures, text boxes and shapes are not editable as objects.
- Comment text is plain in Write: an edited comment is written as plain
  paragraphs (an untouched one keeps its formatting, links and mentions).
  Comments marked in headers, footers, notes or text boxes are kept on
  save but not listed in the pane.
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
`tst_docx_controls` (content controls: kinds, block and inline, round trip,
patched check box / list / date / bound text, a copy without its id),
`tst_docx_comments` (ranges, replies, resolved state; untouched set copied,
edited / new / deleted comments written, header-only comments kept),
`tst_docx_corpus` (`QQO_DOCX_CORPUS` lists real documents: read, write,
re-read, compare; aggregate counts only). `tests/qindawrite/tst_write_docx`
covers WriteDocument (layout, headers, pictures, recovery, HTML, legacy
formats through LibreOffice), `tst_write_comments` the Comments pane and
its commands through the real window, `tst_write_controls` filling in a
form (check box, list, calendar, placeholder) through the real window and renders listed documents to PDF for
review (`QQO_WRITE_RENDER_DOCX` + `QQO_GRAB_DIR`). `tests/qindanote/tst_note_word`
exports a page with every block kind and ink, validates it, reads it back
(`QQO_GRAB_DIR` keeps the `.docx` for a look in Word or LibreOffice).
