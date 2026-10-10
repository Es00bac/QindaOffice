<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# QindaNote: OneNote's everyday note-taking (M84, D-093)

QindaNote is the suite's OneNote. Before M84 it had notebooks, sections and
pages, pen ink with pressure, live notes with AI and speech, Markdown
blocks, checklists, pictures and exports to PDF, Markdown and Word (M7–M65),
but typed text had no formatting. M84 adds what OneNote users do every
day: formatted typing, tags, search across notebooks, import, page
templates and page polish. The live-notes side is described in
[live notes](live-notes.md) and [QindaNote AI](qindanote-ai.md).

## Formatted typing

A text block is edited as formatted text. Pick the **Text** tool (T) or
**Add Text**, and type. The editor is QindaTK's `Tk.RichTextEditor`, laid
out by the same engine that paints the block on the page and on paper, so
the text does not move when the editor opens or closes.

| What | Where | Keys |
| --- | --- | --- |
| Bold, Italic, Underline | Text ▸ Basic Text, Format menu | Ctrl+B, Ctrl+I, Ctrl+U |
| Strikethrough, Subscript, Superscript | Text ▸ Basic Text, Format menu | — (OneNote's Ctrl+-, Ctrl+= and Ctrl+Shift+= are the suite's zoom keys) |
| Font, size, Grow / Shrink Font | Text ▸ Basic Text | Ctrl+Shift+> / < |
| Font colour, Text highlight colour | Text ▸ Basic Text (colour pickers, More Colours…) | Ctrl+Alt+H highlights |
| Clear Formatting | Text ▸ Basic Text | — |
| Styles: Normal, Title, Heading 1–6, Quote, Code | Text ▸ Styles, Format ▸ Styles | Ctrl+Alt+1 … 6 |
| Bullets, Numbering | Text ▸ Paragraph | Ctrl+., Ctrl+/ |
| Increase / Decrease Indent | Text ▸ Paragraph | Tab / Shift+Tab in a list; Alt+Shift+→ / ← |
| Align Left, Centre, Right | Text ▸ Paragraph | Ctrl+L, Ctrl+R |
| Link… | Insert ▸ Links | Ctrl+K |
| Link to File… | Insert ▸ Links | — |
| Table… | Insert ▸ Tables | Tab / Shift+Tab between cells |
| Insert / delete rows and columns, Delete Table | the Table tab (while the caret is in a table), Insert ▸ Table menu | — |
| Cut, Copy, Paste, Select All | Home ▸ Clipboard, Edit menu | Ctrl+X, C, V, A |

- **Lists.** Enter continues a list; Enter in an empty item ends it; Tab
  and Shift+Tab change the level. Outside a list, Increase Indent indents
  the paragraph by 18 pt, as OneNote's outline does.
- **Tables** live inside a text block. Tab moves to the next cell (past the
  last cell it leaves the table); the contextual **Table** tab and Insert ▸
  Table add rows above or below, columns left or right, and delete rows,
  columns or the table.
- **Links.** Ctrl+K asks for the address and, with nothing selected, the
  text to show; "example.org" means https://example.org. Ctrl+click opens a
  link, in the editor or on the page. **Link to File** links a document:
  a `.docx`, `.odt` or `.rtf` opens in QindaWrite, anything else in the
  desktop's application for it (OneNote's linked notes).
- **Paste** keeps formatting from the web, Word and QindaWrite — runs,
  headings, lists, tables, links — as sanitised HTML: scripts, styles,
  frames, objects, forms, event handlers, `javascript:` links, remote and
  local-file pictures and CSS `url()`s are removed before QindaTK parses it.
  A picture alone on the clipboard (a screenshot) pastes as an inline
  picture.
- **Undo** is per edit, on the page's one history: a run of typing, one
  format command, a paste, a table edit, a tag. Ctrl+Z inside the editor
  keeps the editor open and the caret where it was.
- With nothing open, **Insert Table**, **Link** and every tag start a new
  note holding them, as OneNote does; the formatting commands are off.
- Keys never clash: OneNote's keys that are the suite's zoom keys are left
  off, and **View ▸ Show Navigator** is Ctrl+Shift+G (F9 is live notes'
  This Matters).

## Tags

**Text ▸ Tags** and the **Tags** menu carry OneNote's tags. A tag applies
to the selected lines (paragraphs) of the open note, or to a whole
checklist item, Markdown block or picture; applying it again removes it.
With nothing open, a tag starts a new note with one tagged line.

| Tag | Look | Key |
| --- | --- | --- |
| To Do | a box in the margin that ticks | Ctrl+1 |
| Important | star | Ctrl+2 |
| Question | question mark | Ctrl+3 |
| Remember for later | yellow highlight of the line | Ctrl+4 |
| Definition | green highlight of the line | Ctrl+5 |
| Highlight | highlighter | Ctrl+6 |
| Contact, Address, Phone number | card, house, phone | Ctrl+7, 8, 9 |
| Idea, Critical, Project A, Project B | bolt, alert, red flag, orange flag | — |
| Remove Tag | | — (Ctrl+0 is the suite's Reset Zoom) |

- Icons hang in the margin left of the line, as in OneNote; a To Do box
  ticks with a click or tap with any tool but the eraser. Enter at the end
  of a tagged line starts the next line with the same tags (unticked).
- **Custom tags** (Tags ▸ Customize Tags…): a name, one of 23 icons and a
  colour; **Apply Tag…** lists every tag. Custom tags live in
  `$XDG_CONFIG_HOME/<app>/tags.json` (at most 64); a note keeps its custom
  tag's name, icon and colour, so it shows on any machine and after the tag
  is removed from the list.
- **Find Tags** (Tags ▸ Find Tags, or View ▸ Panes) opens the tag summary
  pane: the tagged lines of this page, section or notebook, grouped by tag
  (OneNote's order, then custom tags by name) or by page. A click opens the
  page with the caret in the line; a To Do ticks from the pane. **Create
  Tag Summary Page** collects them on a new page, tags included.
- Tags travel with the exports: on paper and in PDF as drawn; in Word, To Do
  as ☐ / ☑ and the others as their icons (small pictures); in Markdown,
  To Do as `- [ ]` / `- [x]` and the others as `[#Tag]` badges.

## Search across notebooks

**Search Notebooks…** (Ctrl+E, Edit menu, View ▸ Panes) opens the Search
pane. It finds typed and formatted text, live-notes Markdown, checklist
items, tag names and page titles in **this page**, **this section**, **this
notebook** or **all notebooks** — the notebooks open in other QindaNote
windows, then the recent ones on disk (read, not opened). Every word typed
must be somewhere on the page; case does not matter. Results are grouped
by section and page, with a snippet per match; a click opens the page —
and the notebook, when it was not open — with the caret in the block.
Handwriting and pictures are not searched: the suite has no OCR.

## Import

**File ▸ Import…** (also Insert ▸ Files) turns a document into pages after
the open one, in its order:

- **Word** (`.docx`, `.docm`, `.dotx`, `.dotm`) through `office_docx`:
  headings, bulleted and numbered lists, tables, pictures (their own bytes,
  inline) and the run formatting. A document with two or more Heading 1
  paragraphs becomes one page per Heading 1, the heading as the page title;
  text before the first one is a page named after the document.
- **Markdown** (`.md`): one page; a leading `# Title` names it. GitHub
  Markdown plus the page vocabulary (`==highlight==`, `++underline++`, …);
  `- [ ]` and `- [x]` items become To Do tags.
- **HTML** (`.html`, `.htm`): one page named by its `<title>`, sanitised as
  Paste is.
- **OneNote** (`.one`) files are not readable here, and the dialog says so:
  their format, MS-ONESTORE, has no open implementation the suite can use,
  and writing one is out of scope. In OneNote, export the pages to a Word
  document (File ▸ Export) or to PDF, and import the Word document.

## Pages

- **Page templates** (Insert ▸ Page Templates, Page ▸ Page Templates):
  Blank, Lined, Grid, Meeting Notes (date, attendees, agenda, notes,
  decisions and To Do action items) and To-Do List.
- **Page colour** (Page ▸ Page Colour): seven paper tints or any colour;
  it prints and goes to PDF.
- **Rule lines** (Page ▸ Rule Lines, the Home ▸ Pages picker): narrow,
  standard and wide ruling; small, standard and large grids; dots.
- **Subpages** (Page ▸ Make Subpage, Ctrl+Alt+]; Promote Subpage,
  Ctrl+Alt+[): a page indents under the page above it, two levels deep at
  most; the navigator shows the indent.
- Page versions are not kept (not cheap enough for this milestone).

## Exports

| Export | Formatted text | Tags |
| --- | --- | --- |
| Print, PDF | painted as on screen (one renderer) | drawn |
| Word (Export as Word Document, Open in QindaWrite) | runs (font, size, bold, italic, underline, strike, colour, highlight, super/subscript, links), Word's Title, Heading 1–6, Quote and Code styles, lists, tables, inline pictures | ☐ / ☑, icons as pictures |
| Markdown | `#`…`######`, `**`, `*`, `~~`, `++`, `==`, `^…^`, `~…~`, links, lists, `> ` quotes, fenced code, GFM tables | `- [ ]` / `- [x]`, `[#Tag]` |

Text colour has no Markdown form and is dropped there. Page colour does not
reach Word.

## Page format

<a id="page-format"></a>

A page is still one JSON file (`sections/<id>/pages/<id>.json`, the ink
envelope of `office_ink`); its `blocks` object is QindaNote's:

```json
{ "version": 2, "pageColor": "#fff8d6",
  "blocks": [
    { "type": "text", "rect": [40, 40, 680], "text": "Plain mirror\nof the text",
      "rich": { "schema": "qindatk.richtext", "version": 1, "…": "…" },
      "tags": [ { "p": "<paragraph id>", "tag": "todo", "done": false },
                { "p": "<paragraph id>", "tag": "custom:<uuid>", "name": "Waiting for",
                  "icon": "clock", "color": "#26a269" } ] },
    { "type": "check", "text": "Call back", "checked": false,
      "tags": [ { "tag": "phone" } ] } ] }
```

- `version` is the page format: absent (1) for pages written before M84,
  2 since. Every page M84 writes says 2; a page it only reads is not
  rewritten, so it stays byte for byte as it was.
- A text block keeps `text`, the plain mirror (paragraphs by `\n`, table
  cells by `\t`) that search, the assistant, the agent and every older
  QindaNote read. `rich` — QindaTK's `qindatk.richtext` codec object — is
  written only when the text carries formatting or tags. A block whose
  `rich` no longer matches its `text` was edited by an older QindaNote: the
  text wins and the stale formatting is ignored. The document defaults in
  `rich` are replaced by the page font on load, so formatted and plain
  blocks look alike on every machine.
- Tags name rich paragraphs by their stable ids (`p`); a whole-block tag
  has no `p`. Built-in tag ids: `todo`, `important`, `question`,
  `remember`, `definition`, `highlight`, `contact`, `address`, `phone`,
  `idea`, `critical`, `projectA`, `projectB`.
- `notebook.json` gains `level` (1 or 2) on subpages; pages without it are
  level 0, so a notebook without subpages keeps its index unchanged.

## Code map

| File | Responsibility |
| --- | --- |
| `note_rich_text.*` | a block's document: construction, plain mirror, `store()`, Replace, editor-content comparison |
| `note_rich_render.*` | layout and painting of formatted text (cached), line boxes, links and caret hits |
| `note_rich_style.*` | styles (Title, Heading 1–6, Quote, Code, Normal), links, table edits as transactions |
| `note_rich_export.h`, `note_rich_export_word.cpp`, `note_rich_export_md.cpp` | Word runs and Markdown |
| `note_rich_import.*`, `note_clipboard.*` | sanitised HTML, Markdown and Word bodies to formatted text; the paste clipboard |
| `note_tags.*`, `note_tag_summary.*` | the tag catalogue, custom tags, placement and painting, Find Tags |
| `note_search.*` | search across notebooks, the results model, pending navigation |
| `note_import.*`, `note_templates.*` | File ▸ Import; page templates |
| `note_page_item_rich.cpp`, `note_page_item_tags.cpp` | the page item's rich editor glue (history, commands) and tags |
| `note_host_format.cpp`, `note_host_tags.cpp`, `note_host_pages.cpp` | the commands (ActionEntries) and their state |
| `NotePage.qml`, `NoteTextSlots.qml`, `NoteTagsPanel.qml`, `NoteSearchPanel.qml`, `Note*Dialog.qml` | the editor, the Text tab's widgets, the panes and dialogs |

## Limits

- Long imports: a page is at most 64 page heights; split a long Word
  document with Heading 1s (it then becomes one page per chapter).
- Pictures inside formatted text are inline only (no floating or resizing
  handles); separate picture blocks keep their M21 behaviour.
- No OCR, no handwriting recognition: ink and pictures are not searched.
- No page versions; no Dock to Desktop (out of scope).
- QindaTK gaps found (reported, not worked around in the toolkit): see
  [D-093](decisions.md#d-093-qindanote-formatted-typing-tags-search-import).
