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

## The model

The body is a `QTextDocument`; `Docx::WordParts` (`docx_model.h`) holds the
rest and `WriteDocument` owns both:

- **Page layout** — paper size, orientation, four margins, header and footer
  distances, gutter. Layout ▸ Page Setup edits all of it (Letter, Legal,
  Tabloid, Executive, A3–A5, B5, #10 envelope or custom; inches or cm).
- **Headers and footers** — rich `QTextDocument`s per slot (default, first
  page, even pages) with pictures and PAGE / NUMPAGES fields; "different
  first page" in Page Setup. An untouched header is copied back byte for
  byte; the Page Setup text fields edit the default slot as a plain pattern
  (`{page}` / `{pages}`) and warn when that would replace a richer header.
- **Footnotes** — Write's markers and note texts; an unedited note keeps
  Word's own markup.
- **Pictures** — the original bytes (`WordParts::media`, resource names
  `qwmedia:…`) travel with the document: Insert ▸ Image stores the file's
  bytes, not its path.
- **The source package** — what Write does not model is carried from it on
  save: styles, numbering definitions, theme, settings, fonts, comments,
  endnotes, custom XML, glossary, people, VBA (macro kinds only).

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
objects, ink, equations (shown as their linear text), comment and endnote
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
- Keep-with-next and widow control are not applied on screen (Qt has no
  such layout rule), so page breaks can fall earlier than in Word.
- Floating pictures, text boxes and shapes are not editable as objects.
- Comments are kept but not shown; there is no comments pane yet.
- Content controls (`w:sdt`) and legacy form fields are read as their
  content and not written back as controls.

## Gates

`tests/office_docx`: `tst_docx_read` (styles, spacing, lists, tables,
pictures, preservation, layout, headers, notes, links, revisions),
`tst_docx_write` (new document, unchanged markup, carried parts,
edited notes and tables — XSD-validated with `QQO_OOXML_SCHEMA_DIR`),
`tst_docx_corpus` (`QQO_DOCX_CORPUS` lists real documents: read, write,
re-read, compare; aggregate counts only). `tests/qindawrite/tst_write_docx`
covers WriteDocument (layout, headers, pictures, recovery, HTML, legacy
formats through LibreOffice) and renders listed documents to PDF for
review (`QQO_WRITE_RENDER_DOCX` + `QQO_GRAB_DIR`). `tests/qindanote/tst_note_word`
exports a page with every block kind and ink, validates it, reads it back
(`QQO_GRAB_DIR` keeps the `.docx` for a look in Word or LibreOffice).
