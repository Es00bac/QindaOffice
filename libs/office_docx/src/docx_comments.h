// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word comments (D-068): the comments part read into WordParts::comments,
// written back, and found in the text.
//
// In the text a comment is the runs between its w:commentRangeStart and
// w:commentRangeEnd — each carries kCommentIds with its id — and its
// reference mark, Word's w:commentReference run, held as a zero-width
// space carrying kCommentReference. Replies and the resolved state come
// from commentsExtended (w15:commentEx: paraIdParent, done), keyed by the
// w14:paraId of each comment's last paragraph.
//
// AGENT-CONTRACT: the reader sets kCommentIds / kCommentReference
// (docx_reader_runs.cpp), the body writer turns them back into range
// markers and reference runs (docx_writer_body.cpp), and docx_writer.cpp
// copies the source's comment parts while no comment changed
// (commentsChanged()), else writes commentsPart() and
// commentsExtendedPart().

#include "docx_model.h"
#include "ooxml_package.h"
#include "ooxml_xml.h"

#include <QList>
#include <QRectF>

class QPainter;
class QTextDocument;

namespace QindaOffice::Docx {

// The comments a package carries (comments.xml + commentsExtended.xml of
// its main part), in the order Word wrote them.
[[nodiscard]] QList<Comment> readComments(const Ooxml::Package &package, const QString &mainPart);

// A comment's place in the text: [start, end) of its range (equal when it
// marks a point) and its reference mark, -1 when gone.
struct CommentSpan {
    QString id;
    int start = -1;
    int end = -1;
    int reference = -1;
};
[[nodiscard]] QList<CommentSpan> commentSpans(const QTextDocument &document);

// Whether the comments to write differ from the ones read: one added,
// edited, resolved, re-parented, or no longer referenced in `document`.
[[nodiscard]] bool commentsChanged(const WordParts &parts, const QTextDocument &document);

// The w:comments part for the comments `document` still references (an
// unchanged one from its own markup), and the w15:commentsEx part. A
// comment read from the source that the body never referenced
// (`sourceIds`, WordParts::sourceCommentIds) belongs to a header, a note
// or a kept text box and is always kept.
[[nodiscard]] Ooxml::XmlElementPtr commentsPart(const QList<Comment> &comments, const QStringList &sourceIds,
                                                const QTextDocument &document);
[[nodiscard]] Ooxml::XmlElementPtr commentsExtendedPart(const QList<Comment> &comments,
                                                        const QStringList &sourceIds,
                                                        const QTextDocument &document);

// A new comment's id (one past the largest) and a w14:paraId (8 hex digits
// below 0x80000000, as Word requires).
[[nodiscard]] QString nextCommentId(const QList<Comment> &comments);
[[nodiscard]] QString newParaId();

// Paints a band under every commented range (display only, D-004 paper
// colours); the `active` comment's band is stronger.
void drawCommentRanges(QPainter *painter, const QTextDocument &document, const QList<CommentSpan> &spans,
                       const QRectF &clip, const QString &active = QString());

} // namespace QindaOffice::Docx
