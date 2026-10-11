// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_model.h"
#include "docx_math.h"

#include "docx_float.h"
#include "docx_sections.h"

namespace QindaOffice::Docx {

namespace {

void cloneStory(const HeaderFooter &from, HeaderFooter &to)
{
    to.unlinked = from.unlinked;
    if (from.document) {
        to.document.reset(from.document->clone());
        installMathLayout(to.document.get());
        if (hasFloatingPictures(*to.document)) {
            installPictureHandler(to.document.get());
        }
        // A copy is new content: written from its text, not copied as a part.
        to.document->setModified(true);
    }
}

} // namespace

WordParts cloneWordParts(const WordParts &parts)
{
    WordParts out;
    out.layout = parts.layout;
    for (int s = 0; s < kHeaderFooterSlots; ++s) {
        cloneStory(parts.headers[size_t(s)], out.headers[size_t(s)]);
        cloneStory(parts.footers[size_t(s)], out.footers[size_t(s)]);
    }
    for (const auto &[key, stories] : parts.sectionStories) {
        SectionStories &copy = out.sectionStories[key];
        for (int s = 0; s < kHeaderFooterSlots; ++s) {
            cloneStory(stories.headers[size_t(s)], copy.headers[size_t(s)]);
            cloneStory(stories.footers[size_t(s)], copy.footers[size_t(s)]);
        }
    }
    out.closingFlow = parts.closingFlow;
    out.importedClosingFlow = parts.importedClosingFlow;
    out.differentFirstPage = parts.differentFirstPage;
    out.differentOddEven = parts.differentOddEven;
    out.footnotes = parts.footnotes;
    out.footnoteXml = parts.footnoteXml;
    out.footnoteImportedText = parts.footnoteImportedText;
    out.endnotes = parts.endnotes;
    out.endnoteXml = parts.endnoteXml;
    out.endnoteImportedText = parts.endnoteImportedText;
    out.footnoteOptions = parts.footnoteOptions;
    out.endnoteOptions = parts.endnoteOptions;
    out.importedFootnoteOptions = parts.importedFootnoteOptions;
    out.importedEndnoteOptions = parts.importedEndnoteOptions;
    out.comments = parts.comments;
    out.sourceCommentIds = parts.sourceCommentIds;
    out.contentControls = parts.contentControls;
    out.bibliography = parts.bibliography;
    out.media = parts.media;
    out.source = parts.source;
    out.sourceSectionXml = parts.sourceSectionXml;
    out.importedLayout = parts.importedLayout;
    out.importedDifferentFirstPage = parts.importedDifferentFirstPage;
    out.properties = parts.properties;
    out.defaultTabStop = parts.defaultTabStop;
    out.styles = parts.styles;
    // The source's w:mailMerge is not carried: an ordinary document.
    out.importedMailMerge = parts.importedMailMerge;
    return out;
}

void shareStories(WordParts &parts, const QString &key)
{
    SectionStories &stories = parts.sectionStories[key];
    for (int s = 0; s < kHeaderFooterSlots; ++s) {
        cloneStory(parts.headers[size_t(s)], stories.headers[size_t(s)]);
        cloneStory(parts.footers[size_t(s)], stories.footers[size_t(s)]);
    }
}

QString sectionPropertiesXml(const WordParts &parts)
{
    // The source's own, saying what the closing section says now (M88).
    if (const Ooxml::XmlElementPtr source = Ooxml::parseFragment(parts.sourceSectionXml)) {
        const SectionSettings now = closingSettings(parts);
        const SectionSettings was = importedClosingSettings(parts);
        if (now == was) {
            return parts.sourceSectionXml;
        }
        writeSectionSettings(*source, now, was);
        return Ooxml::serializeFragment(*source);
    }
    return Ooxml::serializeFragment(*newSectionElement(closingSettings(parts)));
}

} // namespace QindaOffice::Docx
