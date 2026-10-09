// SPDX-License-Identifier: GPL-3.0-or-later
// Paragraphs and runs of the .docx writer: text, breaks, hyperlinks, simple
// fields, tracked changes, bookmarks, footnote references, pictures and
// preserved objects.

#include "docx_format.h"
#include "docx_properties.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QTextFrame>
#include <QUrl>

#include <functional>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
using Ooxml::XmlChild;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kWP = QString::fromUtf16(Ooxml::Ns::WP);

const QString kA = QString::fromUtf16(Ooxml::Ns::A);
const QString kPIC = QString::fromUtf16(Ooxml::Ns::PIC);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);

// One fragment's grouping keys (outermost first) and content.
struct Piece {
    QTextFragment fragment;
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

void appendText(XmlElement &run, const QString &text, bool deleted)
{
    if (text.isEmpty()) {
        return;
    }
    XmlElement &t = run.appendElement(kW, deleted ? QStringLiteral("delText") : QStringLiteral("t"));
    if (text.front().isSpace() || text.back().isSpace() || text.contains(QStringLiteral("  "))) {
        t.setAttribute(QString::fromUtf16(Ooxml::Ns::XML), QStringLiteral("space"), QStringLiteral("preserve"));
    }
    t.appendText(text);
}

} // namespace

QString WriteEnv::mediaPart(const QByteArray &bytes, const QString &extension)
{
    const QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
    const auto found = mediaByHash.constFind(hash);
    if (found != mediaByHash.constEnd()) {
        return found.value();
    }
    const QString name = writer.uniquePartName(QStringLiteral("word/media/image"), QLatin1Char('.') + extension);
    writer.addPart(name, bytes, Ooxml::ContentType::forImageExtension(extension));
    mediaByHash.insert(hash, name);
    return name;
}

BodyWriter::BodyWriter(WriteEnv &env, const QTextDocument *document, QString part)
    : m_env(env)
    , m_document(document)
    , m_part(std::move(part))
{
}

void BodyWriter::writeFrame(const QTextFrame *frame, XmlElement &container)
{
    // Qt keeps the block that follows a frame inside the parent; the last
    // block of a story is never skipped (Word needs a closing paragraph).
    QList<QTextFrame::iterator> items;
    for (auto it = frame->begin(); !it.atEnd(); ++it) {
        items.append(it);
    }
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QTextFrame::iterator &it = items.at(i);
        if (QTextFrame *child = it.currentFrame()) {
            if (const auto *table = qobject_cast<const QTextTable *>(child)) {
                writeTable(table, container);
            } else {
                writeFrame(child, container);
            }
            continue;
        }
        writeBlock(it.currentBlock(), container, i == items.size() - 1);
    }
}

void BodyWriter::writeBlock(const QTextBlock &block, XmlElement &container, bool requireParagraph)
{
    const QTextBlockFormat bf = block.blockFormat();
    if (!requireParagraph && bf.boolProperty(kParagraphSynthetic) && block.text().isEmpty()) {
        return;
    }
    XmlElement &p = container.appendElement(kW, QStringLiteral("p"));
    if (XmlElementPtr pPr = paragraphProperties(block)) {
        if (XmlElement *sectPr = pPr->child(kW, u"sectPr")) {
            m_env.sectionHook(*sectPr, m_part);
        }
        p.appendElement(pPr);
    }
    if (bf.boolProperty(kPageBreakRun) && (ownPageBreaks(bf) & QTextFormat::PageBreak_AlwaysBefore)
        && block.position() > 0) {
        XmlElement &run = p.appendElement(kW, QStringLiteral("r"));
        run.appendElement(kW, QStringLiteral("br")).setAttribute(kW, QStringLiteral("type"), QStringLiteral("page"));
    }
    writeRuns(block, p);
}

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
        piece.href = f.isAnchor() ? f.anchorHref() : QString();
        piece.field = f.stringProperty(kFieldInstruction);
        piece.revision = revisionKey(f);
        piece.island = f.hasProperty(kInlineXml) ? qMax(1, f.intProperty(kInlineSerial)) : 0;
        pieces.append(piece);
    }
    using Key = std::function<QString(const Piece &)>;
    const QList<Key> keys{[](const Piece &x) { return x.href; }, [](const Piece &x) { return x.field; },
                          [](const Piece &x) { return x.revision; }};
    std::function<void(XmlElement &, qsizetype, qsizetype, int)> emitGroup;
    emitGroup = [&](XmlElement &parent, qsizetype from, qsizetype to, int level) {
        if (level == keys.size()) {
            int lastIsland = 0;
            for (qsizetype i = from; i < to; ++i) {
                const Piece &piece = pieces.at(i);
                const QTextCharFormat f = piece.fragment.charFormat();
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
                const QString text = piece.fragment.text();
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
            } else if (level == 0) {
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
            } else if (level == 1) {
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

XmlElementPtr BodyWriter::runFor(const QTextFragment &fragment, const QString &text, bool deleted)
{
    auto run = std::make_shared<XmlElement>(kW, QStringLiteral("r"));
    if (XmlElementPtr rPr = runProperties(fragment.charFormat())) {
        run->appendElement(rPr);
    }
    QString pending;
    auto flush = [&] {
        appendText(*run, pending, deleted);
        pending.clear();
    };
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'\t':
            flush();
            run->appendElement(kW, QStringLiteral("tab"));
            break;
        case 0x2028: // QChar::LineSeparator
        case u'\n':
            flush();
            run->appendElement(kW, QStringLiteral("br"));
            break;
        case 0x00AD:
            flush();
            run->appendElement(kW, QStringLiteral("softHyphen"));
            break;
        case 0x2011:
            flush();
            run->appendElement(kW, QStringLiteral("noBreakHyphen"));
            break;
        case 0xFFFC: // an object without an image or preserved markup
        case 0x2029:
            break;
        default:
            pending.append(c);
        }
    }
    flush();
    return run->children.size() > (run->child(kW, u"rPr") != nullptr ? 1 : 0) ? run : nullptr;
}

XmlElementPtr BodyWriter::preservedElement(const QTextCharFormat &format)
{
    XmlElementPtr element = Ooxml::parseFragment(format.stringProperty(kInlineXml));
    if (!element) {
        return nullptr;
    }
    if (m_env.copier) {
        m_env.copier->rewrite(*element, format.stringProperty(kInlineSourcePart), m_part);
    }
    // Drawing ids must stay unique across the document.
    std::function<void(XmlElement &)> renumber = [&](XmlElement &e) {
        if (e.ns == kWP && e.name == u"docPr") {
            e.setAttribute(QString(), QStringLiteral("id"), QString::number(m_env.nextDocPr++));
        }
        for (XmlChild &c : e.children) {
            if (c.element) {
                renumber(*c.element);
            }
        }
    };
    renumber(*element);
    return element;
}

void BodyWriter::syncComments(XmlElement &parent, const QTextCharFormat &format)
{
    const QStringList ids = format.property(kCommentIds).toStringList();
    for (const QString &open : QStringList(m_openComments)) {
        if (!ids.contains(open)) {
            parent.appendElement(kW, QStringLiteral("commentRangeEnd")).setAttribute(kW, QStringLiteral("id"), open);
            m_openComments.removeAll(open);
        }
    }
    for (const QString &id : ids) {
        if (!m_openComments.contains(id)) {
            parent.appendElement(kW, QStringLiteral("commentRangeStart")).setAttribute(kW, QStringLiteral("id"), id);
            m_openComments.append(id);
        }
    }
}

void BodyWriter::closeComments(XmlElement &container)
{
    for (const QString &open : std::as_const(m_openComments)) {
        container.appendElement(kW, QStringLiteral("commentRangeEnd")).setAttribute(kW, QStringLiteral("id"), open);
    }
    m_openComments.clear();
}

} // namespace QindaOffice::Docx::detail
