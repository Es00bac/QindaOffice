// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_reader.h"
#include <QScopeGuard>

#include "docx_comments.h"
#include "docx_controls.h"
#include "docx_format.h"
#include "docx_list_labels.h"
#include "docx_mailmerge.h"
#include "docx_metrics.h"
#include "docx_notes.h"
#include "docx_properties.h"
#include "docx_reader_p.h"
#include "docx_sections.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextOption>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);

XmlElementPtr relatedXml(const Ooxml::Package &package, const QString &source, const char16_t *type,
                         QString *partName = nullptr)
{
    const auto rel = package.firstOfType(source, QString::fromUtf16(type));
    if (!rel) {
        return nullptr;
    }
    const QString part = package.targetPart(source, *rel);
    if (partName != nullptr) {
        *partName = part;
    }
    return part.isEmpty() ? nullptr : package.xml(part);
}

QString describe(const QMap<QString, int> &approximations)
{
    QStringList parts;
    for (auto it = approximations.cbegin(); it != approximations.cend(); ++it) {
        parts.append(it.value() == 1 ? QStringLiteral("1 %1").arg(it.key())
                                     : QStringLiteral("%1 %2s").arg(it.value()).arg(it.key()));
    }
    return parts.join(QStringLiteral(", "));
}

// AGENT-NOTE: QTextDocumentLayout drops the top margin of a frame's first
// block (probed on Qt 6.11: a 160 px top margin on block one moved nothing),
// so a Word cover whose title sits 2 inches down started at the margin. A
// synthetic, practically zero-height empty block in front makes the title
// an ordinary block whose top margin Qt applies; the writer skips the
// spacer (kParagraphSynthetic) and the title keeps its own spacing markup.
void insertLeadingSpace(QTextDocument *body)
{
    const QTextBlock first = body->begin();
    const qreal gap = first.blockFormat().topMargin();
    if (!first.isValid() || gap <= 0.5 || first.blockFormat().boolProperty(kParagraphSynthetic)) {
        return;
    }
    QTextCursor cursor(first);
    const QTextBlockFormat original = first.blockFormat();
    const QTextCharFormat mark = first.charFormat();
    cursor.insertBlock(original, mark); // the text moves on; block one is now empty
    QTextBlockFormat spacer;
    spacer.setLineHeight(0.01, QTextBlockFormat::FixedHeight);
    spacer.setProperty(kParagraphSynthetic, true);
    QTextCursor head(body->begin());
    head.setBlockFormat(spacer);
    QTextCharFormat tiny;
    tiny.setFontPointSize(1);
    head.setBlockCharFormat(tiny);
}

} // namespace

bool isWordPackage(const Ooxml::Package &package)
{
    const QString main = package.mainPart();
    if (main.isEmpty()) {
        return false;
    }
    const QString type = package.contentType(main);
    return type.contains(u"wordprocessingml") || type.contains(u"ms-word");
}

bool readDocx(const QString &path, QTextDocument *body, WordParts *parts, QString *error)
{
    QString loadError;
    const auto package = Ooxml::Package::load(path, &loadError);
    if (!package) {
        if (error != nullptr) {
            *error = loadError;
        }
        return false;
    }
    return readDocx(package, body, parts, error);
}

bool readDocx(std::shared_ptr<const Ooxml::Package> package, QTextDocument *body, WordParts *parts, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!package || !isWordPackage(*package)) {
        return fail(QStringLiteral("not a Word document"));
    }
    const QString main = package->mainPart();
    QString xmlError;
    const XmlElementPtr root = package->xml(main, &xmlError);
    const XmlElement *bodyElement = root ? root->child(kW, u"body") : nullptr;
    if (bodyElement == nullptr) {
        return fail(xmlError.isEmpty() ? QStringLiteral("the document has no body") : xmlError);
    }

    *parts = WordParts();
    parts->source = package;
    parts->properties = Ooxml::readCoreProperties(*package);

    detail::ReadEnv env;
    env.package = package;
    env.parts = parts;
    const XmlElementPtr stylesRoot = relatedXml(*package, main, Ooxml::RelType::Styles);
    env.styles = StyleSheet::parse(stylesRoot.get());
    const XmlElementPtr numberingRoot = relatedXml(*package, main, Ooxml::RelType::Numbering);
    env.numbering = Numbering::parse(numberingRoot.get());
    if (const XmlElementPtr themeRoot = relatedXml(*package, main, Ooxml::RelType::Theme)) {
        env.theme = Ooxml::parseTheme(*themeRoot);
    }
    QString settingsPart;
    const XmlElementPtr settings = relatedXml(*package, main, Ooxml::RelType::Settings, &settingsPart);
    if (settings) {
        const QString tab = childVal(settings.get(), u"defaultTabStop");
        if (!tab.isEmpty() && Units::parseTwips(tab) > 0) {
            parts->defaultTabStop = Units::twipsToPx(Units::parseTwips(tab));
        }
        parts->differentOddEven = onOff(settings.get(), u"evenAndOddHeaders");
    }
    // Footnote and endnote options (M77): the document's, then the closing
    // section's own (Word lets a section's win); merge settings.
    const XmlElement *closingSection = bodyElement->child(kW, u"sectPr");
    for (const bool endnote : {false, true}) {
        const QStringView name = endnote ? u"endnotePr" : u"footnotePr";
        NoteOptions options = readNoteOptions(settings ? settings->child(kW, name) : nullptr);
        options = readNoteOptions(closingSection != nullptr ? closingSection->child(kW, name) : nullptr, options);
        (endnote ? parts->endnoteOptions : parts->footnoteOptions) = options;
        (endnote ? parts->importedEndnoteOptions : parts->importedFootnoteOptions) = options;
    }
    parts->mailMerge = readMailMerge(*package, settingsPart, settings.get());
    parts->importedMailMerge = parts->mailMerge;
    // Footnote and endnote texts: each note's own markup is kept beside it.
    for (const bool endnote : {false, true}) {
        const XmlElementPtr notes = relatedXml(*package, main, endnote ? Ooxml::RelType::Endnotes : Ooxml::RelType::Footnotes);
        if (!notes) {
            continue;
        }
        for (const XmlElement *note : notes->elements(kW, endnote ? u"endnote" : u"footnote")) {
            const QString type = wAttr(note, u"type");
            if (!type.isEmpty() && type != u"normal") {
                continue; // separators
            }
            const QString id = wAttr(note, u"id");
            (endnote ? env.endnoteText : env.footnoteText).insert(id, noteText(*note));
            (endnote ? parts->endnoteXml : parts->footnoteXml).insert(QStringLiteral("docx-") + id, Ooxml::serializeFragment(*note));
        }
    }

    // Body defaults: the document's base font and tab stop.
    const XmlElementPtr baseRun = env.styles.paragraphRunProperties(QString());
    const QTextCharFormat baseFormat = charFormatFromRPr(baseRun.get(), env.theme);
    auto prepare = [&](QTextDocument *document) {
        QFont font = document->defaultFont();
        if (baseFormat.hasProperty(QTextFormat::FontFamilies)) {
            const QStringList families = baseFormat.fontFamilies().toStringList();
            if (!families.isEmpty()) {
                font.setFamilies(families);
            }
        }
        font.setPointSizeF(baseFormat.fontPointSize());
        document->setDefaultFont(font);
        QTextOption option = document->defaultTextOption();
        option.setTabStopDistance(parts->defaultTabStop);
        document->setDefaultTextOption(option);
    };

    // AGENT-GUARD: imported tables are formatted again by the Word metric
    // and list passes below. With an installed math layout, each intermediate
    // edit otherwise triggers a whole-table layout (hundreds of times for an
    // 80-row table). Keep all stories suspended through those final passes.
    const bool bodyLayoutEnabled = body->isLayoutEnabled();
    body->setLayoutEnabled(false);
    const auto restoreLayouts = qScopeGuard([&] {
        body->setLayoutEnabled(bodyLayoutEnabled);
        forEachStory(*parts, [](HeaderFooter &story) { story.document->setLayoutEnabled(true); });
    });
    const bool undo = body->isUndoRedoEnabled();
    body->setUndoRedoEnabled(false);
    body->clear();
    prepare(body);
    detail::BodyReader reader(env, body, main);
    reader.readDocument(*bodyElement);

    insertLeadingSpace(body);

    // Sections (M88, D-096, docx_sections.h): every paragraph-level
    // w:sectPr marked its paragraph; the body's closing one is WordParts'
    // own. Each section's header and footer references are its own
    // stories; a slot it does not reference is linked to the section
    // before (Word's "Link to Previous"), so a cover section without
    // references shows nothing and a later one inherits.
    setClosingSettings(*parts, readSectionSettings(closingSection));
    if (closingSection != nullptr) {
        parts->sourceSectionXml = Ooxml::serializeFragment(*closingSection);
    }
    parts->importedLayout = parts->layout;
    parts->importedDifferentFirstPage = parts->differentFirstPage;
    parts->importedClosingFlow = parts->closingFlow;
    const QList<const XmlElement *> &sections = reader.sections();
    for (qsizetype i = 0; i < sections.size(); ++i) {
        const XmlElement *sectPr = sections.at(i);
        // AGENT-CONTRACT: the key docx_reader_body.cpp gave the paragraph.
        const QString key = sectPr == closingSection ? QString() : QStringLiteral("r%1").arg(i + 1);
        for (const XmlElement *ref : sectPr->elements()) {
            const bool isHeader = ref->name == u"headerReference";
            if (!isHeader && ref->name != u"footerReference") {
                continue;
            }
            const QString type = wAttr(ref, u"type", QStringLiteral("default"));
            const HeaderFooterSlot slot = type == u"first" ? HeaderFooterSlot::First
                : type == u"even"                          ? HeaderFooterSlot::Even
                                                           : HeaderFooterSlot::Default;
            HeaderFooter &target = *sectionStory(*parts, key, !isHeader, slot, true);
            if (target.document) {
                continue;
            }
            const auto rel = package->relationship(main, ref->attribute(kR, u"id"));
            const QString part = rel ? package->targetPart(main, *rel) : QString();
            const XmlElementPtr content = part.isEmpty() ? nullptr : package->xml(part);
            if (!content) {
                continue;
            }
            target.document = std::make_unique<QTextDocument>();
            target.document->setLayoutEnabled(false);
            target.document->setUndoRedoEnabled(false);
            prepare(target.document.get());
            detail::BodyReader headerReader(env, target.document.get(), part);
            headerReader.readDocument(*content);
            target.document->setUndoRedoEnabled(true);
            target.document->setModified(false);
            target.sourcePart = part;
        }
    }
    if (!env.approximations.isEmpty()) {
        parts->warnings.append(QStringLiteral("Shown approximately and kept unchanged on save: %1.")
                                   .arg(describe(env.approximations)));
    }
    // Comments (docx_comments.h): the ones the body marks are the set an
    // unchanged save copies back.
    parts->comments = readComments(*package, main);
    for (const CommentSpan &span : commentSpans(*body)) {
        if (span.reference >= 0) {
            parts->sourceCommentIds.append(span.id);
        }
    }
    // Content controls (docx_controls.h): the text each one holds now is
    // what a save compares against (placeholder, binding, list, date).
    auto noteControlTexts = [parts](const QTextDocument &story) {
        const QHash<QString, QString> texts = controlTexts(story);
        for (auto it = texts.cbegin(); it != texts.cend(); ++it) {
            if (auto control = parts->contentControls.find(it.key()); control != parts->contentControls.end()) {
                control->sourceText = it.value();
            }
        }
    };
    if (!parts->contentControls.isEmpty()) {
        noteControlTexts(*body);
        forEachStory(*parts, [&](HeaderFooter &story) { noteControlTexts(*story.document); });
    }
    // Word's line height (docx_metrics.h) and the first lines of list
    // paragraphs after their numbers (docx_list_labels.h): display only,
    // before undo is on.
    applyWordLineMetrics(body);
    layoutListText(body, parts->defaultTabStop);
    forEachStory(*parts, [&](HeaderFooter &story) {
        const bool modified = story.document->isModified();
        applyWordLineMetrics(story.document.get());
        layoutListText(story.document.get(), parts->defaultTabStop);
        story.document->setModified(modified); // untouched: still copied byte for byte
    });
    body->setUndoRedoEnabled(undo);
    body->setModified(false);
    return true;
}

} // namespace QindaOffice::Docx
