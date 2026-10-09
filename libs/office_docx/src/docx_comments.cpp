// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_comments.h"

#include "docx_properties.h"
#include "docx_styles.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QAbstractTextDocumentLayout>
#include <QHash>
#include <QPainter>
#include <QRandomGenerator>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <functional>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kW14 = QString::fromUtf16(Ooxml::Ns::W14);
const QString kW15 = QString::fromUtf16(Ooxml::Ns::W15);
const QString kExtendedRel = QStringLiteral("http://schemas.microsoft.com/office/2011/relationships/commentsExtended");

// Paper colours (D-004): Word's comment bands.
const QColor kBand(0xff, 0xe8, 0x99, 0x80);
const QColor kActiveBand(0xff, 0xc8, 0x3d, 0xb0);

XmlElementPtr related(const Ooxml::Package &package, const QString &main, const QString &type)
{
    const auto rel = package.firstOfType(main, type);
    if (!rel) {
        return nullptr;
    }
    const QString part = package.targetPart(main, *rel);
    return part.isEmpty() ? nullptr : package.xml(part);
}

QString paragraphText(const XmlElement &p)
{
    QString text;
    std::function<void(const XmlElement &)> walk = [&](const XmlElement &e) {
        for (const Ooxml::XmlChild &child : e.children) {
            if (!child.element) {
                continue;
            }
            const XmlElement &c = *child.element;
            if (c.ns == kW && c.name == u"t") {
                text += c.text();
            } else if (c.ns == kW && c.name == u"tab") {
                text += QLatin1Char('\t');
            } else if (c.ns == kW && (c.name == u"br" || c.name == u"cr")) {
                text += QChar::LineSeparator;
            } else if (!(c.ns == kW && (c.name == u"delText" || c.name == u"instrText"))) {
                walk(c);
            }
        }
    };
    walk(p);
    return text;
}

QSet<QString> referencedIds(const QTextDocument &document)
{
    QSet<QString> ids;
    for (const CommentSpan &span : commentSpans(document)) {
        if (span.reference >= 0) {
            ids.insert(span.id);
        }
    }
    return ids;
}

// The comments a rewritten part keeps: the ones the body references, and
// the ones read from the source that the body never did — a header, a
// note or a kept text box references those, and Write does not list them.
// AGENT-GUARD: dropping one of those leaves its w:commentReference
// dangling, which Word reports as unreadable content.
QSet<QString> keptIds(const QList<Comment> &comments, const QStringList &sourceIds, const QTextDocument &document)
{
    QSet<QString> ids = referencedIds(document);
    const QSet<QString> fromBody(sourceIds.cbegin(), sourceIds.cend());
    for (const Comment &c : comments) {
        if (!c.sourceXml.isEmpty() && !fromBody.contains(c.id)) {
            ids.insert(c.id);
        }
    }
    return ids;
}

QString paraIdOf(const QList<Comment> &comments, const QString &id)
{
    for (const Comment &c : comments) {
        if (c.id == id) {
            return c.paraId;
        }
    }
    return {};
}

} // namespace

QList<Comment> readComments(const Ooxml::Package &package, const QString &mainPart)
{
    QList<Comment> out;
    const XmlElementPtr root = related(package, mainPart, QString::fromUtf16(Ooxml::RelType::Comments));
    if (!root) {
        return out;
    }
    QHash<QString, qsizetype> byParaId;
    for (const XmlElement *c : root->elements(kW, u"comment")) {
        Comment comment;
        comment.id = wAttr(c, u"id");
        comment.author = wAttr(c, u"author");
        comment.initials = wAttr(c, u"initials");
        comment.date = QDateTime::fromString(wAttr(c, u"date"), Qt::ISODate);
        QStringList paragraphs;
        for (const XmlElement *p : c->elements(kW, u"p")) {
            paragraphs.append(paragraphText(*p));
            comment.paraId = p->attribute(kW14, u"paraId").toUpper();
        }
        comment.text = paragraphs.join(QLatin1Char('\n'));
        comment.sourceXml = Ooxml::serializeFragment(*c);
        comment.sourceText = comment.text;
        if (!comment.paraId.isEmpty()) {
            byParaId.insert(comment.paraId, out.size());
        }
        out.append(comment);
    }
    if (const XmlElementPtr extended = related(package, mainPart, kExtendedRel)) {
        for (const XmlElement *e : extended->elements(kW15, u"commentEx")) {
            const qsizetype at = byParaId.value(e->attribute(kW15, u"paraId").toUpper(), -1);
            if (at < 0) {
                continue;
            }
            out[at].done = Ooxml::Units::parseOnOff(e->attribute(kW15, u"done"), false);
            const qsizetype parent = byParaId.value(e->attribute(kW15, u"paraIdParent").toUpper(), -1);
            if (parent >= 0) {
                out[at].parentId = out.at(parent).id;
            }
        }
    }
    for (Comment &c : out) {
        c.sourceParentId = c.parentId;
        c.sourceDone = c.done;
    }
    return out;
}

QList<CommentSpan> commentSpans(const QTextDocument &document)
{
    QList<CommentSpan> spans;
    QHash<QString, qsizetype> index;
    auto spanFor = [&](const QString &id) -> CommentSpan & {
        auto it = index.constFind(id);
        if (it == index.constEnd()) {
            it = index.insert(id, spans.size());
            spans.append({id, -1, -1, -1});
        }
        return spans[it.value()];
    };
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QTextCharFormat format = fragment.charFormat();
            const int from = fragment.position();
            const int to = from + fragment.length();
            for (const QString &id : format.property(kCommentIds).toStringList()) {
                CommentSpan &span = spanFor(id);
                span.start = span.start < 0 ? from : qMin(span.start, from);
                span.end = qMax(span.end, to);
            }
            if (const QString id = format.stringProperty(kCommentReference); !id.isEmpty()) {
                CommentSpan &span = spanFor(id);
                span.reference = from;
                if (span.start < 0) {
                    span.start = span.end = from; // a comment on a point
                }
            }
        }
    }
    return spans;
}

bool commentsChanged(const WordParts &parts, const QTextDocument &document)
{
    const QSet<QString> referenced = referencedIds(document);
    if (referenced != QSet<QString>(parts.sourceCommentIds.cbegin(), parts.sourceCommentIds.cend())) {
        return true;
    }
    for (const Comment &c : parts.comments) {
        if (referenced.contains(c.id) && !c.isUnchanged()) {
            return true;
        }
    }
    return false;
}

XmlElementPtr commentsPart(const QList<Comment> &comments, const QStringList &sourceIds,
                           const QTextDocument &document)
{
    const QSet<QString> kept = keptIds(comments, sourceIds, document);
    auto root = std::make_shared<XmlElement>(kW, QStringLiteral("comments"));
    for (const Comment &c : comments) {
        if (!kept.contains(c.id)) {
            continue; // its text was deleted: Word drops the comment too
        }
        if (c.isUnchanged()) {
            if (XmlElementPtr kept = Ooxml::parseFragment(c.sourceXml)) {
                root->appendElement(kept);
                continue;
            }
        }
        XmlElement &comment = root->appendElement(kW, QStringLiteral("comment"));
        comment.setAttribute(kW, QStringLiteral("id"), c.id);
        comment.setAttribute(kW, QStringLiteral("author"), c.author);
        if (c.date.isValid()) {
            comment.setAttribute(kW, QStringLiteral("date"), c.date.toUTC().toString(Qt::ISODate));
        }
        if (!c.initials.isEmpty()) {
            comment.setAttribute(kW, QStringLiteral("initials"), c.initials);
        }
        const QStringList lines = c.text.split(QLatin1Char('\n'));
        for (qsizetype i = 0; i < lines.size(); ++i) {
            XmlElement &p = comment.appendElement(kW, QStringLiteral("p"));
            p.setAttribute(kW14, QStringLiteral("paraId"), i + 1 == lines.size() && !c.paraId.isEmpty() ? c.paraId
                                                                                                    : newParaId());
            p.setAttribute(kW14, QStringLiteral("textId"), QStringLiteral("77777777"));
            p.appendElement(kW, QStringLiteral("pPr"))
                .appendElement(kW, QStringLiteral("pStyle"))
                .setAttribute(kW, QStringLiteral("val"), QStringLiteral("CommentText"));
            if (i == 0) {
                XmlElement &mark = p.appendElement(kW, QStringLiteral("r"));
                mark.appendElement(kW, QStringLiteral("rPr"))
                    .appendElement(kW, QStringLiteral("rStyle"))
                    .setAttribute(kW, QStringLiteral("val"), QStringLiteral("CommentReference"));
                mark.appendElement(kW, QStringLiteral("annotationRef"));
            }
            if (!lines.at(i).isEmpty()) {
                XmlElement &t = p.appendElement(kW, QStringLiteral("r")).appendElement(kW, QStringLiteral("t"));
                t.setAttribute(QString::fromUtf16(Ooxml::Ns::XML), QStringLiteral("space"), QStringLiteral("preserve"));
                t.appendText(lines.at(i));
            }
        }
    }
    return root;
}

XmlElementPtr commentsExtendedPart(const QList<Comment> &comments, const QStringList &sourceIds,
                                   const QTextDocument &document)
{
    const QSet<QString> kept = keptIds(comments, sourceIds, document);
    auto root = std::make_shared<XmlElement>(kW15, QStringLiteral("commentsEx"));
    for (const Comment &c : comments) {
        if (!kept.contains(c.id) || c.paraId.isEmpty()) {
            continue;
        }
        XmlElement &e = root->appendElement(kW15, QStringLiteral("commentEx"));
        e.setAttribute(kW15, QStringLiteral("paraId"), c.paraId);
        if (const QString parent = paraIdOf(comments, c.parentId); !c.parentId.isEmpty() && !parent.isEmpty()) {
            e.setAttribute(kW15, QStringLiteral("paraIdParent"), parent);
        }
        e.setAttribute(kW15, QStringLiteral("done"), c.done ? QStringLiteral("1") : QStringLiteral("0"));
    }
    return root;
}

QString nextCommentId(const QList<Comment> &comments)
{
    int next = 0;
    for (const Comment &c : comments) {
        next = qMax(next, c.id.toInt() + 1);
    }
    return QString::number(next);
}

QString newParaId()
{
    const quint32 value = QRandomGenerator::global()->bounded(0x10000000u, 0x7fffffffu);
    return QString::number(value, 16).toUpper().rightJustified(8, QLatin1Char('0'));
}

void drawCommentRanges(QPainter *painter, const QTextDocument &document, const QList<CommentSpan> &spans,
                       const QRectF &clip, const QString &active)
{
    if (spans.isEmpty()) {
        return;
    }
    QAbstractTextDocumentLayout *layout = document.documentLayout();
    painter->save();
    for (const CommentSpan &span : spans) {
        if (span.end <= span.start) {
            continue;
        }
        const QColor colour = span.id == active ? kActiveBand : kBand;
        for (QTextBlock block = document.findBlock(span.start); block.isValid() && block.position() < span.end;
             block = block.next()) {
            const QTextLayout *lines = block.layout();
            const QRectF box = layout->blockBoundingRect(block);
            if (lines == nullptr || !box.intersects(clip)) {
                continue;
            }
            for (int i = 0; i < lines->lineCount(); ++i) {
                const QTextLine line = lines->lineAt(i);
                const int lineStart = block.position() + line.textStart();
                const int lineEnd = lineStart + line.textLength();
                const int from = qMax(span.start, lineStart);
                const int to = qMin(span.end, lineEnd);
                if (from >= to) {
                    continue;
                }
                const qreal x1 = line.cursorToX(from - block.position());
                const qreal x2 = line.cursorToX(to - block.position());
                painter->fillRect(QRectF(box.left() + x1, box.top() + line.y(), x2 - x1, line.height()), colour);
            }
        }
    }
    painter->restore();
}

} // namespace QindaOffice::Docx
