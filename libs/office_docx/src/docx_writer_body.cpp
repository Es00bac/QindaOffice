// SPDX-License-Identifier: GPL-3.0-or-later
// Paragraphs and runs of the .docx writer: text, breaks, hyperlinks, fields
// (simple, or complex for merge fields with nested codes and Word's rules
// without a result — M77), tracked changes, bookmarks, footnote and endnote
// references, pictures and preserved objects.

#include "docx_controls.h"
#include "docx_format.h"
#include "docx_merge_fields.h"
#include "docx_properties.h"
#include "docx_writer_p.h"
#include "docx_graphics.h"
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
    if (m_document) {
        const auto fields = referenceSpans(*m_document);
        for (const auto &span : fields) {
            const QString id = span.field.value(QStringLiteral("id")).toString();
            m_referenceOccurrences[id].append(span);
            m_referenceSpans.insert(id + QLatin1Char('@') + QString::number(span.start), span);
        }
        m_bookmarkRanges = bookmarkRanges(*m_document);
        QHash<QPair<int, int>, QStringList> bookmarksByRange;
        for (auto it = m_bookmarkRanges.cbegin(); it != m_bookmarkRanges.cend(); ++it) bookmarksByRange[{it->start, it->end}].append(it.key());
        for (const auto &span : fields) for (const auto &name : bookmarksByRange.value({span.start, span.end})) {
            if (m_bookmarkFieldOwners.contains(name)) continue;
            const QString key = span.field.value(QStringLiteral("id")).toString() + QLatin1Char('@') + QString::number(span.start);
            m_bookmarkFieldOwners.insert(name, key); m_fieldBookmarks[key].append(name);
        }
        for (auto it = m_bookmarkRanges.cbegin(); it != m_bookmarkRanges.cend(); ++it) m_bookmarkIds.insert(it.key(), QString::number(m_env.nextBookmark++));
    }
    if (m_env.parts != nullptr && !m_env.parts->contentControls.isEmpty() && m_document != nullptr) {
        m_controlTexts = controlTexts(*m_document);
    }
}

void BodyWriter::writeFrame(const QTextFrame *frame, XmlElement &container)
{
    // Qt keeps the block that follows a frame inside the parent; the last
    // block of a story is never skipped (Word needs a closing paragraph).
    QList<QTextFrame::iterator> items;
    for (auto it = frame->begin(); !it.atEnd(); ++it) {
        items.append(it);
    }
    writeItems(items, container);
}

bool BodyWriter::skipsBlock(const QTextBlock &block, bool requireParagraph)
{
    return !requireParagraph && block.blockFormat().boolProperty(kParagraphSynthetic) && block.text().isEmpty();
}

void BodyWriter::writeBlock(const QTextBlock &block, XmlElement &container, bool requireParagraph)
{
    const QTextBlockFormat bf = block.blockFormat();
    if (skipsBlock(block, requireParagraph)) {
        return;
    }
    XmlElement &p = container.appendElement(kW, QStringLiteral("p"));
    if (XmlElementPtr pPr = paragraphProperties(block)) {
        if (XmlElement *sectPr = pPr->child(kW, u"sectPr")) {
            m_env.sectionHook(*sectPr, m_part, m_env.sectionEnds.value(block.blockNumber()));
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
    XmlElementPtr element = writeGraphic(format, *m_env.parts, m_env.writer, m_part, &m_env.graphicError);
    const bool graphicWritten = bool(element);
    if (!element) element = Ooxml::parseFragment(format.stringProperty(kInlineXml));
    if (!element) {
        return nullptr;
    }
    if (!graphicWritten && m_env.copier) {
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
