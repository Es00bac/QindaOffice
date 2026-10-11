// SPDX-License-Identifier: GPL-3.0-or-later
// Sections and comments for the pptx writer (pptx_review.h).

#include "pptx_review.h"

#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QUuid>

#include <algorithm>

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

const char *const kLegacyCommentsType = "application/vnd.openxmlformats-officedocument.presentationml.comments+xml";
const char *const kLegacyAuthorsType = "application/vnd.openxmlformats-officedocument.presentationml.commentAuthors+xml";
const char *const kModernCommentsType = "application/vnd.ms-powerpoint.comments+xml";
const char *const kModernAuthorsType = "application/vnd.ms-powerpoint.authors+xml";

XmlElementPtr make(const QString &ns, const QString &name)
{
    return std::make_shared<XmlElement>(ns, name);
}

void set(XmlElement &e, const char *name, const QString &value)
{
    e.setAttribute(QString(), QLatin1String(name), value);
}

QString date(const QDateTime &when)
{
    const QDateTime utc = (when.isValid() ? when : QDateTime::currentDateTimeUtc()).toUTC();
    return utc.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"));
}

QString guid()
{
    return QUuid::createUuid().toString(QUuid::WithBraces).toUpper();
}

QString initialsOf(const QString &name)
{
    QString out;
    for (const QString &word : name.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        out += word.front().toUpper();
    }
    return out.left(3);
}

bool changed(const Slide &slide)
{
    const SlideSource *source = slide.origin.get();
    return source == nullptr ? !slide.comments.isEmpty() : slide.comments != source->comments;
}

// The source deck's author list of one kind, in order: id, name, initials.
QList<std::tuple<QString, QString, QString>> sourceAuthors(const WriteSession &session, bool modern, QString *part)
{
    QList<std::tuple<QString, QString, QString>> authors;
    const DeckSource *deck = session.deck.origin.get();
    if (deck == nullptr || !session.source) {
        return authors;
    }
    for (const Ooxml::Relationship &rel : session.source->relationships(deck->presentationPart)) {
        if (modern ? rel.type != modernAuthorsRelType() : rel.type != relType("commentAuthors")) {
            continue;
        }
        *part = session.source->targetPart(deck->presentationPart, rel);
        const XmlElementPtr root = session.source->xml(*part);
        for (const XmlElement *author : root ? root->elements() : QList<const XmlElement *>()) {
            authors.append({author->attr(u"id"), author->attr(u"name"), author->attr(u"initials")});
        }
    }
    return authors;
}

XmlElementPtr textBody(const QString &text)
{
    XmlElementPtr body = make(nsP188(), QStringLiteral("txBody"));
    body->appendElement(nsA(), QStringLiteral("bodyPr"));
    body->appendElement(nsA(), QStringLiteral("lstStyle"));
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        XmlElement &p = body->appendElement(nsA(), QStringLiteral("p"));
        if (line.isEmpty()) {
            set(p.appendElement(nsA(), QStringLiteral("endParaRPr")), "lang", QStringLiteral("en-US"));
            continue;
        }
        XmlElement &r = p.appendElement(nsA(), QStringLiteral("r"));
        set(r.appendElement(nsA(), QStringLiteral("rPr")), "lang", QStringLiteral("en-US"));
        r.appendElement(nsA(), QStringLiteral("t")).appendText(Ooxml::xmlSafeText(line));
    }
    return body;
}

// The a16:creationId a shape's own XML carries ("" = none).
QString shapeCreationId(const Shape &shape)
{
    const XmlElement *xml = shape.origin ? shape.origin->xml.get() : nullptr;
    const XmlElement *id = xml != nullptr ? xml->descendant(nsA16(), u"creationId") : nullptr;
    return id != nullptr ? id->attr(u"id") : QString();
}

void slideMoniker(XmlElement &list, quint32 creationId, quint32 sldId)
{
    list.appendElement(nsPc(), QStringLiteral("docMk"));
    XmlElement &slide = list.appendElement(nsPc(), QStringLiteral("sldMk"));
    set(slide, "cId", QString::number(creationId));
    set(slide, "sldId", QString::number(sldId));
}

void anchor(XmlElement &cm, const Slide &slide, const SlideComment &comment, quint32 creationId, quint32 sldId,
            const QHash<QString, quint32> &writtenIds)
{
    const Shape *shape = nullptr;
    for (const Shape &candidate : slide.shapes) {
        if (!comment.shapeId.isEmpty() && candidate.id == comment.shapeId && writtenIds.contains(candidate.id)) {
            shape = &candidate;
        }
    }
    if (shape == nullptr) {
        slideMoniker(cm.appendElement(nsPc(), QStringLiteral("sldMkLst")), creationId, sldId);
        return;
    }
    XmlElement &list = cm.appendElement(nsAc(), QStringLiteral("deMkLst"));
    slideMoniker(list, creationId, sldId);
    const QString kind = shape->kind == ShapeKind::Image                                       ? QStringLiteral("picMk")
        : shape->kind == ShapeKind::Group                                                     ? QStringLiteral("grpSpMk")
        : shape->kind == ShapeKind::Table || shape->kind == ShapeKind::Chart                  ? QStringLiteral("graphicFrameMk")
        : shape->connector                                                                    ? QStringLiteral("cxnSpMk")
                                                                                              : QStringLiteral("spMk");
    XmlElement &moniker = list.appendElement(nsAc(), kind);
    set(moniker, "id", QString::number(writtenIds.value(shape->id)));
    if (const QString id = shapeCreationId(*shape); !id.isEmpty()) {
        set(moniker, "creationId", id);
    }
}

QByteArray modernPart(const Slide &slide, quint32 creationId, quint32 sldId, const QHash<QString, quint32> &writtenIds,
                      const CommentState &state)
{
    XmlElementPtr list = make(nsP188(), QStringLiteral("cmLst"));
    QHash<QString, XmlElement *> threads;
    for (const SlideComment &comment : slide.comments) {
        XmlElement *parent = comment.isReply() ? threads.value(comment.parentId) : nullptr;
        if (comment.isReply() && parent == nullptr) {
            continue; // its thread is gone
        }
        XmlElementPtr e = make(nsP188(), parent != nullptr ? QStringLiteral("reply") : QStringLiteral("cm"));
        const QString id = comment.id.startsWith(QLatin1Char('{')) ? comment.id : guid();
        set(*e, "id", id);
        set(*e, "authorId", state.authorIds.value(comment.author));
        if (parent == nullptr && comment.resolved) {
            set(*e, "status", QStringLiteral("resolved"));
        }
        set(*e, "created", date(comment.created));
        if (parent == nullptr) {
            anchor(*e, slide, comment, creationId, sldId, writtenIds);
            // MS-PPTX CT_Comment: anchor, pos, replies, text. Coordinates are
            // relative to the slide or drawing object named by the anchor.
            XmlElement &pos = e->appendElement(nsP188(), QStringLiteral("pos"));
            set(pos, "x", QString::number(qRound64(Ooxml::Units::ptToEmu(comment.position.x()))));
            set(pos, "y", QString::number(qRound64(Ooxml::Units::ptToEmu(comment.position.y()))));
            threads.insert(comment.id, e.get());
            e->appendElement(textBody(comment.text));
            list->appendElement(e);
        } else {
            XmlElement *replies = parent->child(nsP188(), u"replyLst");
            if (replies == nullptr) {
                replies = &parent->insertElementBefore(make(nsP188(), QStringLiteral("replyLst")), {u"txBody", u"extLst"});
            }
            e->appendElement(textBody(comment.text));
            replies->appendElement(e);
        }
    }
    return Ooxml::serializeDocument(*list);
}

QByteArray legacyPart(const Slide &slide, CommentState &state)
{
    XmlElementPtr list = make(nsP(), QStringLiteral("cmLst"));
    QHash<QString, std::pair<QString, int>> written; // model id → author id, idx
    for (const SlideComment &comment : slide.comments) {
        const QString author = state.authorIds.value(comment.author);
        int idx = 0;
        if (comment.authorId == author && comment.id.startsWith(author + QLatin1Char('.'))) {
            idx = comment.id.mid(author.size() + 1).toInt(); // read from the file: kept
        }
        if (idx <= 0) {
            idx = ++state.lastIndex[author];
        }
        state.lastIndex[author] = qMax(state.lastIndex.value(author), idx);
        written.insert(comment.id, {author, idx});
        XmlElement &cm = list->appendElement(nsP(), QStringLiteral("cm"));
        set(cm, "authorId", author);
        set(cm, "dt", date(comment.created));
        set(cm, "idx", QString::number(idx));
        QPointF at = comment.position;
        for (const Shape &shape : slide.shapes) {
            if (!comment.shapeId.isEmpty() && shape.id == comment.shapeId) {
                at = shape.rect.normalized().topLeft();
            }
        }
        XmlElement &pos = cm.appendElement(nsP(), QStringLiteral("pos"));
        set(pos, "x", QString::number(qRound(at.x() * 8)));
        set(pos, "y", QString::number(qRound(at.y() * 8)));
        cm.appendElement(nsP(), QStringLiteral("text")).appendText(Ooxml::xmlSafeText(comment.text));
        if (comment.isReply() && written.contains(comment.parentId)) {
            const auto [parentAuthor, parentIdx] = written.value(comment.parentId);
            XmlElement &ext = cm.appendElement(nsP(), QStringLiteral("extLst")).appendElement(nsP(), QStringLiteral("ext"));
            set(ext, "uri", QString::fromUtf16(kThreadingUri));
            XmlElement &threading = ext.appendElement(nsP15(), QStringLiteral("threadingInfo"));
            set(threading, "timeZoneBias", QStringLiteral("0"));
            XmlElement &parent = threading.appendElement(nsP15(), QStringLiteral("parentCm"));
            set(parent, "authorId", parentAuthor);
            set(parent, "idx", QString::number(parentIdx));
        }
    }
    return Ooxml::serializeDocument(*list);
}

XmlElement &extensionList(XmlElement &sld)
{
    if (XmlElement *list = sld.child(nsP(), u"extLst")) {
        return *list;
    }
    return sld.appendElement(nsP(), QStringLiteral("extLst"));
}

// The slide's creation id: its own, else one made from its model id.
quint32 ensureCreationId(XmlElement &sld, const Slide &slide)
{
    if (const quint32 id = slideCreationId(sld)) {
        return id;
    }
    const quint32 id = (qHash(slide.id) & 0x7FFFFFFFu) | 1u;
    XmlElement &ext = extensionList(sld).appendElement(nsP(), QStringLiteral("ext"));
    set(ext, "uri", QString::fromUtf16(kCreationIdUri));
    set(ext.appendElement(nsP14(), QStringLiteral("creationId")), "val", QString::number(id));
    return id;
}

void relateModern(XmlElement &sld, const QString &relationshipId)
{
    XmlElement &ext = extensionList(sld).appendElement(nsP(), QStringLiteral("ext"));
    set(ext, "uri", QString::fromUtf16(kCommentRelUri));
    ext.appendElement(nsP188(), QStringLiteral("commentRel")).setAttribute(nsR(), QStringLiteral("id"), relationshipId);
}

} // namespace

CommentState prepareComments(const WriteSession &session)
{
    CommentState state;
    bool resolved = false;
    bool any = false;
    for (const Slide &slide : session.deck.slides) {
        any = any || !slide.comments.isEmpty();
        state.anyChanged = state.anyChanged || changed(slide);
        for (const SlideComment &comment : slide.comments) {
            resolved = resolved || comment.resolved;
        }
    }
    // A resolved comment needs PowerPoint's modern comments.
    state.modern = !session.deck.legacyComments || resolved;
    if (!any) {
        return state;
    }
    QString part;
    for (const auto &[id, name, initials] : sourceAuthors(session, state.modern, &part)) {
        if (!state.authorIds.contains(name)) {
            state.authorIds.insert(name, id);
            state.initials.insert(name, initials);
            state.order.append({id, name});
        }
    }
    // Ids a legacy deck's comments already use, so a new one takes the next.
    for (const Slide &slide : session.deck.slides) {
        for (const SlideComment &comment : slide.comments) {
            if (!state.modern && comment.id.contains(QLatin1Char('.'))) {
                const QString author = comment.id.section(QLatin1Char('.'), 0, 0);
                state.lastIndex[author] = qMax(state.lastIndex.value(author), comment.id.section(QLatin1Char('.'), 1).toInt());
            }
            if (state.authorIds.contains(comment.author)) {
                continue;
            }
            QString id = comment.authorId;
            const bool usable = state.modern ? id.startsWith(QLatin1Char('{')) : !id.isEmpty() && !id.startsWith(QLatin1Char('{'));
            if (!usable || std::any_of(state.order.cbegin(), state.order.cend(), [&id](const auto &e) { return e.first == id; })) {
                id = state.modern ? guid() : QString::number(state.order.size());
            }
            state.authorIds.insert(comment.author, id);
            state.initials.insert(comment.author, comment.initials.isEmpty() ? initialsOf(comment.author) : comment.initials);
            state.order.append({id, comment.author});
        }
    }
    return state;
}

void writeSlideComments(PartWriter &out, XmlElement &sld, const Slide &slide, quint32 sldId,
                        const QHash<QString, quint32> &writtenIds, CommentState &state)
{
    if (slide.comments.isEmpty()) {
        return;
    }
    const SlideSource *source = slide.origin.get();
    const bool carry = source != nullptr && !changed(slide) && !source->commentsPart.isEmpty() && out.session.copier != nullptr
        && source->modernComments == state.modern && (!state.modern || source->sldId == sldId);
    if (carry) {
        // Unchanged: the comments part byte for byte.
        for (const Ooxml::Relationship &rel : out.session.source->relationships(source->part)) {
            if (isCommentsRelType(rel.type)) {
                const QString id = out.session.copier->copyRelationship(source->part, rel.id, out.part);
                if (state.modern && !id.isEmpty()) {
                    relateModern(sld, id);
                }
            }
        }
        return;
    }
    Ooxml::PackageWriter &w = out.session.writer;
    if (state.modern) {
        const quint32 creationId = ensureCreationId(sld, slide);
        // PowerPoint names the part by the slide's id and creation id, in hex.
        const QString stem = QStringLiteral("ppt/comments/modernComment_%1_%2")
                                 .arg(QString::number(sldId, 16).toUpper(), QString::number(creationId, 16).toUpper());
        QString part = stem + QStringLiteral(".xml");
        if (w.hasPart(part)) {
            part = w.uniquePartName(stem + QLatin1Char('_'), QStringLiteral(".xml"));
        }
        w.addPart(part, modernPart(slide, creationId, sldId, writtenIds, state), QLatin1String(kModernCommentsType));
        relateModern(sld, out.relate(modernCommentsRelType(), part));
    } else {
        const QString part = w.uniquePartName(QStringLiteral("ppt/comments/comment"), QStringLiteral(".xml"));
        w.addPart(part, legacyPart(slide, state), QLatin1String(kLegacyCommentsType));
        out.relate(relType("comments"), part);
    }
}

void writeCommentAuthors(PartWriter &presentation, const CommentState &state)
{
    if (state.order.isEmpty()) {
        return;
    }
    WriteSession &session = presentation.session;
    const DeckSource *deck = session.deck.origin.get();
    if (!state.anyChanged && deck != nullptr && session.copier) {
        // Nothing changed: the author list as it was.
        for (const Ooxml::Relationship &rel : session.source->relationships(deck->presentationPart)) {
            if (state.modern ? rel.type == modernAuthorsRelType() : rel.type == relType("commentAuthors")) {
                session.copier->copyRelationship(deck->presentationPart, rel.id, presentation.part);
                return;
            }
        }
    }
    XmlElementPtr list = make(state.modern ? nsP188() : nsP(), state.modern ? QStringLiteral("authorLst") : QStringLiteral("cmAuthorLst"));
    int color = 0;
    for (const auto &[id, name] : state.order) {
        XmlElement &author = list->appendElement(list->ns, state.modern ? QStringLiteral("author") : QStringLiteral("cmAuthor"));
        set(author, "id", id);
        set(author, "name", name);
        set(author, "initials", state.initials.value(name));
        if (state.modern) {
            set(author, "userId", name);
            set(author, "providerId", QStringLiteral("None"));
        } else {
            set(author, "lastIdx", QString::number(qMax(1, state.lastIndex.value(id))));
            set(author, "clrIdx", QString::number(color++ % 8));
        }
    }
    const QString part = state.modern ? QStringLiteral("ppt/authors.xml") : QStringLiteral("ppt/commentAuthors.xml");
    session.writer.addPart(part, Ooxml::serializeDocument(*list),
                           QLatin1String(state.modern ? kModernAuthorsType : kLegacyAuthorsType));
    presentation.relate(state.modern ? modernAuthorsRelType() : relType("commentAuthors"), part);
}

XmlElementPtr sectionsExtension(const QList<SlideSection> &sections, const QHash<QString, quint32> &slideIds)
{
    if (sections.isEmpty()) {
        return nullptr;
    }
    XmlElementPtr ext = make(nsP(), QStringLiteral("ext"));
    set(*ext, "uri", QString::fromUtf16(kSectionsUri));
    XmlElement &list = ext->appendElement(nsP14(), QStringLiteral("sectionLst"));
    for (const SlideSection &section : sections) {
        XmlElement &e = list.appendElement(nsP14(), QStringLiteral("section"));
        set(e, "name", section.name);
        set(e, "id", section.id.startsWith(QLatin1Char('{')) ? section.id : guid());
        XmlElement &slides = e.appendElement(nsP14(), QStringLiteral("sldIdLst"));
        for (const QString &slide : section.slides) {
            if (const quint32 id = slideIds.value(slide)) {
                set(slides.appendElement(nsP14(), QStringLiteral("sldId")), "id", QString::number(id));
            }
        }
    }
    return ext;
}

} // namespace QindaOffice::Show::Pptx
