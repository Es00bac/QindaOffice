// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The mail merge itself (M77, D-086): copies of a main document with its
// merge fields replaced by one recipient's values, as Word's Finish &
// Merge makes them, and what a main document's fields need.
//
// One copy per recipient for letters, e-mail messages and envelopes; for
// labels a copy is a sheet, whose «Next Record» fields move on to the next
// recipient label by label; a directory runs every recipient into one
// list. A SKIPIF that holds drops its recipient's copy; NEXTIF moves on
// when its condition holds. Copies after the first start a new section
// (the main document's own w:sectPr, so each record starts a page, as in
// Word's merged document); a directory's run on. A paragraph left empty
// because every merge field on it was empty is removed (Word's default:
// w:doNotSuppressBlankLines off). The merged text carries no fields.

#include "docx_merge_fields.h"

#include <QRectF>
#include <QString>

class QPainter;
class QTextCharFormat;
class QTextCursor;
class QTextDocument;

namespace QindaOffice::Docx {

struct MergeRequest {
    const QTextDocument *main = nullptr;
    QString kind = QStringLiteral("letters"); // letters, email, envelopes, labels, directory
    const MergeData *data = nullptr;
    QList<int> order;                    // recipient order (recipientOrder())
    QMap<QString, QString> fieldMap;     // Match Fields
    int first = 0;                       // positions in `order`, inclusive
    int last = -1;                       // -1: to the end
    bool singleCopy = false;             // one copy only (Preview Results, one e-mail)
    // The w:sectPr a copy that another follows ends with; empty: a page
    // break before the next copy instead.
    QString sectionXml;
};

struct MergeResult {
    int copies = 0;
    QList<int> records; // data source indices merged, in output order
    QList<int> skipped; // data source indices a SKIPIF dropped
};

// Merges into `out`, which must be empty (set it up for layout first: page
// size, margins, default font — rows with a Word height are padded for what
// the merge put in them, docx_table_format.h refreshRowHeights). Pictures
// in the main document travel as resources of `out`; the caller copies
// WordParts::media.
MergeResult mergeDocument(QTextDocument *out, const MergeRequest &request);

// One merge field of a document: [position, position + length) shows it.
struct MergeFieldSpan {
    int position = 0;
    int length = 0;
    QString instruction;
};
// The merge fields of `document` in text order (body, table cells
// included); adjacent runs of one field are one span.
[[nodiscard]] QList<MergeFieldSpan> mergeFieldSpans(const QTextDocument &document);

// Check for Errors: what the main document asks of the data source that it
// does not have ("«Region» — the data source has no field Region").
[[nodiscard]] QStringList missingMergeColumns(const QTextDocument &document, const MergeData &data);

// Inserts a merge field at the cursor as Word shows it in a main document
// (`display`, or its label «First_Name»; the code in kFieldInstruction),
// in the cursor's character format. Returns the format to go on typing in.
QTextCharFormat insertMergeField(QTextCursor &cursor, const QString &instruction, const QString &display = QString());

// Highlight Merge Fields (display only, never printed): shades the merge
// fields of the part of `document` in `clip`, as Word's field shading.
void drawMergeFieldShading(QPainter *painter, const QTextDocument &document, const QRectF &clip);

} // namespace QindaOffice::Docx
