<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Word references

M94 / D-102 adds document-local sources, citations, bibliographies, captions,
cross-references and native tables of contents. Use the References ribbon or
Insert ▸ References. Commands share persistent window-scoped action IDs with
the exported menu. Editing works in the body; close a header or footer first.

## Sources and citations

Manage Sources creates, edits and deletes books, journal articles and websites.
A unique tag identifies each source. Personal authors use `Last, First; Last,
First`; corporate authors use the complete organization name. Publication
details include year, publisher/city, journal/volume/issue/pages and website
name/URL/accessed date. A cited source must have its citations removed before
it can be deleted. Source edits, citation updates and style changes participate
in the document's text Undo/Redo transaction, including crash recovery.

Insert Citation uses a chosen source and optional page/range. Edit Citation
changes a supported citation under the cursor. Author-date and
numeric are the two supported local formatting choices; these are compact
built-in renderers, not a complete APA/IEEE or CSL implementation. Numeric
labels follow first citation order, followed by uncited sources. Insert
Bibliography lists the document's sources in that order. Refresh References
updates supported cached results explicitly. A source-tag rename updates its
supported citations; a locked or unsupported citation prevents the rename.

Word sources are native `b:Sources` custom XML, with datastore properties and
package relationships. Unchanged source parts are copied exactly; edits patch
known fields while preserving unknown source details, author roles and root
attributes. Imported unsupported source types remain read-only, and unsupported
styles keep their existing results until a supported style is chosen. There is
no online lookup, global source library, style download or network access.

## Captions and cross-references

Insert Caption creates Figure, Table or Equation text with a native `SEQ`
number. Captions remain ordinary editable text. Refresh References renumbers
supported sequences after adding/removing/reordering captions and updates
references. Equation captions are generic and do not depend on the equation
editor. Cross-reference inserts caption text, number or page as native `REF`
or `PAGEREF` fields. Number references use a bookmark around the actual sequence
result, so Word receives a real caption number rather than paragraph numbering.
Optional hyperlinks navigate with Ctrl-click or Go to Reference.

Bookmark ranges are retained across paragraphs and saved with matching start
and end markers. Missing targets produce a visible reference error on explicit
refresh. Locked fields and unsupported switches retain their cached results.

## Table of contents

Table of Contents Options selects heading levels 1–9, hyperlinks and dot
leaders. The supported built-in heading styles and imported outline levels
supply entries. Native `TOC` fields contain nested `PAGEREF` fields and stable
heading bookmarks; TOC Heading and TOC1–TOC9 styles survive reopen. Tab leaders
use the same paragraph painter for the view, print and PDF.

Update Table of Contents explicitly rebuilds supported entries and refreshes
page numbers after pagination. No automatic whole-document refresh runs while
typing. Imported locked TOCs remain unchanged. Unknown TOC switches remain
unchanged under ordinary Update; choosing explicit supported options replaces
those options deliberately. Page results use Write's current physical page
layout; chapter-based caption numbering, custom field evaluation, indexes,
tables of authorities and a full reference-style catalog remain outside M94.

## Modules and verification

`docx_bibliography` owns source data/formatting and native source-part patching.
`docx_reference_fields` owns field stacks, occurrence IDs and bookmark ranges
(format properties 180–184). The reader records nested/multiparagraph fields;
`docx_writer_runs` emits one native begin/end pair for each occurrence, preserving
unknown codes and field locks. These modules remain Core/Gui-only.
`ReferencesController` and the cohesive `write_reference_edit` helpers own
window actions and undoable edits. The existing TOC module owns TOC generation.

Synthetic native package/schema, read/write/reopen, source preservation,
Undo/Redo/recovery, LibreOffice/PDF and real-window 1280/520-pixel checks are in
`tst_docx_references` and `tst_write_references`. Existing Word reader/writer,
review, mail-merge, menu export and UI fixture tests cover shared paths.
Qualification evidence and remaining manual checks are in
[verification.md](verification.md). No owner's document corpus is used here.
