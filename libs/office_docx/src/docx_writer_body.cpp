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

// A preserved w:drawing at a new size: the anchor's (or inline's) extent
// and the picture shape's own a:ext.
void resizeDrawing(XmlElement &drawing, const QString &cx, const QString &cy)
{
    for (XmlChild &child : drawing.children) {
        if (!child.element || child.element->ns != kWP) {
            continue; // wp:inline or wp:anchor
        }
        for (XmlChild &part : child.element->children) {
            if (part.element && part.element->ns == kWP && part.element->name == u"extent") {
                part.element->setAttribute(QString(), QStringLiteral("cx"), cx);
                part.element->setAttribute(QString(), QStringLiteral("cy"), cy);
            }
        }
    }
    const QString kPicNs = QString::fromUtf16(Ooxml::Ns::PIC);
    if (auto *spPr = const_cast<XmlElement *>(drawing.descendant(kPicNs, u"spPr"))) {
        if (XmlElement *xfrm = spPr->child(QString::fromUtf16(Ooxml::Ns::A), u"xfrm")) {
            if (XmlElement *ext = xfrm->child(QString::fromUtf16(Ooxml::Ns::A), u"ext")) {
                ext->setAttribute(QString(), QStringLiteral("cx"), cx);
                ext->setAttribute(QString(), QStringLiteral("cy"), cy);
            }
        }
    }
}
const QString kA = QString::fromUtf16(Ooxml::Ns::A);
const QString kPIC = QString::fromUtf16(Ooxml::Ns::PIC);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);

QString imageExtension(const QByteArray &bytes)
{
    if (bytes.startsWith("\x89PNG")) {
        return QStringLiteral("png");
    }
    if (bytes.startsWith("\xFF\xD8")) {
        return QStringLiteral("jpeg");
    }
    if (bytes.startsWith("GIF8")) {
        return QStringLiteral("gif");
    }
    if (bytes.startsWith("BM")) {
        return QStringLiteral("bmp");
    }
    if (bytes.startsWith("II*") || bytes.startsWith("MM\0*")) {
        return QStringLiteral("tiff");
    }
    if (bytes.size() > 44 && bytes.mid(40, 4) == " EMF") {
        return QStringLiteral("emf");
    }
    if (bytes.startsWith("\xD7\xCD\xC6\x9A")) {
        return QStringLiteral("wmf");
    }
    if (bytes.contains("<svg")) {
        return QStringLiteral("svg");
    }
    return {};
}

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

XmlElementPtr BodyWriter::pictureRun(const QTextImageFormat &format)
{
    const QString name = format.name();
    QByteArray bytes = m_env.parts->media.value(name);
    QSizeF natural;
    if (bytes.isEmpty()) {
        const QVariant resource = m_document->resource(QTextDocument::ImageResource, QUrl(name));
        QImage image;
        if (resource.userType() == QMetaType::QImage) {
            image = resource.value<QImage>();
        } else if (resource.userType() == QMetaType::QPixmap) {
            image = resource.value<QPixmap>().toImage();
        } else if (resource.userType() == QMetaType::QByteArray) {
            bytes = resource.toByteArray();
        }
        if (bytes.isEmpty() && image.isNull()) {
            const QUrl url(name);
            QFile file(url.isLocalFile() ? url.toLocalFile() : name);
            if (file.open(QIODevice::ReadOnly)) {
                bytes = file.readAll();
            }
        }
        if (bytes.isEmpty() && !image.isNull()) {
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
        }
    }
    QString ext = imageExtension(bytes);
    if (bytes.isEmpty() || ext.isEmpty()) {
        return nullptr;
    }
    if (const QImage probe = QImage::fromData(bytes); !probe.isNull()) {
        natural = QSizeF(probe.width(), probe.height());
    }
    const QString media = m_env.mediaPart(bytes, ext);
    const QString rId = m_env.writer.addRelationship(m_part, QString::fromUtf16(Ooxml::RelType::Image), media);
    QSizeF size(format.width(), format.height());
    if (size.width() <= 0 && size.height() <= 0) {
        size = natural.isEmpty() ? QSizeF(96, 96) : natural;
    } else if (size.height() <= 0) {
        size.setHeight(natural.isEmpty() ? size.width() : size.width() * natural.height() / natural.width());
    } else if (size.width() <= 0) {
        size.setWidth(natural.isEmpty() ? size.height() : size.height() * natural.width() / natural.height());
    }
    const QString cx = QString::number(Units::roundToInt64(Units::pxToEmu(size.width())));
    const QString cy = QString::number(Units::roundToInt64(Units::pxToEmu(size.height())));

    auto run = std::make_shared<XmlElement>(kW, QStringLiteral("r"));
    if (XmlElementPtr rPr = runProperties(format)) {
        run->appendElement(rPr);
    }
    const QSizeF imported = format.property(kDrawingSize).toSizeF();
    XmlElementPtr drawing = Ooxml::parseFragment(format.stringProperty(kDrawingXml));
    const QString alt = format.stringProperty(kPictureAltText);
    if (drawing) {
        // The source drawing (alt text, crop, effects, anchoring) with the
        // picture re-pointed at the part written above; a resized picture
        // keeps all of it and only its extents change.
        if (qAbs(imported.width() - size.width()) >= 0.5 || qAbs(imported.height() - size.height()) >= 0.5) {
            resizeDrawing(*drawing, cx, cy);
        }
        if (auto *docPr = const_cast<XmlElement *>(drawing->descendant(kWP, u"docPr"));
            docPr != nullptr && docPr->attr(u"descr") != alt) {
            if (alt.isEmpty()) {
                docPr->removeAttribute(QString(), u"descr");
            } else {
                docPr->setAttribute(QString(), QStringLiteral("descr"), alt);
            }
        }
        if (XmlElement *blip = const_cast<XmlElement *>(drawing->descendant(kA, u"blip"))) {
            blip->removeAttribute(kR, u"embed");
            if (m_env.copier) {
                m_env.copier->rewrite(*drawing, format.stringProperty(kInlineSourcePart), m_part);
            }
            blip->setAttribute(kR, QStringLiteral("embed"), rId);
        }
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
        renumber(*drawing);
        run->appendElement(drawing);
        return run;
    }
    const int id = m_env.nextDocPr++;
    XmlElement &d = run->appendElement(kW, QStringLiteral("drawing"));
    XmlElement &inl = d.appendElement(kWP, QStringLiteral("inline"));
    for (const char *attr : {"distT", "distB", "distL", "distR"}) {
        inl.setAttribute(QString(), QString::fromLatin1(attr), QStringLiteral("0"));
    }
    inl.appendElement(kWP, QStringLiteral("extent"))
        .setAttribute(QString(), QStringLiteral("cx"), cx)
        .setAttribute(QString(), QStringLiteral("cy"), cy);
    XmlElement &effect = inl.appendElement(kWP, QStringLiteral("effectExtent"));
    for (const char *attr : {"l", "t", "r", "b"}) {
        effect.setAttribute(QString(), QString::fromLatin1(attr), QStringLiteral("0"));
    }
    XmlElement &docPr = inl.appendElement(kWP, QStringLiteral("docPr"))
                            .setAttribute(QString(), QStringLiteral("id"), QString::number(id))
                            .setAttribute(QString(), QStringLiteral("name"), QStringLiteral("Picture %1").arg(id));
    if (!alt.isEmpty()) {
        docPr.setAttribute(QString(), QStringLiteral("descr"), alt);
    }
    inl.appendElement(kWP, QStringLiteral("cNvGraphicFramePr"))
        .appendElement(kA, QStringLiteral("graphicFrameLocks"))
        .setAttribute(QString(), QStringLiteral("noChangeAspect"), QStringLiteral("1"));
    XmlElement &data = inl.appendElement(kA, QStringLiteral("graphic")).appendElement(kA, QStringLiteral("graphicData"));
    data.setAttribute(QString(), QStringLiteral("uri"), QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/picture"));
    XmlElement &pic = data.appendElement(kPIC, QStringLiteral("pic"));
    XmlElement &nv = pic.appendElement(kPIC, QStringLiteral("nvPicPr"));
    nv.appendElement(kPIC, QStringLiteral("cNvPr"))
        .setAttribute(QString(), QStringLiteral("id"), QStringLiteral("0"))
        .setAttribute(QString(), QStringLiteral("name"), media.section(QLatin1Char('/'), -1));
    nv.appendElement(kPIC, QStringLiteral("cNvPicPr"));
    XmlElement &fill = pic.appendElement(kPIC, QStringLiteral("blipFill"));
    fill.appendElement(kA, QStringLiteral("blip")).setAttribute(kR, QStringLiteral("embed"), rId);
    fill.appendElement(kA, QStringLiteral("stretch")).appendElement(kA, QStringLiteral("fillRect"));
    XmlElement &sp = pic.appendElement(kPIC, QStringLiteral("spPr"));
    XmlElement &xfrm = sp.appendElement(kA, QStringLiteral("xfrm"));
    xfrm.appendElement(kA, QStringLiteral("off"))
        .setAttribute(QString(), QStringLiteral("x"), QStringLiteral("0"))
        .setAttribute(QString(), QStringLiteral("y"), QStringLiteral("0"));
    xfrm.appendElement(kA, QStringLiteral("ext"))
        .setAttribute(QString(), QStringLiteral("cx"), cx)
        .setAttribute(QString(), QStringLiteral("cy"), cy);
    XmlElement &geom = sp.appendElement(kA, QStringLiteral("prstGeom"));
    geom.setAttribute(QString(), QStringLiteral("prst"), QStringLiteral("rect"));
    geom.appendElement(kA, QStringLiteral("avLst"));
    return run;
}

} // namespace QindaOffice::Docx::detail
