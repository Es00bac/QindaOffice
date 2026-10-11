// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_controls.h"
#include "docx_format.h"
#include "docx_merge_fields.h"
#include "docx_reference_fields.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"
#include <QScopeGuard>
namespace QindaOffice::Docx::detail {
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
namespace {
const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);
// One fragment's grouping keys (outermost first) and content.
struct Piece {
    QTextFragment fragment;
    QVariantList references;
    QStringList controls; // inline content controls, outermost first
    QString href;
    QString field;
    QString revision; // "ins:author" / "del:author"
    int island = 0;
};

QString revisionKey(const QTextCharFormat &f)
{
    if (f.hasProperty(kRevisionDeleteProperty)) {
        return QStringLiteral("del:") + f.stringProperty(kRevisionDeleteProperty);
    }
    if (f.hasProperty(kRevisionInsertProperty)) {
        return QStringLiteral("ins:") + f.stringProperty(kRevisionInsertProperty);
    }
    return {};
}

XmlElement &fieldChar(XmlElement &parent, const QString &type)
{
    XmlElement &run = parent.appendElement(kW, QStringLiteral("r"));
    run.appendElement(kW, QStringLiteral("fldChar")).setAttribute(kW, QStringLiteral("fldCharType"), type);
    return run;
}

// A field code as instrText runs, its nested fields ("{ … }", Word's
// field-code view) as nested complex fields and its line breaks as w:br.
void writeCode(XmlElement &parent, const QString &code)
{
    QString text;
    auto flush = [&] {
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (qsizetype k = 0; k < lines.size(); ++k) {
            if (k > 0) {
                parent.appendElement(kW, QStringLiteral("r")).appendElement(kW, QStringLiteral("br"));
            }
            if (!lines.at(k).isEmpty()) {
                XmlElement &t = parent.appendElement(kW, QStringLiteral("r")).appendElement(kW, QStringLiteral("instrText"));
                t.setAttribute(QString::fromUtf16(Ooxml::Ns::XML), QStringLiteral("space"), QStringLiteral("preserve"));
                t.appendText(lines.at(k));
            }
        }
        text.clear();
    };
    for (qsizetype i = 0; i < code.size(); ++i) {
        if (code.at(i) != QLatin1Char('{')) {
            text += code.at(i);
            continue;
        }
        int depth = 1;
        qsizetype j = i + 1;
        for (; j < code.size() && depth > 0; ++j) {
            depth += code.at(j) == QLatin1Char('{') ? 1 : code.at(j) == QLatin1Char('}') ? -1 : 0;
        }
        flush();
        fieldChar(parent, QStringLiteral("begin"));
        writeCode(parent, QLatin1Char(' ') + code.mid(i + 1, j - i - 2).trimmed() + QLatin1Char(' '));
        fieldChar(parent, QStringLiteral("end"));
        i = j - 1;
    }
    flush();
}

// Written as a complex field: a code with nested fields or line breaks,
// or one of Word's merge rules that have no result.
bool complexField(const QString &code)
{
    return code.contains(QLatin1Char('{')) || code.contains(QLatin1Char('\n'))
        || (isMergeField(code) && !mergeFieldHasResult(mergeFieldKind(code)));
}


} // namespace
void BodyWriter::writeRuns(const QTextBlock &block, XmlElement &p)
{
    QList<Piece> pieces;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        if (!fragment.isValid()) {
            continue;
        }
        const QTextCharFormat f = fragment.charFormat();
        Piece piece;
        piece.fragment = fragment;
        piece.controls = knownControls(f.property(kContentControls));
        piece.href = f.isAnchor() ? f.anchorHref() : QString();
        piece.references = f.property(kReferenceFields).toList();
        for (auto &reference : piece.references) {
            auto field = reference.toMap(); const QString id = field.value(QStringLiteral("id")).toString();
            for (const auto &span : m_referenceOccurrences.value(id)) if (fragment.position() >= span.start && fragment.position() < span.end) {
                field.insert(QStringLiteral("id"), id + QLatin1Char('@') + QString::number(span.start)); reference = field; break;
            }
        }
        if (!piece.references.isEmpty()) {
            const auto tokens = fieldTokens(piece.references.last().toMap().value(QStringLiteral("code")).toString());
            const QString type = tokens.value(0).toUpper();
            // REF/PAGEREF's native \h already owns its link. A second
            // hyperlink around the cache makes LibreOffice duplicate PAGEREF.
            if ((type == u"REF" || type == u"PAGEREF") && tokens.contains(QStringLiteral("\\h"))
                && piece.href == QLatin1Char('#') + tokens.value(1)) piece.href.clear();
        }
        piece.field = piece.references.isEmpty() ? f.stringProperty(kFieldInstruction) : QString();
        piece.revision = revisionKey(f);
        piece.island = f.hasProperty(kInlineXml) ? qMax(1, f.intProperty(kInlineSerial)) : 0;
        pieces.append(piece);
    }
    // Grouping levels, outermost first: one per nesting depth of inline
    // content controls (w:sdt), then links, simple fields, revisions.
    using Key = std::function<QString(const Piece &)>;
    qsizetype depth = 0;
    for (const Piece &piece : pieces) {
        depth = qMax(depth, piece.controls.size());
    }
    qsizetype referenceDepth = 0;
    for (const auto &piece : pieces) referenceDepth = qMax(referenceDepth, piece.references.size());
    QList<Key> keys;
    for (qsizetype d = 0; d < depth; ++d) {
        keys.append([d](const Piece &x) { return d < x.controls.size() ? x.controls.at(d) : QString(); });
    }
    for (qsizetype d = 0; d < referenceDepth; ++d) keys.append([d](const Piece &x) {
        return d < x.references.size() ? x.references.at(d).toMap().value(QStringLiteral("id")).toString() : QString();
    });
    const qsizetype linkLevel = depth + referenceDepth;
    keys.append([](const Piece &x) { return x.href; });
    keys.append([](const Piece &x) { return x.field; });
    keys.append([](const Piece &x) { return x.revision; });
    std::function<void(XmlElement &, qsizetype, qsizetype, int)> emitGroup;
    emitGroup = [&](XmlElement &parent, qsizetype from, qsizetype to, int level) {
        if (level == keys.size()) {
            int lastIsland = 0;
            for (qsizetype i = from; i < to; ++i) {
                const Piece &piece = pieces.at(i);
                const QTextCharFormat f = piece.fragment.charFormat();
                const auto names = f.property(kBookmarkRanges).toStringList();
                for (const auto &name : names) if (!m_bookmarkFieldOwners.contains(name) && m_bookmarkRanges.value(name).start == piece.fragment.position()) {
                    auto &start = parent.appendElement(kW, QStringLiteral("bookmarkStart"));
                    start.setAttribute(kW, QStringLiteral("id"), m_bookmarkIds.value(name));
                    start.setAttribute(kW, QStringLiteral("name"), name);
                }
                const auto closeBookmarks = qScopeGuard([&] {
                    for (const auto &name : names) if (!m_bookmarkFieldOwners.contains(name) && m_bookmarkRanges.value(name).end == piece.fragment.position() + piece.fragment.length())
                        parent.appendElement(kW, QStringLiteral("bookmarkEnd")).setAttribute(kW, QStringLiteral("id"), m_bookmarkIds.value(name));
                });
                syncComments(parent, f);
                if (const QString comment = f.stringProperty(kCommentReference); !comment.isEmpty()) {
                    // Word's comment mark, kept as a zero-width space (docx_comments.h).
                    XmlElement &run = parent.appendElement(kW, QStringLiteral("r"));
                    XmlElementPtr rPr = runProperties(f);
                    if (!rPr) {
                        rPr = std::make_shared<XmlElement>(kW, QStringLiteral("rPr"));
                    }
                    if (rPr->child(kW, u"rStyle") == nullptr) {
                        setChildVal(*rPr, QStringLiteral("rStyle"), QStringLiteral("CommentReference"));
                        sortChildren(*rPr);
                    }
                    m_env.usedStyles.insert(rPr->child(kW, u"rStyle")->attribute(kW, u"val"));
                    run.appendElement(rPr);
                    run.appendElement(kW, QStringLiteral("commentReference")).setAttribute(kW, QStringLiteral("id"), comment);
                    // AGENT-GUARD: text typed right after a mark can share its
                    // format; it is written as text, never swallowed.
                    if (const QString rest = piece.fragment.text().remove(QChar(0x200B)); !rest.isEmpty()) {
                        if (XmlElementPtr textRun = runFor(piece.fragment, rest, f.hasProperty(kRevisionDeleteProperty))) {
                            parent.appendElement(textRun);
                        }
                    }
                    continue;
                }
                for (const QString &name : f.anchorNames()) {
                    if (m_bookmarkRanges.contains(name)) continue;
                    const QString id = QString::number(m_env.nextBookmark++);
                    XmlElement &start = parent.appendElement(kW, QStringLiteral("bookmarkStart"));
                    start.setAttribute(kW, QStringLiteral("id"), id);
                    start.setAttribute(kW, QStringLiteral("name"), name);
                    parent.appendElement(kW, QStringLiteral("bookmarkEnd")).setAttribute(kW, QStringLiteral("id"), id);
                }
                if (piece.island != 0) {
                    if (piece.island != lastIsland) {
                        if (XmlElementPtr e = preservedElement(f)) {
                            parent.appendElement(e);
                        }
                    }
                    lastIsland = piece.island;
                    continue;
                }
                lastIsland = 0;
                const bool deleted = f.hasProperty(kRevisionDeleteProperty);
                const QString text = f.boolProperty(kEmptyReference) ? piece.fragment.text().remove(QChar(0x200B)) : piece.fragment.text();
                if (f.hasProperty(kFootnoteIdProperty)) {
                    const QString noteId = f.stringProperty(kFootnoteIdProperty);
                    if (!m_env.footnoteIds.contains(noteId)) {
                        m_env.footnoteIds.insert(noteId, m_env.nextFootnoteId++);
                        m_env.footnoteOrder.append(noteId);
                    }
                    XmlElement &run = parent.appendElement(kW, QStringLiteral("r"));
                    XmlElement &rPr = run.appendElement(kW, QStringLiteral("rPr"));
                    setChildVal(rPr, QStringLiteral("rStyle"), QStringLiteral("FootnoteReference"));
                    m_env.usedStyles.insert(QStringLiteral("FootnoteReference"));
                    run.appendElement(kW, QStringLiteral("footnoteReference"))
                        .setAttribute(kW, QStringLiteral("id"), QString::number(m_env.footnoteIds.value(noteId)));
                    continue;
                }
                if (f.hasProperty(kEndnoteIdProperty)) {
                    // An endnote's reference mark (M77): the note keeps its
                    // own id while endnotes.xml is copied as read.
                    const QString noteId = f.stringProperty(kEndnoteIdProperty);
                    if (!m_env.endnoteIds.contains(noteId)) {
                        m_env.endnoteIds.insert(noteId, m_env.keepEndnotesPart ? noteId.mid(5).toInt() : m_env.nextEndnoteId++);
                        m_env.endnoteOrder.append(noteId);
                    }
                    XmlElement &run = parent.appendElement(kW, QStringLiteral("r"));
                    XmlElement &rPr = run.appendElement(kW, QStringLiteral("rPr"));
                    setChildVal(rPr, QStringLiteral("rStyle"), QStringLiteral("EndnoteReference"));
                    m_env.usedStyles.insert(QStringLiteral("EndnoteReference"));
                    run.appendElement(kW, QStringLiteral("endnoteReference"))
                        .setAttribute(kW, QStringLiteral("id"), QString::number(m_env.endnoteIds.value(noteId)));
                    continue;
                }
                if (f.isImageFormat()) {
                    for (qsizetype c = 0; c < text.size(); ++c) {
                        if (XmlElementPtr run = pictureRun(f.toImageFormat())) {
                            parent.appendElement(run);
                        }
                    }
                    continue;
                }
                if (XmlElementPtr run = runFor(piece.fragment, text, deleted)) {
                    parent.appendElement(run);
                }
            }
            return;
        }
        qsizetype i = from;
        while (i < to) {
            const QString value = keys[level](pieces.at(i));
            qsizetype j = i + 1;
            while (j < to && keys[level](pieces.at(j)) == value) {
                ++j;
            }
            if (value.isEmpty()) {
                emitGroup(parent, i, j, level + 1);
            } else if (level < depth) {
                emitGroup(openControl(parent, value), i, j, level + 1);
            } else if (level < linkLevel) {
                const ReferenceSpan span = m_referenceSpans.value(value);
                if (span.start == pieces.at(i).fragment.position()) {
                    // A caption-number bookmark encloses the entire SEQ,
                    // not only the transient cached text Word recalculates.
                    for (const auto &name : m_fieldBookmarks.value(value)) {
                        auto &start = parent.appendElement(kW, QStringLiteral("bookmarkStart")); start.setAttribute(kW, QStringLiteral("id"), m_bookmarkIds.value(name)); start.setAttribute(kW, QStringLiteral("name"), name);
                    }
                    auto begin = Ooxml::parseFragment(span.field.value(QStringLiteral("beginXml")).toString());
                    if (begin) parent.appendElement(kW, QStringLiteral("r")).appendElement(begin);
                    else fieldChar(parent, QStringLiteral("begin"));
                    writeCode(parent, QLatin1Char(' ') + span.field.value(QStringLiteral("code")).toString() + QLatin1Char(' '));
                    fieldChar(parent, QStringLiteral("separate"));
                }
                emitGroup(parent, i, j, level + 1);
                const auto last = pieces.at(j - 1).fragment;
                if (span.end == last.position() + last.length()) {
                    fieldChar(parent, QStringLiteral("end"));
                    for (const auto &name : m_fieldBookmarks.value(value))
                        parent.appendElement(kW, QStringLiteral("bookmarkEnd")).setAttribute(kW, QStringLiteral("id"), m_bookmarkIds.value(name));
                }
            } else if (level == linkLevel) {
                XmlElement &link = parent.appendElement(kW, QStringLiteral("hyperlink"));
                if (value.startsWith(QLatin1Char('#'))) {
                    link.setAttribute(kW, QStringLiteral("anchor"), value.mid(1));
                } else {
                    const QString target = value.section(QLatin1Char('#'), 0, 0);
                    const QString anchor = value.section(QLatin1Char('#'), 1);
                    link.setAttribute(kR, QStringLiteral("id"),
                                      m_env.writer.addRelationship(m_part, QString::fromUtf16(Ooxml::RelType::Hyperlink),
                                                                   target, true));
                    if (!anchor.isEmpty()) {
                        link.setAttribute(kW, QStringLiteral("anchor"), anchor);
                    }
                }
                link.setAttribute(kW, QStringLiteral("history"), QStringLiteral("1"));
                emitGroup(link, i, j, level + 1);
            } else if (level == linkLevel + 1 && complexField(value)) {
                fieldChar(parent, QStringLiteral("begin"));
                writeCode(parent, QLatin1Char(' ') + value + QLatin1Char(' '));
                if (mergeFieldHasResult(mergeFieldKind(value))) {
                    fieldChar(parent, QStringLiteral("separate"));
                    emitGroup(parent, i, j, level + 1);
                }
                fieldChar(parent, QStringLiteral("end"));
            } else if (level == linkLevel + 1) {
                XmlElement &field = parent.appendElement(kW, QStringLiteral("fldSimple"));
                field.setAttribute(kW, QStringLiteral("instr"), QLatin1Char(' ') + value + QLatin1Char(' '));
                emitGroup(field, i, j, level + 1);
            } else {
                const bool insert = value.startsWith(u"ins:");
                XmlElement &mark = parent.appendElement(kW, insert ? QStringLiteral("ins") : QStringLiteral("del"));
                mark.setAttribute(kW, QStringLiteral("id"), QString::number(m_env.nextRevision++));
                mark.setAttribute(kW, QStringLiteral("author"), value.mid(4));
                const QDateTime when = pieces.at(i).fragment.charFormat().property(kRevisionDate).toDateTime();
                if (when.isValid()) {
                    mark.setAttribute(kW, QStringLiteral("date"), when.toUTC().toString(Qt::ISODate));
                }
                emitGroup(mark, i, j, level + 1);
            }
            i = j;
        }
    };
    emitGroup(p, 0, pieces.size(), 0);
}


} // namespace QindaOffice::Docx::detail
