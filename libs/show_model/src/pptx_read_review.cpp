// SPDX-License-Identifier: GPL-3.0-or-later
// Sections and comments for the pptx reader (pptx_review.h).

#include "pptx_review.h"

#include "pptx_origin.h"

#include <QTimeZone>

#include <algorithm>

namespace QindaOffice::Show::Pptx {

namespace {

struct Author {
    QString name;
    QString initials;
};

// The deck's author list of one kind: id → name and initials.
QHash<QString, Author> authorsOf(const DeckReadState &state, bool modern)
{
    QHash<QString, Author> authors;
    for (const Ooxml::Relationship &rel : state.package->relationships(state.main)) {
        if (modern ? rel.type != modernAuthorsRelType() : !rel.type.endsWith(QLatin1String("/commentAuthors"))) {
            continue;
        }
        const Ooxml::XmlElementPtr root = state.package->xml(state.package->targetPart(state.main, rel));
        if (!root) {
            continue;
        }
        for (const Ooxml::XmlElement *author : root->elements()) {
            if (author->name == u"author" || author->name == u"cmAuthor") {
                authors.insert(author->attr(u"id"), {author->attr(u"name"), author->attr(u"initials")});
            }
        }
    }
    return authors;
}

QDateTime parseDate(const QString &text)
{
    QDateTime when = QDateTime::fromString(text, Qt::ISODateWithMs);
    if (!when.isValid()) {
        when = QDateTime::fromString(text, Qt::ISODate);
    }
    if (when.isValid() && when.timeSpec() == Qt::LocalTime) {
        when.setTimeZone(QTimeZone(QTimeZone::UTC)); // PowerPoint writes UTC without a zone
    }
    return when;
}

QString plainText(const Ooxml::XmlElement *txBody)
{
    QStringList paragraphs;
    if (txBody != nullptr) {
        for (const Ooxml::XmlElement *p : txBody->elements(nsA(), u"p")) {
            QString line;
            for (const Ooxml::XmlElement *e : p->elements()) {
                if (e->name == u"r" || e->name == u"fld") {
                    if (const Ooxml::XmlElement *t = e->child(nsA(), u"t")) {
                        line += t->text();
                    }
                } else if (e->name == u"br") {
                    line += QLatin1Char('\n');
                }
            }
            paragraphs.append(line);
        }
    }
    return paragraphs.join(QLatin1Char('\n'));
}

// The shape a modern comment's anchor names (its moniker's id), "" for the slide.
QString anchoredShape(const Ooxml::XmlElement &cm, const QHash<quint32, QString> &shapeIds)
{
    for (const Ooxml::XmlElement *anchor : cm.elements()) {
        if (anchor->ns != nsAc()) {
            continue;
        }
        for (const Ooxml::XmlElement *moniker : anchor->elements()) {
            if (moniker->ns == nsAc() && moniker->name.endsWith(QLatin1String("Mk")) && moniker->hasAttribute(QString(), u"id")) {
                return shapeIds.value(moniker->attr(u"id").toUInt());
            }
        }
    }
    return {};
}

SlideComment modernEntry(const Ooxml::XmlElement &e, const QHash<QString, Author> &authors)
{
    SlideComment comment;
    comment.id = e.attr(u"id");
    comment.authorId = e.attr(u"authorId");
    comment.author = authors.value(comment.authorId).name;
    comment.initials = authors.value(comment.authorId).initials;
    comment.created = parseDate(e.attr(u"created"));
    comment.resolved = e.attr(u"status") == u"resolved" || e.attr(u"status") == u"closed";
    comment.text = plainText(e.child(nsP188(), u"txBody"));
    return comment;
}

void readModern(const DeckReadState &state, const QString &part, const QHash<quint32, QString> &shapeIds, Slide &slide)
{
    const Ooxml::XmlElementPtr root = state.package->xml(part);
    if (!root) {
        return;
    }
    const QHash<QString, Author> authors = authorsOf(state, true);
    for (const Ooxml::XmlElement *cm : root->elements(nsP188(), u"cm")) {
        SlideComment comment = modernEntry(*cm, authors);
        comment.shapeId = anchoredShape(*cm, shapeIds);
        if (const Ooxml::XmlElement *pos = cm->child(nsP188(), u"pos")) {
            comment.position = QPointF(emuToPt(pos->attr(u"x")), emuToPt(pos->attr(u"y")));
        }
        slide.comments.append(comment);
        if (const Ooxml::XmlElement *replies = cm->child(nsP188(), u"replyLst")) {
            for (const Ooxml::XmlElement *reply : replies->elements(nsP188(), u"reply")) {
                SlideComment answer = modernEntry(*reply, authors);
                answer.parentId = comment.id;
                answer.shapeId = comment.shapeId;
                answer.resolved = comment.resolved;
                slide.comments.append(answer);
            }
        }
    }
}

void readLegacy(const DeckReadState &state, const QString &part, Slide &slide)
{
    const Ooxml::XmlElementPtr root = state.package->xml(part);
    if (!root) {
        return;
    }
    const QHash<QString, Author> authors = authorsOf(state, false);
    QList<SlideComment> threads;
    QList<SlideComment> replies;
    for (const Ooxml::XmlElement *cm : root->elements(nsP(), u"cm")) {
        SlideComment comment;
        comment.authorId = cm->attr(u"authorId");
        comment.id = comment.authorId + QLatin1Char('.') + cm->attr(u"idx");
        comment.author = authors.value(comment.authorId).name;
        comment.initials = authors.value(comment.authorId).initials;
        comment.created = parseDate(cm->attr(u"dt"));
        if (const Ooxml::XmlElement *text = cm->child(nsP(), u"text")) {
            comment.text = text->text();
        }
        // p:pos is in master units: 576 to the inch, 8 to the point.
        if (const Ooxml::XmlElement *pos = cm->child(nsP(), u"pos")) {
            comment.position = QPointF(pos->attr(u"x").toDouble() / 8.0, pos->attr(u"y").toDouble() / 8.0);
        }
        if (const Ooxml::XmlElement *parent = cm->descendant(nsP15(), u"parentCm")) {
            comment.parentId = parent->attr(u"authorId") + QLatin1Char('.') + parent->attr(u"idx");
            replies.append(comment);
        } else {
            threads.append(comment);
        }
    }
    // Thread order: each first comment, then the replies to it.
    for (const SlideComment &thread : std::as_const(threads)) {
        slide.comments.append(thread);
        for (const SlideComment &reply : std::as_const(replies)) {
            if (reply.parentId == thread.id) {
                slide.comments.append(reply);
            }
        }
    }
    for (const SlideComment &reply : std::as_const(replies)) {
        if (std::none_of(threads.cbegin(), threads.cend(), [&reply](const SlideComment &t) { return t.id == reply.parentId; })) {
            SlideComment orphan = reply; // its thread is gone: it starts one
            orphan.parentId.clear();
            slide.comments.append(orphan);
        }
    }
}

} // namespace

const QString &nsP14()
{
    static const QString s = QStringLiteral("http://schemas.microsoft.com/office/powerpoint/2010/main");
    return s;
}

const QString &nsP15()
{
    static const QString s = QStringLiteral("http://schemas.microsoft.com/office/powerpoint/2012/main");
    return s;
}

const QString &nsP188()
{
    static const QString s = QStringLiteral("http://schemas.microsoft.com/office/powerpoint/2018/8/main");
    return s;
}

const QString &nsPc()
{
    static const QString s = QStringLiteral("http://schemas.microsoft.com/office/powerpoint/2013/main/command");
    return s;
}

const QString &nsAc()
{
    static const QString s = QStringLiteral("http://schemas.microsoft.com/office/drawing/2013/main/command");
    return s;
}

const QString &nsA16()
{
    static const QString s = QStringLiteral("http://schemas.microsoft.com/office/drawing/2014/main");
    return s;
}

QString modernCommentsRelType()
{
    return QStringLiteral("http://schemas.microsoft.com/office/2018/10/relationships/comments");
}

QString modernAuthorsRelType()
{
    return QStringLiteral("http://schemas.microsoft.com/office/2018/10/relationships/authors");
}

bool isCommentsRelType(const QString &type)
{
    return type == modernCommentsRelType() || type == relType("comments");
}

bool isAuthorsRelType(const QString &type)
{
    return type == modernAuthorsRelType() || type == relType("commentAuthors");
}

QList<SlideSection> readSections(const DeckReadState &state)
{
    QList<SlideSection> sections;
    const Ooxml::XmlElement *list = state.presentation->child(nsP(), u"extLst");
    if (list == nullptr) {
        return sections;
    }
    QHash<quint32, QString> slideById;
    if (const Ooxml::XmlElement *ids = state.presentation->child(nsP(), u"sldIdLst")) {
        for (const Ooxml::XmlElement *entry : ids->elements(nsP(), u"sldId")) {
            const auto rel = state.package->relationship(state.main, entry->attribute(nsR(), u"id"));
            const QString part = rel ? state.package->targetPart(state.main, *rel) : QString();
            if (state.slideIds.contains(part)) {
                slideById.insert(entry->attr(u"id").toUInt(), state.slideIds.value(part));
            }
        }
    }
    for (const Ooxml::XmlElement *ext : list->elements(nsP(), u"ext")) {
        const Ooxml::XmlElement *sectionList = ext->attr(u"uri") == QStringView(kSectionsUri) ? ext->child(nsP14(), u"sectionLst") : nullptr;
        if (sectionList == nullptr) {
            continue;
        }
        for (const Ooxml::XmlElement *e : sectionList->elements(nsP14(), u"section")) {
            SlideSection section;
            section.id = e->attr(u"id");
            section.name = e->attr(u"name");
            if (const Ooxml::XmlElement *slides = e->child(nsP14(), u"sldIdLst")) {
                for (const Ooxml::XmlElement *slide : slides->elements(nsP14(), u"sldId")) {
                    const QString id = slideById.value(slide->attr(u"id").toUInt());
                    if (!id.isEmpty()) {
                        section.slides.append(id);
                    }
                }
            }
            sections.append(section);
        }
    }
    return sections;
}

quint32 slideCreationId(const Ooxml::XmlElement &sld)
{
    const Ooxml::XmlElement *list = sld.child(nsP(), u"extLst");
    if (list == nullptr) {
        return 0;
    }
    for (const Ooxml::XmlElement *ext : list->elements(nsP(), u"ext")) {
        if (const Ooxml::XmlElement *id = ext->child(nsP14(), u"creationId")) {
            return id->attr(u"val").toUInt();
        }
    }
    return 0;
}

void readSlideComments(const DeckReadState &state, const QString &slidePart, const QHash<quint32, QString> &shapeIds,
                       Slide &slide, SlideSource &source)
{
    for (const Ooxml::Relationship &rel : state.package->relationships(slidePart)) {
        if (rel.external || !isCommentsRelType(rel.type)) {
            continue;
        }
        const QString part = state.package->targetPart(slidePart, rel);
        if (part.isEmpty()) {
            continue;
        }
        source.commentsPart = part;
        source.modernComments = rel.type == modernCommentsRelType();
        if (source.modernComments) {
            readModern(state, part, shapeIds, slide);
        } else {
            readLegacy(state, part, slide);
        }
    }
    source.comments = slide.comments;
}

} // namespace QindaOffice::Show::Pptx
