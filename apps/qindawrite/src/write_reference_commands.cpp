// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_references.h"
#include "write_reference_edit.h"
#include "write_toc.h"
#include "page_geometry.h"
#include "page_text_control.h"
#include "write_document.h"
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QUuid>
namespace QindaOffice::Apps::Write {
bool ReferencesController::insertCitation(const QString &tag, const QString &pages)
{
    if (!body()) return fail(tr("Close the header or footer first."));
    const auto &data = m_document->parts().bibliography; const auto *source = data.find(tag);
    if (!source) return fail(tr("Choose a source, or add one in Manage Sources."));
    if (style() == u"preserved") return fail(tr("Choose a supported citation style in Manage Sources first."));
    if (pages.contains(QLatin1Char('"')) || pages.contains(QLatin1Char('\\')) || pages.contains(QLatin1Char('\n')) || pages.size() > 128)
        return fail(tr("Enter a page number or range without quotes or backslashes."));
    auto c = m_control->textCursor(); c.beginEditBlock();
    const QString code = QStringLiteral("CITATION %1 \\l 1033").arg(tag) + (pages.isEmpty() ? QString() : QStringLiteral(" \\p \"%1\"").arg(pages));
    insertReference(c, code, Docx::citationText(*source, style(), int(citationOrder(*c.document(), data).indexOf(tag) + 1), pages), c.charFormat());
    refreshReferences(*c.document(), data, m_document->pageSize().height()); c.endEditBlock(); finish(c); return true;
}
QVariantMap ReferencesController::citationInfo() const
{
    const auto cursor = m_control->textCursor();
    for (const auto &span : Docx::referenceSpans(*cursor.document())) {
        const auto code = span.field.value(QStringLiteral("code")).toString(); const auto tokens = Docx::fieldTokens(code);
        if (tokens.value(0).toUpper() != u"CITATION" || referenceLocked(span.field) || !simpleReferenceCode(code)) continue;
        if (cursor.selectionStart() < span.start || cursor.selectionEnd() > span.end) continue;
        return {{QStringLiteral("tag"), tokens.value(1)}, {QStringLiteral("pages"), tokens.contains(QStringLiteral("\\p")) ? tokens.value(tokens.indexOf(QStringLiteral("\\p")) + 1) : QString()},
                {QStringLiteral("start"), span.start}, {QStringLiteral("end"), span.end}};
    }
    return {};
}
bool ReferencesController::editCitation(const QString &tag, const QString &pages)
{
    if (!body()) return fail(tr("Close the header or footer first."));
    const auto info = citationInfo(); if (info.isEmpty()) return fail(tr("The citation changed. Reopen Edit Citation."));
    const auto &data = m_document->parts().bibliography; const auto *source = data.find(tag);
    if (!source || style() == u"preserved") return fail(tr("Choose a supported source and citation style first."));
    if (pages.contains(QLatin1Char('"')) || pages.contains(QLatin1Char('\\')) || pages.contains(QLatin1Char('\n')) || pages.size() > 128)
        return fail(tr("Enter a page number or range without quotes or backslashes."));
    auto c = m_control->textCursor(); c.setPosition(info.value(QStringLiteral("start")).toInt());
    c.setPosition(info.value(QStringLiteral("end")).toInt(), QTextCursor::KeepAnchor);
    auto format = c.charFormat(); const auto bookmarks = format.property(Docx::kBookmarkRanges);
    c.beginEditBlock();
    const QString code = QStringLiteral("CITATION %1 \\l 1033").arg(tag) + (pages.isEmpty() ? QString() : QStringLiteral(" \\p \"%1\"").arg(pages));
    insertReference(c, code, Docx::citationText(*source, style(), int(citationOrder(*c.document(), data).indexOf(tag) + 1), pages), format);
    if (bookmarks.isValid()) { auto range = c; range.setPosition(info.value(QStringLiteral("start")).toInt(), QTextCursor::KeepAnchor); QTextCharFormat mark; mark.setProperty(Docx::kBookmarkRanges, bookmarks); range.mergeCharFormat(mark); }
    refreshReferences(*c.document(), data, m_document->pageSize().height()); c.endEditBlock(); finish(c); return true;
}
bool ReferencesController::insertBibliography()
{
    if (!body()) return fail(tr("Close the header or footer first."));
    const auto &data = m_document->parts().bibliography;
    if (data.sources.isEmpty()) return fail(tr("Add a source before inserting a bibliography."));
    if (style() == u"preserved") return fail(tr("Choose a supported citation style first."));
    auto c = m_control->textCursor(); c.beginEditBlock();
    if (!c.block().text().isEmpty()) { c.movePosition(QTextCursor::EndOfBlock); c.insertBlock(QTextBlockFormat(), QTextCharFormat()); }
    insertReference(c, QStringLiteral("BIBLIOGRAPHY"), bibliographyResult(*c.document(), data));
    c.insertBlock(QTextBlockFormat(), QTextCharFormat()); c.endEditBlock(); finish(c); return true;
}
bool ReferencesController::insertCaption(const QString &label, const QString &text)
{
    if (!body()) return fail(tr("Close the header or footer first."));
    if (label != u"Figure" && label != u"Table" && label != u"Equation") return fail(tr("Choose Figure, Table or Equation."));
    if (text.trimmed().isEmpty() || text.contains(QLatin1Char('\n')) || text.size() > 4096) return fail(tr("Enter a caption on one line."));
    auto c = m_control->textCursor(); c.beginEditBlock();
    if (!c.block().text().isEmpty()) { c.movePosition(QTextCursor::EndOfBlock); c.insertBlock(QTextBlockFormat(), QTextCharFormat()); }
    auto block = c.blockFormat(); block.setProperty(Docx::kParagraphStyleId, QStringLiteral("Caption")); c.setBlockFormat(block);
    const int start = c.position(); QTextCharFormat plain; plain.setFontItalic(true);
    c.insertText(label + QLatin1Char(' '), plain); insertReference(c, QStringLiteral("SEQ %1 \\* ARABIC").arg(label), QStringLiteral("1"), plain);
    c.insertText(QStringLiteral(": ") + text, plain); const int end = c.position();
    const QString bookmark = QStringLiteral("_QindaCaption_") + QUuid::createUuid().toString(QUuid::Id128).left(24);
    QTextCursor range(c); range.setPosition(start); range.setPosition(end, QTextCursor::KeepAnchor);
    QTextCharFormat mark; mark.setProperty(Docx::kBookmarkRanges, QStringList{bookmark}); range.mergeCharFormat(mark);
    c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    refreshReferences(*c.document(), m_document->parts().bibliography, m_document->pageSize().height());
    c.endEditBlock(); finish(c); return true;
}
QVariantList ReferencesController::targets() const { return referenceTargets(*m_document->textDocument()); }
bool ReferencesController::insertCrossReference(const QString &bookmark, const QString &kind, bool link)
{
    if (!body()) return fail(tr("Close the header or footer first."));
    if (!Docx::bookmarkRanges(*m_document->textDocument()).contains(bookmark)) return fail(tr("Choose an existing caption or bookmark."));
    if (kind != u"text" && kind != u"number" && kind != u"page") return fail(tr("Choose Caption text, Number or Page."));
    const auto targetRange = Docx::bookmarkRanges(*m_document->textDocument()).value(bookmark);
    if (m_control->textCursor().position() > targetRange.start && m_control->textCursor().position() < targetRange.end) return fail(tr("Insert the cross-reference outside its target caption."));
    auto c = m_control->textCursor(); c.beginEditBlock();
    QString destination = bookmark;
    if (kind == u"number") {
        const auto range = Docx::bookmarkRanges(*c.document()).value(bookmark);
        bool found = false;
        for (const auto &span : Docx::referenceSpans(*c.document())) {
            if (Docx::referenceType(span.field.value(QStringLiteral("code")).toString()) != u"SEQ" || span.start < range.start || span.end > range.end) continue;
            QTextCursor number(c.document()); number.setPosition(span.start); number.setPosition(span.end, QTextCursor::KeepAnchor);
            auto names = number.charFormat().property(Docx::kBookmarkRanges).toStringList();
            destination = QStringLiteral("_QindaNumber_") + QUuid::createUuid().toString(QUuid::Id128).left(24); names.append(destination);
            QTextCharFormat mark; mark.setProperty(Docx::kBookmarkRanges, names); number.mergeCharFormat(mark); found = true; break;
        }
        if (!found) { c.endEditBlock(); return fail(tr("This bookmark has no caption number. Choose Caption text instead.")); }
    }
    const QString code = (kind == u"page" ? QStringLiteral("PAGEREF ") : QStringLiteral("REF ")) + destination
        + (link ? QStringLiteral(" \\h") : QString());
    insertReference(c, code, QStringLiteral("1"), c.charFormat());
    if (link) { QTextCursor range(c); range.setPosition(c.position() - 1, QTextCursor::KeepAnchor); QTextCharFormat f; f.setAnchor(true); f.setAnchorHref(QLatin1Char('#') + destination); range.mergeCharFormat(f); }
    refreshReferences(*c.document(), m_document->parts().bibliography, m_document->pageSize().height()); c.endEditBlock(); finish(c); return true;
}
QVariantMap ReferencesController::tocOptions() const { return tableOfContentsOptions(*m_document->textDocument()); }
bool ReferencesController::applyToc(const QVariantMap &options)
{
    if (!body()) return fail(tr("Close the header or footer first."));
    const int first = options.value(QStringLiteral("first")).toInt(), last = options.value(QStringLiteral("last")).toInt();
    if (first < 1 || last > 9 || last < first) return fail(tr("Choose heading levels between 1 and 9 in ascending order."));
    PageGeometry geometry; geometry.setPageSize(m_document->pageSize());
    const int count = updateTableOfContents(m_document->textDocument(), geometry, options);
    if (count == -2) return fail(tr("This table of contents is locked. Its saved result is preserved."));
    if (count < 0) insertTableOfContents(m_document->textDocument(), geometry, m_control->textCursor(), options);
    finish(m_control->textCursor()); emit message(tr("Table of contents refreshed.")); return true;
}
bool ReferencesController::refresh()
{
    if (!body()) return fail(tr("Close the header or footer first."));
    auto c = m_control->textCursor(); c.beginEditBlock();
    const int count = refreshReferences(*c.document(), m_document->parts().bibliography, m_document->pageSize().height()); c.endEditBlock();
    c.document()->documentLayout()->documentSize(); c.joinPreviousEditBlock();
    refreshReferences(*c.document(), m_document->parts().bibliography, m_document->pageSize().height(), true); c.endEditBlock();
    finish(c); emit message(tr("Refreshed %1 reference results. Unsupported and locked fields keep their saved results.").arg(count)); return true;
}
bool ReferencesController::follow(const QString &bookmark)
{
    auto c = m_control->textCursor(); QString target = bookmark;
    if (target.startsWith(QLatin1Char('#'))) target.remove(0, 1);
    if (target.isEmpty()) {
        target = c.charFormat().anchorHref(); if (target.startsWith(QLatin1Char('#'))) target.remove(0, 1); else target.clear();
        if (target.isEmpty()) for (const auto &span : Docx::referenceSpans(*c.document())) if (c.position() >= span.start && c.position() <= span.end) {
            const auto tokens = Docx::fieldTokens(span.field.value(QStringLiteral("code")).toString());
            if (tokens.value(0).toUpper() == u"REF" || tokens.value(0).toUpper() == u"PAGEREF") target = tokens.value(1);
        }
    }
    if (!followReference(c, target)) return fail(tr("Reference destination not found."));
    m_control->setTextCursor(c); clearError(); return true;
}
} // namespace QindaOffice::Apps::Write
