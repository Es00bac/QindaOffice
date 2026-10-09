// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Review comments on slides (M75, D-084), as PowerPoint keeps them: a
// comment belongs to a slide and is anchored to the slide (at a point) or
// to one of its shapes; a reply answers a thread's first comment; a
// thread can be resolved. PowerPoint writes "modern" comments
// (ppt/comments/modernComment_*.xml, authors in ppt/authors.xml) and,
// before 2021, "legacy" ones (ppt/comments/commentN.xml, authors in
// ppt/commentAuthors.xml, replies through p15:threadingInfo, no resolved
// state); pptx_comments.h reads both and writes the deck's own kind.
//
// AGENT-NOTE: comments are a flat list per slide in thread order (a
// thread's first comment, then its replies); `parentId` names the thread.
// `authorId` is the author's id in the deck's author list as read (a GUID
// for modern comments, a number for legacy ones) so an unchanged author
// keeps its entry; a comment typed in QindaShow leaves it empty.

#include <QDateTime>
#include <QPointF>
#include <QString>

namespace QindaOffice::Show {

struct SlideComment {
    QString id;       // modern: the comment's GUID; legacy: "<authorId>.<idx>"
    QString parentId; // the thread's first comment ("" = this is it)
    QString author;
    QString initials;
    QString authorId;
    QDateTime created; // UTC
    QString text;      // plain text; paragraphs are separated by '\n'
    bool resolved = false;
    QString shapeId;  // the model id of the shape it is on ("" = the slide)
    QPointF position; // on the slide, in points (slide-anchored comments)
    bool operator==(const SlideComment &) const = default;
    [[nodiscard]] bool isReply() const { return !parentId.isEmpty(); }
};

} // namespace QindaOffice::Show
