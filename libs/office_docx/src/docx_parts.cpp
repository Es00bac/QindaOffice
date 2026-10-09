// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_model.h"

#include "docx_float.h"
#include "ooxml_units.h"

namespace QindaOffice::Docx {

WordParts cloneWordParts(const WordParts &parts)
{
    WordParts out;
    out.layout = parts.layout;
    for (int s = 0; s < kHeaderFooterSlots; ++s) {
        for (const bool header : {true, false}) {
            const HeaderFooter &from = header ? parts.headers[size_t(s)] : parts.footers[size_t(s)];
            HeaderFooter &to = header ? out.headers[size_t(s)] : out.footers[size_t(s)];
            if (from.document) {
                to.document.reset(from.document->clone());
                if (hasFloatingPictures(*to.document)) {
                    installPictureHandler(to.document.get());
                }
                // A copy is new content: written from its text, not copied as a part.
                to.document->setModified(true);
            }
        }
    }
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

QString sectionPropertiesXml(const WordParts &parts)
{
    if (!parts.sourceSectionXml.isEmpty()) {
        return parts.sourceSectionXml;
    }
    const PageLayout &l = parts.layout;
    auto twips = [](qreal px) { return QString::number(qRound(Ooxml::Units::pxToTwips(px))); };
    return QStringLiteral("<w:sectPr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
                          "<w:pgSz w:w=\"%1\" w:h=\"%2\"%3/>"
                          "<w:pgMar w:top=\"%4\" w:right=\"%5\" w:bottom=\"%6\" w:left=\"%7\" w:header=\"%8\""
                          " w:footer=\"%9\" w:gutter=\"%10\"/><w:cols w:space=\"720\"/></w:sectPr>")
        .arg(twips(l.pageSize.width()), twips(l.pageSize.height()),
             l.landscape() ? QStringLiteral(" w:orient=\"landscape\"") : QString(), twips(l.margins.top()),
             twips(l.margins.right()), twips(l.margins.bottom()), twips(l.margins.left()), twips(l.headerDistance),
             twips(l.footerDistance))
        .arg(twips(l.gutter));
}

} // namespace QindaOffice::Docx
