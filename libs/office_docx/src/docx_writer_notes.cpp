// SPDX-License-Identifier: GPL-3.0-or-later
// Footnotes and endnotes of the .docx writer: the notes the body references,
// in reference order, each read note from its own markup while its text is
// unchanged. An endnotes part whose notes are all still referenced and
// unchanged is copied byte for byte (M77, D-086). Split from docx_writer.cpp.

#include "docx_properties.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"

#include <QTextBlock>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kMain = QStringLiteral("word/document.xml");

XmlElementPtr separatorNote(const QString &element, const QString &kind, const QString &id)
{
    auto note = std::make_shared<XmlElement>(kW, element);
    note->setAttribute(kW, QStringLiteral("type"), kind);
    note->setAttribute(kW, QStringLiteral("id"), id);
    XmlElement &p = note->appendElement(kW, QStringLiteral("p"));
    XmlElement &spacing = p.appendElement(kW, QStringLiteral("pPr")).appendElement(kW, QStringLiteral("spacing"));
    spacing.setAttribute(kW, QStringLiteral("after"), QStringLiteral("0"));
    spacing.setAttribute(kW, QStringLiteral("line"), QStringLiteral("240"));
    spacing.setAttribute(kW, QStringLiteral("lineRule"), QStringLiteral("auto"));
    p.appendElement(kW, QStringLiteral("r")).appendElement(kW, kind);
    return note;
}

// The note ids the body's reference marks carry, in text order.
QStringList referencedNotes(const QTextDocument &body, int property)
{
    QStringList ids;
    for (QTextBlock block = body.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QString id = it.fragment().charFormat().stringProperty(property);
            if (!id.isEmpty() && !ids.contains(id)) {
                ids.append(id);
            }
        }
    }
    return ids;
}

// One notes part (footnotes.xml or endnotes.xml) for the referenced notes.
void writeNotesPart(WriteEnv &env, bool endnote)
{
    const WordParts &parts = *env.parts;
    const QString element = endnote ? QStringLiteral("endnote") : QStringLiteral("footnote");
    const QString partName = endnote ? QStringLiteral("word/endnotes.xml") : QStringLiteral("word/footnotes.xml");
    const QString textStyle = endnote ? QStringLiteral("EndnoteText") : QStringLiteral("FootnoteText");
    const QString refStyle = endnote ? QStringLiteral("EndnoteReference") : QStringLiteral("FootnoteReference");
    const QHash<QString, QString> &texts = endnote ? parts.endnotes : parts.footnotes;
    const QHash<QString, QString> &importedText = endnote ? parts.endnoteImportedText : parts.footnoteImportedText;
    const QHash<QString, QString> &markup = endnote ? parts.endnoteXml : parts.footnoteXml;
    const QString &sourcePart = endnote ? env.sourceEndnotesPart : env.sourceFootnotesPart;
    XmlElement notes(kW, endnote ? QStringLiteral("endnotes") : QStringLiteral("footnotes"));
    notes.appendElement(separatorNote(element, QStringLiteral("separator"), QStringLiteral("-1")));
    notes.appendElement(separatorNote(element, QStringLiteral("continuationSeparator"), QStringLiteral("0")));
    for (const QString &noteId : std::as_const(endnote ? env.endnoteOrder : env.footnoteOrder)) {
        const QString id = QString::number((endnote ? env.endnoteIds : env.footnoteIds).value(noteId));
        const QString text = texts.value(noteId);
        XmlElementPtr preserved = importedText.contains(noteId) && importedText.value(noteId) == text
            ? Ooxml::parseFragment(markup.value(noteId))
            : nullptr;
        if (preserved) {
            preserved->setAttribute(kW, QStringLiteral("id"), id);
            if (env.copier && !sourcePart.isEmpty()) {
                env.copier->rewrite(*preserved, sourcePart, partName);
            }
            notes.appendElement(preserved);
            continue;
        }
        XmlElement &note = notes.appendElement(kW, element);
        note.setAttribute(kW, QStringLiteral("id"), id);
        XmlElement &p = note.appendElement(kW, QStringLiteral("p"));
        setChildVal(p.appendElement(kW, QStringLiteral("pPr")), QStringLiteral("pStyle"), textStyle);
        XmlElement &ref = p.appendElement(kW, QStringLiteral("r"));
        setChildVal(ref.appendElement(kW, QStringLiteral("rPr")), QStringLiteral("rStyle"), refStyle);
        ref.appendElement(kW, endnote ? QStringLiteral("endnoteRef") : QStringLiteral("footnoteRef"));
        XmlElement &t = p.appendElement(kW, QStringLiteral("r")).appendElement(kW, QStringLiteral("t"));
        t.setAttribute(QString::fromUtf16(Ooxml::Ns::XML), QStringLiteral("space"), QStringLiteral("preserve"));
        t.appendText(QLatin1Char(' ') + text);
        env.usedStyles.insert(textStyle);
        env.usedStyles.insert(refStyle);
    }
    markIgnorable(notes);
    env.writer.addXmlPart(partName, notes, wordprocessingType(endnote ? "endnotes+xml" : "footnotes+xml"));
    env.writer.addRelationship(
        kMain, QString::fromUtf16(endnote ? Ooxml::RelType::Endnotes : Ooxml::RelType::Footnotes), partName);
}

} // namespace

void prepareEndnotes(WriteEnv &env, const QTextDocument &body)
{
    const WordParts &parts = *env.parts;
    env.keepEndnotesPart = false;
    if (env.sourceEndnotesPart.isEmpty() || !env.copier) {
        return;
    }
    const QStringList referenced = referencedNotes(body, kEndnoteIdProperty);
    if (referenced.isEmpty() || referenced.size() != parts.endnoteImportedText.size()) {
        return;
    }
    for (const QString &id : referenced) {
        if (!id.startsWith(u"docx-") || !parts.endnoteImportedText.contains(id)
            || parts.endnotes.value(id) != parts.endnoteImportedText.value(id)) {
            return;
        }
    }
    env.keepEndnotesPart = true;
}

NotesWritten writeNotes(WriteEnv &env)
{
    NotesWritten written;
    if (!env.footnoteOrder.isEmpty()) {
        writeNotesPart(env, false);
        written.footnotes = true;
    }
    if (env.keepEndnotesPart && !env.endnoteOrder.isEmpty()) {
        // Every note as read: the part itself, byte for byte.
        env.writer.addRelationship(kMain, QString::fromUtf16(Ooxml::RelType::Endnotes),
                                   env.copier->copyPart(env.sourceEndnotesPart));
        written.endnotes = true;
    } else if (!env.endnoteOrder.isEmpty()) {
        writeNotesPart(env, true);
        written.endnotes = true;
    }
    return written;
}

} // namespace QindaOffice::Docx::detail
