// SPDX-License-Identifier: GPL-3.0-or-later
// settings.xml of the .docx writer: the source's settings kept consistent
// with what this save wrote — the default tab stop (M70), the footnote and
// endnote options and separator references (M77), even/odd headers, and
// the mail merge (M77, docx_mailmerge.h). Split from docx_writer.cpp.

#include "docx_mailmerge.h"
#include "docx_notes.h"
#include "docx_styles.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kMain = QStringLiteral("word/document.xml");
const QString kSettings = QStringLiteral("word/settings.xml");

const QStringList &settingsOrder()
{
    // CT_Settings sequence (ECMA-376 Part 1 §17.15.1.78), abridged to the
    // elements Word writes; unknown ones keep their place.
    static const QStringList order{
        QStringLiteral("writeProtection"), QStringLiteral("view"), QStringLiteral("zoom"),
        QStringLiteral("removePersonalInformation"), QStringLiteral("removeDateAndTime"),
        QStringLiteral("doNotDisplayPageBoundaries"), QStringLiteral("displayBackgroundShape"),
        QStringLiteral("embedTrueTypeFonts"), QStringLiteral("embedSystemFonts"), QStringLiteral("saveSubsetFonts"),
        QStringLiteral("mirrorMargins"), QStringLiteral("gutterAtTop"), QStringLiteral("hideSpellingErrors"),
        QStringLiteral("hideGrammaticalErrors"), QStringLiteral("activeWritingStyle"), QStringLiteral("proofState"),
        QStringLiteral("formsDesign"), QStringLiteral("attachedTemplate"), QStringLiteral("linkStyles"),
        QStringLiteral("stylePaneFormatFilter"), QStringLiteral("stylePaneSortMethod"), QStringLiteral("documentType"),
        QStringLiteral("mailMerge"), QStringLiteral("revisionView"), QStringLiteral("trackRevisions"),
        QStringLiteral("doNotTrackMoves"), QStringLiteral("doNotTrackFormatting"), QStringLiteral("documentProtection"),
        QStringLiteral("autoFormatOverride"), QStringLiteral("styleLockTheme"), QStringLiteral("styleLockQFSet"),
        QStringLiteral("defaultTabStop"), QStringLiteral("autoHyphenation"), QStringLiteral("consecutiveHyphenLimit"),
        QStringLiteral("hyphenationZone"), QStringLiteral("doNotHyphenateCaps"), QStringLiteral("showEnvelope"),
        QStringLiteral("summaryLength"), QStringLiteral("clickAndTypeStyle"), QStringLiteral("defaultTableStyle"),
        QStringLiteral("evenAndOddHeaders"), QStringLiteral("bookFoldRevPrinting"), QStringLiteral("bookFoldPrinting"),
        QStringLiteral("bookFoldPrintingSheets"), QStringLiteral("drawingGridHorizontalSpacing"),
        QStringLiteral("drawingGridVerticalSpacing"), QStringLiteral("displayHorizontalDrawingGridEvery"),
        QStringLiteral("displayVerticalDrawingGridEvery"), QStringLiteral("doNotUseMarginsForDrawingGridOrigin"),
        QStringLiteral("drawingGridHorizontalOrigin"), QStringLiteral("drawingGridVerticalOrigin"),
        QStringLiteral("doNotShadeFormData"), QStringLiteral("noPunctuationKerning"),
        QStringLiteral("characterSpacingControl"), QStringLiteral("printTwoOnOne"),
        QStringLiteral("strictFirstAndLastChars"), QStringLiteral("noLineBreaksAfter"),
        QStringLiteral("noLineBreaksBefore"), QStringLiteral("savePreviewPicture"),
        QStringLiteral("doNotValidateAgainstSchema"), QStringLiteral("saveInvalidXml"),
        QStringLiteral("ignoreMixedContent"), QStringLiteral("alwaysShowPlaceholderText"),
        QStringLiteral("doNotDemarcateInvalidXml"), QStringLiteral("saveXmlDataOnly"),
        QStringLiteral("useXSLTWhenSaving"), QStringLiteral("saveThroughXslt"), QStringLiteral("showXMLTags"),
        QStringLiteral("alwaysMergeEmptyNamespace"), QStringLiteral("updateFields"),
        QStringLiteral("hdrShapeDefaults"), QStringLiteral("footnotePr"), QStringLiteral("endnotePr"),
        QStringLiteral("compat"), QStringLiteral("docVars"), QStringLiteral("rsids"), QStringLiteral("mathPr"),
        QStringLiteral("attachedSchema"), QStringLiteral("themeFontLang"), QStringLiteral("clrSchemeMapping"),
        QStringLiteral("doNotIncludeSubdocsInStats"), QStringLiteral("doNotAutoCompressPictures"),
        QStringLiteral("forceUpgrade"), QStringLiteral("captions"), QStringLiteral("readModeInkLockDown"),
        QStringLiteral("smartTagType"), QStringLiteral("schemaLibrary"), QStringLiteral("shapeDefaults"),
        QStringLiteral("doNotEmbedSmartTags"), QStringLiteral("decimalSymbol"), QStringLiteral("listSeparator")};
    return order;
}

void sortSettings(XmlElement &settings)
{
    const QStringList &order = settingsOrder();
    QList<std::pair<qsizetype, Ooxml::XmlChild>> ranked;
    qsizetype last = -1;
    for (const Ooxml::XmlChild &c : std::as_const(settings.children)) {
        const qsizetype at = c.element && c.element->ns == kW ? order.indexOf(c.element->name) : -1;
        last = at >= 0 ? at : last;
        ranked.append({at >= 0 ? at : last, c});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    settings.children.clear();
    for (const auto &entry : ranked) {
        settings.children.append(entry.second);
    }
}

// w:footnotePr / w:endnotePr of settings.xml: the options (when they
// changed, or for a new document), and the separator notes' ids while the
// notes part this save wrote has them.
void notePr(XmlElement &settings, bool endnote, const NoteOptions &options, bool optionsChanged, bool separators,
            bool keepSource)
{
    const QString name = endnote ? QStringLiteral("endnotePr") : QStringLiteral("footnotePr");
    const QString note = endnote ? QStringLiteral("endnote") : QStringLiteral("footnote");
    XmlElement *pr = settings.child(kW, name);
    const bool created = pr == nullptr;
    if (pr == nullptr && (separators || (optionsChanged && !(options == NoteOptions{})))) {
        pr = &settings.appendElement(kW, name);
    }
    if (pr == nullptr) {
        return;
    }
    // A kept notes part keeps the separator ids the source's settings name.
    if (!keepSource || created) {
        pr->removeChildren(kW, note);
        if (separators) {
            pr->appendElement(kW, note).setAttribute(kW, QStringLiteral("id"), QStringLiteral("-1"));
            pr->appendElement(kW, note).setAttribute(kW, QStringLiteral("id"), QStringLiteral("0"));
        }
    }
    if (optionsChanged) {
        writeNoteOptions(*pr, options);
    }
    if (pr->children.isEmpty()) {
        settings.removeChildren(kW, name);
    }
}

} // namespace

void writeSettings(WriteEnv &env, const XmlElementPtr &sourceSettings, const QString &sourceSettingsPart,
                   const NotesWritten &notes)
{
    const WordParts &parts = *env.parts;
    XmlElementPtr settings = sourceSettings ? sourceSettings->clone() : nullptr;
    if (!settings) {
        settings = std::make_shared<XmlElement>(kW, QStringLiteral("settings"));
        settings->appendElement(kW, QStringLiteral("zoom")).setAttribute(kW, QStringLiteral("percent"), QStringLiteral("100"));
        setChildVal(*settings, QStringLiteral("defaultTabStop"), twips(parts.defaultTabStop));
        setChildVal(*settings, QStringLiteral("characterSpacingControl"), QStringLiteral("doNotCompress"));
        XmlElement &compat = settings->appendElement(kW, QStringLiteral("compat"));
        XmlElement &mode = compat.appendElement(kW, QStringLiteral("compatSetting"));
        mode.setAttribute(kW, QStringLiteral("name"), QStringLiteral("compatibilityMode"));
        mode.setAttribute(kW, QStringLiteral("uri"), QStringLiteral("http://schemas.microsoft.com/office/word"));
        mode.setAttribute(kW, QStringLiteral("val"), QStringLiteral("15"));
    } else if (env.copier) {
        env.copier->rewrite(*settings, sourceSettingsPart, kSettings);
    }
    // Format ▸ Tabs ▸ Default tab stops (M70): kept as read unless changed.
    if (const QString tab = childVal(settings.get(), u"defaultTabStop");
        tab.isEmpty()
        || qAbs(Ooxml::Units::twipsToPx(Ooxml::Units::parseTwips(tab, 720)) - parts.defaultTabStop) > 0.5) {
        setChildVal(*settings, QStringLiteral("defaultTabStop"), twips(parts.defaultTabStop));
    }
    const bool fresh = !sourceSettings;
    notePr(*settings, false, parts.footnoteOptions,
           fresh || !(parts.footnoteOptions == parts.importedFootnoteOptions), notes.footnotes, false);
    notePr(*settings, true, parts.endnoteOptions, fresh || !(parts.endnoteOptions == parts.importedEndnoteOptions),
           notes.endnotes, notes.endnotes && env.keepEndnotesPart);
    settings->removeChildren(kW, u"evenAndOddHeaders");
    if (parts.differentOddEven) {
        settings->appendElement(kW, QStringLiteral("evenAndOddHeaders"));
    }
    // The mail merge: the source's own w:mailMerge while the settings are
    // as read; rebuilt from the model when they changed.
    if (!(parts.mailMerge == parts.importedMailMerge)) {
        const XmlElement *old = settings->child(kW, u"mailMerge");
        const XmlElementPtr kept = old != nullptr ? old->clone() : nullptr;
        writeMailMerge(*settings, parts.mailMerge, kept.get(), env.writer, kSettings);
    }
    sortSettings(*settings);
    markIgnorable(*settings);
    env.writer.addXmlPart(kSettings, *settings, wordprocessingType("settings+xml"));
    env.writer.addRelationship(kMain, QString::fromUtf16(Ooxml::RelType::Settings), kSettings);
}

void patchSectionNoteOptions(const WordParts &parts, XmlElement &sectPr, bool closing)
{
    for (const bool endnote : {false, true}) {
        const NoteOptions &options = endnote ? parts.endnoteOptions : parts.footnoteOptions;
        const NoteOptions &read = endnote ? parts.importedEndnoteOptions : parts.importedFootnoteOptions;
        if (options == read) {
            continue;
        }
        const QString name = endnote ? QStringLiteral("endnotePr") : QStringLiteral("footnotePr");
        XmlElement *pr = sectPr.child(kW, name);
        if (pr == nullptr && closing) {
            pr = &sectPr.appendElement(kW, name);
        }
        if (pr != nullptr) {
            writeNoteOptions(*pr, options);
            if (pr->children.isEmpty()) {
                sectPr.removeChildren(kW, name);
            }
        }
    }
}

} // namespace QindaOffice::Docx::detail
