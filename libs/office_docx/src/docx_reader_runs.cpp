// SPDX-License-Identifier: GPL-3.0-or-later
// Runs of a .docx paragraph: text, fields (simple and complex, merge fields
// with their nested codes), hyperlinks, tracked changes, symbols, breaks,
// footnote and endnote markers, preserved objects.
// Split from docx_reader_body.cpp (paragraphs, tables entry, lists).


#include "docx_float.h"
#include "docx_reference_fields.h"
#include "docx_format.h"
#include "docx_merge_fields.h"
#include "docx_notes.h"
#include "docx_properties.h"
#include "docx_reader_p.h"
#include "docx_text.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextTable>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kM = QString::fromUtf16(Ooxml::Ns::M);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);
const QString kMC = QString::fromUtf16(Ooxml::Ns::MC);

// Fields whose result runs keep the field on save; others (TOC,
// HYPERLINK, form fields, an IF without merge fields) are read as their
// plain result. Mail merge fields (M77) are kept with their nested codes.
bool keepsField(const QString &instruction)
{
    static const QStringList kTypes{
        QStringLiteral("PAGE"), QStringLiteral("NUMPAGES"), QStringLiteral("SECTIONPAGES"),
        QStringLiteral("DATE"), QStringLiteral("TIME"), QStringLiteral("CREATEDATE"),
        QStringLiteral("SAVEDATE"), QStringLiteral("PRINTDATE"), QStringLiteral("AUTHOR"),
        QStringLiteral("TITLE"), QStringLiteral("SUBJECT"), QStringLiteral("FILENAME"),
        QStringLiteral("MERGEFIELD"), QStringLiteral("REF"), QStringLiteral("PAGEREF"),
        QStringLiteral("NOTEREF"), QStringLiteral("SEQ"), QStringLiteral("DOCPROPERTY"),
        QStringLiteral("NUMWORDS"), QStringLiteral("NUMCHARS"), QStringLiteral("USERNAME"),
        QStringLiteral("STYLEREF"), QStringLiteral("LASTSAVEDBY"),
    };
    return kTypes.contains(instruction.trimmed().section(QLatin1Char(' '), 0, 0).toUpper()) || isMergeField(instruction);
}

QString fieldType(const QString &instruction)
{
    return instruction.trimmed().section(QLatin1Char(' '), 0, 0).toUpper();
}

// HYPERLINK "url" \l "anchor" → url#anchor
QString hyperlinkTarget(const QString &instruction)
{
    QStringList quoted;
    QString current;
    bool inQuote = false;
    bool anchorNext = false;
    QString url;
    QString anchor;
    const QStringList tokens = instruction.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (qsizetype i = 1; i < tokens.size(); ++i) {
        QString token = tokens.at(i);
        if (inQuote) {
            current += QLatin1Char(' ') + token;
            if (token.endsWith(QLatin1Char('"'))) {
                inQuote = false;
                token = current;
            } else {
                continue;
            }
        } else if (token.startsWith(QLatin1Char('"')) && (token.size() == 1 || !token.endsWith(QLatin1Char('"')))) {
            inQuote = true;
            current = token;
            continue;
        }
        if (token == u"\\l") {
            anchorNext = true;
            continue;
        }
        if (token.startsWith(QLatin1Char('\\'))) {
            anchorNext = false;
            ++i; // switch argument (\o "tip", \t "frame")
            continue;
        }
        const QString value = token.remove(QLatin1Char('"'));
        if (anchorNext) {
            anchor = value;
            anchorNext = false;
        } else if (url.isEmpty()) {
            url = value;
        }
    }
    return anchor.isEmpty() ? url : url + QLatin1Char('#') + anchor;
}

// Private-use glyphs Word stores for Symbol / Wingdings characters.
QString symbolText(const XmlElement &sym)
{
    const QString font = wAttr(&sym, u"font").toLower();
    bool ok = false;
    uint code = wAttr(&sym, u"char").toUInt(&ok, 16);
    if (!ok) {
        return {};
    }
    if (code >= 0xF000) {
        code -= 0xF000;
    }
    if (font.contains(u"wingdings")) {
        switch (code) {
        case 0xFC: return QStringLiteral("✓");
        case 0xFB: return QStringLiteral("✗");
        case 0xFE: return QStringLiteral("☒");
        case 0xA8: return QStringLiteral("☐");
        case 0x6F: return QStringLiteral("☐");
        case 0x78: return QStringLiteral("☒");
        case 0x9F: case 0x6C: return QStringLiteral("●");
        case 0xA7: case 0x6E: return QStringLiteral("■");
        case 0xD8: return QStringLiteral("➢");
        case 0xE0: return QStringLiteral("→");
        default: break;
        }
    }
    if (font.contains(u"symbol") && code == 0xB7) {
        return QStringLiteral("•");
    }
    return QString(QChar(char16_t(code)));
}

} // namespace

QString BodyReader::currentHref(const InlineState &inl, const ContainerState &state) const
{
    if (!inl.href.isEmpty()) {
        return inl.href;
    }
    const auto referenceHref = [](const QString &code) {
        const auto tokens = fieldTokens(code); const QString type = tokens.value(0).toUpper();
        return (type == u"REF" || type == u"PAGEREF") && tokens.contains(QStringLiteral("\\h"))
            ? QLatin1Char('#') + tokens.value(1) : QString();
    };
    for (auto it = inl.simpleFields.crbegin(); it != inl.simpleFields.crend(); ++it) if (const QString href = referenceHref(*it); !href.isEmpty()) return href;
    for (auto it = state.fields.crbegin(); it != state.fields.crend(); ++it) {
        if (it->inResult) if (const QString href = referenceHref(it->instruction); !href.isEmpty()) return href;
        if (it->inResult && fieldType(it->instruction) == u"HYPERLINK") {
            return hyperlinkTarget(it->instruction);
        }
    }
    return {};
}

QString BodyReader::currentField(const InlineState &inl, const ContainerState &state) const
{
    if (!inl.simpleFields.isEmpty()) {
        // Trimmed: the writer puts Word's own spaces back around it.
        const QString instr = inl.simpleFields.last().trimmed();
        return keepsField(instr) ? instr : QString();
    }
    if (!state.fields.isEmpty() && state.fields.last().inResult) {
        // A merge IF owns its whole result, nested merge fields' results too.
        for (const ContainerState::Field &f : state.fields) {
            if (f.inResult && mergeFieldKind(f.instruction) == MergeFieldKind::If) {
                return f.instruction.trimmed();
            }
        }
        const QString instr = state.fields.last().instruction.trimmed();
        return keepsField(instr) ? instr : QString();
    }
    return {};
}

bool BodyReader::inFieldInstruction(const ContainerState &state) const
{
    for (const ContainerState::Field &f : state.fields) {
        if (!f.inResult) {
            return true;
        }
    }
    return false;
}

void BodyReader::readInline(const XmlElement &parent, QTextCursor &cursor, InlineState &inl, ContainerState &state)
{
    for (const XmlElement *e : parent.elements()) {
        if (e->ns == kM && (e->name == u"oMath" || e->name == u"oMathPara")) {
            readMath(*e, cursor, inl, state);
            continue;
        }
        if (e->ns != kW) {
            continue;
        }
        const QStringView n = e->name;
        if (n == u"r") {
            readRun(*e, cursor, inl, state);
        } else if (n == u"hyperlink") {
            const QString saved = inl.href;
            QString target;
            if (const QString id = e->attribute(kR, u"id"); !id.isEmpty()) {
                if (const auto rel = m_env.package->relationship(m_part, id)) {
                    target = rel->target;
                }
            }
            if (const QString anchor = wAttr(e, u"anchor"); !anchor.isEmpty()) {
                target += QLatin1Char('#') + anchor;
            }
            inl.href = target;
            readInline(*e, cursor, inl, state);
            inl.href = saved;
        } else if (n == u"fldSimple") {
            const QString instr = wAttr(e, u"instr");
            const QString saved = inl.href;
            if (fieldType(instr) == u"HYPERLINK") {
                inl.href = hyperlinkTarget(instr);
            }
            inl.simpleFields.append(instr);
            auto reference = referenceField(instr);
            if (const QString lock = wAttr(e, u"fldLock"); !lock.isEmpty()) {
                Ooxml::XmlElement begin(kW, QStringLiteral("fldChar"));
                begin.setAttribute(kW, QStringLiteral("fldCharType"), QStringLiteral("begin")); begin.setAttribute(kW, QStringLiteral("fldLock"), lock);
                reference.insert(QStringLiteral("beginXml"), Ooxml::serializeFragment(begin));
            }
            inl.simpleReferences.append(reference);
            const int resultStart = cursor.position();
            readInline(*e, cursor, inl, state);
            if (cursor.position() == resultStart && !isMergeField(instr) && fieldType(instr) != u"HYPERLINK") {
                QTextCharFormat empty; empty.setProperty(kEmptyReference, true); insertText(cursor, QString(QChar(0x200B)), empty, inl, state);
            }
            inl.simpleFields.removeLast();
            inl.simpleReferences.removeLast();
            inl.href = saved;
        } else if (n == u"ins" || n == u"moveTo") {
            const QString saved = inl.insertAuthor;
            inl.insertAuthor = wAttr(e, u"author", QStringLiteral("Unknown"));
            inl.revisionDate = QDateTime::fromString(wAttr(e, u"date"), Qt::ISODate);
            readInline(*e, cursor, inl, state);
            inl.insertAuthor = saved;
        } else if (n == u"del" || n == u"moveFrom") {
            const QString saved = inl.deleteAuthor;
            inl.deleteAuthor = wAttr(e, u"author", QStringLiteral("Unknown"));
            inl.revisionDate = QDateTime::fromString(wAttr(e, u"date"), Qt::ISODate);
            readInline(*e, cursor, inl, state);
            inl.deleteAuthor = saved;
        } else if (n == u"sdt") {
            // An inline content control: its runs carry its key (docx_controls.h).
            if (const XmlElement *content = e->child(kW, u"sdtContent")) {
                m_inlineControls.append(openControl(*e, false));
                readInline(*content, cursor, inl, state);
                m_inlineControls.removeLast();
            }
        } else if (n == u"smartTag" || n == u"customXml" || n == u"dir" || n == u"bdo") {
            readInline(*e, cursor, inl, state);
        } else if (n == u"commentRangeStart" || n == u"commentRangeEnd") {
            commentRange(*e);
        } else if (n == u"bookmarkStart" || n == u"bookmarkEnd") {
            bookmarkRange(*e, state);
        }
        // pPr, proofErr, bookmarkEnd, permStart/End: nothing to show.
    }
}

void BodyReader::readFieldChar(const XmlElement &fldChar, QTextCursor &cursor, const QTextCharFormat &format,
                               InlineState &inl, ContainerState &state)
{
    const QString type = wAttr(&fldChar, u"fldCharType");
    if (type == u"begin") {
        ContainerState::Field field;
        field.nested = inFieldInstruction(state);
        field.referenceId = referenceField({}).value(QStringLiteral("id")).toString();
        field.beginXml = Ooxml::serializeFragment(fldChar);
        state.fields.append(field);
    } else if (type == u"separate") {
        if (!state.fields.isEmpty()) {
            state.fields.last().inResult = true;
        }
    } else if (type == u"end") {
        if (state.fields.isEmpty()) {
            return;
        }
        const ContainerState::Field field = state.fields.takeLast();
        const QString code = field.instruction.trimmed();
        if (field.nested && !state.fields.isEmpty() && !state.fields.last().inResult) {
            state.fields.last().instruction += QStringLiteral("{ ") + code + QStringLiteral(" }");
            return;
        }
        if (!field.produced && !inFieldInstruction(state) && !code.isEmpty() && !isMergeField(code) && fieldType(code) != u"HYPERLINK") {
            QTextCharFormat empty = format; empty.setProperty(kEmptyReference, true);
            inl.simpleReferences.append(QVariantMap{{QStringLiteral("id"), field.referenceId}, {QStringLiteral("code"), code}, {QStringLiteral("beginXml"), field.beginXml}});
            insertText(cursor, QString(QChar(0x200B)), empty, inl, state); inl.simpleReferences.removeLast();
        }
        // A merge rule Word writes without a result (NEXT, NEXTIF, SKIPIF),
        // or a merge field whose result is empty, shows Word's label.
        if (!field.produced && !inFieldInstruction(state) && isMergeField(code)) {
            QTextCharFormat label = format;
            label.setProperty(kFieldInstruction, code);
            insertText(cursor, mergeFieldLabel(code), label, inl, state);
        }
    }
}

void BodyReader::readRun(const XmlElement &r, QTextCursor &cursor, InlineState &inl, ContainerState &state)
{
    const XmlElement *rPr = r.child(kW, u"rPr");
    const QString rStyle = childVal(rPr, u"rStyle");
    XmlElementPtr effective = mergeProperties(inl.paragraphRunProperties.get(),
                                              m_env.styles.characterRunProperties(rStyle).get());
    effective = mergeProperties(effective.get(), rPr);
    QTextCharFormat cf = charFormatFromRPr(effective.get(), m_env.theme);
    if (!rStyle.isEmpty()) {
        cf.setProperty(kRunStyleId, rStyle);
    }
    if (rPr != nullptr) {
        cf.setProperty(kRunXml, Ooxml::serializeFragment(*rPr));
    }
    for (const XmlElement *e : r.elements()) {
        if (e->ns == kMC && e->name == u"AlternateContent") {
            readObject(r, *e, cursor, cf, inl, state);
            continue;
        }
        if (e->ns != kW) {
            continue;
        }
        const QStringView n = e->name;
        if (n == u"fldChar") {
            readFieldChar(*e, cursor, cf, inl, state);
        } else if (n == u"instrText" || n == u"delInstrText") {
            if (!state.fields.isEmpty() && !state.fields.last().inResult) {
                state.fields.last().instruction += e->text();
            }
        } else if ((n == u"br" || n == u"cr") && !state.fields.isEmpty() && !state.fields.last().inResult) {
            state.fields.last().instruction += QLatin1Char('\n'); // a line break in a code (Address Block)
        } else if (inFieldInstruction(state)) {
            continue; // everything between begin and separate is the code
        } else if (n == u"t" || n == u"delText") {
            insertText(cursor, e->text(), cf, inl, state);
        } else if (n == u"tab" || n == u"ptab") {
            insertText(cursor, QStringLiteral("\t"), cf, inl, state);
        } else if (n == u"br") {
            const QString type = wAttr(e, u"type");
            if (type == u"page") {
                pageBreak(cursor, inl, state);
            } else {
                insertText(cursor, QString(QChar::LineSeparator), cf, inl, state);
            }
        } else if (n == u"cr") {
            insertText(cursor, QString(QChar::LineSeparator), cf, inl, state);
        } else if (n == u"noBreakHyphen") {
            insertText(cursor, QString(QChar(0x2011)), cf, inl, state);
        } else if (n == u"softHyphen") {
            insertText(cursor, QString(QChar(0x00AD)), cf, inl, state);
        } else if (n == u"sym") {
            insertText(cursor, symbolText(*e), cf, inl, state);
        } else if (n == u"pgNum") {
            QTextCharFormat field = cf;
            field.setProperty(kFieldInstruction, QStringLiteral("PAGE"));
            insertText(cursor, QStringLiteral("1"), field, inl, state);
        } else if (n == u"footnoteReference") {
            footnoteMarker(cursor, wAttr(e, u"id"), cf, inl, state);
        } else if (n == u"endnoteReference") {
            endnoteMarker(cursor, wAttr(e, u"id"), cf, inl, state);
        } else if (n == u"commentReference") {
            // The comment's mark (docx_comments.h): a zero-width space the
            // writer turns back into this run.
            QTextCharFormat mark = cf;
            mark.setProperty(kCommentReference, wAttr(e, u"id"));
            insertText(cursor, QString(QChar(0x200B)), mark, inl, state);
        } else if (n == u"ruby") {
            if (const XmlElement *base = e->child(kW, u"rubyBase")) {
                readInline(*base, cursor, inl, state);
            }
        } else if (n == u"drawing" || n == u"pict" || n == u"object" || n == u"contentPart") {
            readObject(r, *e, cursor, cf, inl, state);
        }
        // footnoteRef/endnoteRef (inside notes), separators,
        // lastRenderedPageBreak, annotationRef: nothing to show.
    }
}

QTextCharFormat BodyReader::prepareFormat(QTextCharFormat format, InlineState &inl, ContainerState &state)
{
    const QString href = currentHref(inl, state);
    if (!href.isEmpty()) {
        format.setAnchor(true);
        format.setAnchorHref(href);
    }
    if (!state.pendingAnchors.isEmpty()) {
        format.setAnchorNames(state.pendingAnchors);
        state.pendingAnchors.clear();
    }
    prepareReferences(format, inl, state);
    const QString field = currentField(inl, state);
    if (!field.isEmpty()) {
        format.setProperty(kFieldInstruction, field);
    }
    if (!state.fields.isEmpty() && state.fields.last().inResult) {
        state.fields.last().produced = true;
    }
    if (!m_openComments.isEmpty() && !format.hasProperty(kCommentReference)) {
        format.setProperty(kCommentIds, m_openComments);
    }
    if (!m_inlineControls.isEmpty()) {
        format.setProperty(kContentControls, m_inlineControls);
    }
    // Tracked changes look the way QindaWrite shows its own
    // (write_revisions.cpp): set before the snapshot, so the marks never
    // reach the file as real formatting.
    if (!inl.insertAuthor.isEmpty()) {
        format.setProperty(kRevisionInsertProperty, inl.insertAuthor);
        format.setFontUnderline(true);
        format.setForeground(QColor::fromRgb(kRevisionInsertColor));
    }
    if (!inl.deleteAuthor.isEmpty()) {
        format.setProperty(kRevisionDeleteProperty, inl.deleteAuthor);
        format.setFontStrikeOut(true);
        format.setForeground(QColor::fromRgb(kRevisionDeleteColor));
    }
    if ((!inl.insertAuthor.isEmpty() || !inl.deleteAuthor.isEmpty()) && inl.revisionDate.isValid()) {
        format.setProperty(kRevisionDate, inl.revisionDate);
    }
    if (!format.hasProperty(kRunSnapshot)) {
        format.setProperty(kRunSnapshot, charSnapshot(format));
    }
    return format;
}

void BodyReader::insertText(QTextCursor &cursor, const QString &text, QTextCharFormat format, InlineState &inl,
                            ContainerState &state)
{
    if (text.isEmpty()) {
        return;
    }
    cursor.insertText(text, prepareFormat(std::move(format), inl, state));
    inl.blockHasContent = true;
}

void BodyReader::insertObject(QTextCursor &cursor, QTextCharFormat format, InlineState &inl, ContainerState &state)
{
    insertText(cursor, QString(QChar::ObjectReplacementCharacter), std::move(format), inl, state);
}

void BodyReader::pageBreak(QTextCursor &cursor, InlineState &inl, ContainerState &state)
{
    if (!inl.blockHasContent) {
        QTextBlockFormat f = cursor.blockFormat();
        f.setPageBreakPolicy(f.pageBreakPolicy() | QTextFormat::PageBreak_AlwaysBefore);
        f.setProperty(kPageBreakRun, true);
        cursor.setBlockFormat(f);
        return;
    }
    // Mid-paragraph: what follows starts a new page, in a block with the
    // same paragraph formatting.
    QTextBlockFormat f = inl.blockFormat;
    f.setTopMargin(0);
    f.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    f.setProperty(kPageBreakRun, true);
    // AGENT-NOTE (M88): a section break stays on both parts; of the two the
    // LAST ends the section (sectionEnds, docx_sections.h), where Word's
    // paragraph mark with the w:sectPr is.
    f.setProperty(kParagraphSnapshot, blockSnapshot(f));
    cursor.insertBlock(f, inl.markFormat);
    inl.blockHasContent = false;
    Q_UNUSED(state);
}

void BodyReader::footnoteMarker(QTextCursor &cursor, const QString &id, QTextCharFormat format, InlineState &inl,
                                ContainerState &state)
{
    const QString noteId = QStringLiteral("docx-") + id;
    format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    format.setProperty(kFootnoteIdProperty, noteId);
    WordParts &parts = *m_env.parts;
    const QString text = m_env.footnoteText.value(id);
    parts.footnotes.insert(noteId, text);
    parts.footnoteImportedText.insert(noteId, text);
    const NoteOptions &options = parts.footnoteOptions;
    insertText(cursor, noteNumber(options.start + m_footnoteNumber++, noteFormat(options, false)), format, inl, state);
}

void BodyReader::endnoteMarker(QTextCursor &cursor, const QString &id, QTextCharFormat format, InlineState &inl,
                               ContainerState &state)
{
    const QString noteId = QStringLiteral("docx-") + id;
    format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    format.setProperty(kEndnoteIdProperty, noteId);
    WordParts &parts = *m_env.parts;
    const QString text = m_env.endnoteText.value(id);
    parts.endnotes.insert(noteId, text);
    parts.endnoteImportedText.insert(noteId, text);
    const NoteOptions &options = parts.endnoteOptions;
    insertText(cursor, noteNumber(options.start + m_endnoteNumber++, noteFormat(options, true)), format, inl, state);
}

void BodyReader::commentRange(const XmlElement &marker)
{
    const QString id = wAttr(&marker, u"id");
    if (marker.name == u"commentRangeStart") {
        if (!id.isEmpty() && !m_openComments.contains(id)) {
            m_openComments.append(id);
        }
    } else {
        m_openComments.removeAll(id);
    }
}

void BodyReader::preservedText(QTextCursor &cursor, const XmlElement &element, const QString &display,
                               QTextCharFormat format, InlineState &inl, ContainerState &state)
{
    format.setProperty(kInlineXml, Ooxml::serializeFragment(element));
    format.setProperty(kInlineSourcePart, m_part);
    format.setProperty(kInlineSerial, m_env.nextIsland++);
    format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    insertText(cursor, display, format, inl, state);
}

} // namespace QindaOffice::Docx::detail
