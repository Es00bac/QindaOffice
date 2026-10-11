// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_references.h"
#include "write_reference_edit.h"
#include "page_text_control.h"
#include "write_document.h"
#include "menu_tree.h"
#include <QTextBlock>
namespace QindaOffice::Apps::Write {
ReferencesController::ReferencesController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model, QObject *parent)
    : QObject(parent), m_control(control), m_document(document)
{
    const QStringList ids{QStringLiteral("writeSourcesAction"), QStringLiteral("writeCitationAction"), QStringLiteral("writeBibliographyAction"),
        QStringLiteral("writeCaptionAction"), QStringLiteral("writeCrossReferenceAction"), QStringLiteral("writeTocOptionsAction"),
        QStringLiteral("writeRefreshReferencesAction"), QStringLiteral("writeFollowReferenceAction"), QStringLiteral("writeEditCitationAction")};
    const QStringList labels{tr("Manage &Sources…"), tr("Insert &Citation…"), tr("Insert &Bibliography"), tr("Insert Cap&tion…"),
        tr("C&ross-reference…"), tr("Table of Contents &Options…"), tr("Refresh &References"), tr("&Go to Reference"), tr("&Edit Citation…")};
    const QStringList icons{QStringLiteral("library"), QStringLiteral("quote"), QStringLiteral("book-open"), QStringLiteral("text-cursor-input"),
        QStringLiteral("link"), QStringLiteral("list-tree"), QStringLiteral("refresh-cw"), QStringLiteral("corner-down-right"), QStringLiteral("pencil")};
    const QStringList kinds{QStringLiteral("sources"), QStringLiteral("citation"), QStringLiteral("bibliography"), QStringLiteral("caption"),
        QStringLiteral("crossReference"), QStringLiteral("toc"), QStringLiteral("refresh"), QStringLiteral("follow"), QStringLiteral("editCitation")};
    for (int i = 0; i < ids.size(); ++i) {
        auto *entry = model->add(ids.at(i), labels.at(i), {}, icons.at(i)); m_actions.append(entry);
        connect(entry, &Quick::ActionEntry::triggered, this, [this, kind = kinds.at(i)] { request(kind); });
    }
    connect(document, &WriteDocument::contentChanged, this, &ReferencesController::changed);
    connect(control, &PageTextControl::cursorPositionChanged, this, &ReferencesController::updateBoundaryFormat);
}
QList<Quick::MenuNode> ReferencesController::menuItems() const
{
    QList<Quick::MenuNode> items; for (const auto *action : m_actions) items.append(Quick::MenuNode::action(action->id())); return items;
}
QVariantList ReferencesController::sources() const
{
    QVariantList out; for (const auto &source : m_document->parts().bibliography.sources) {
        auto values = source.values; values.insert(QStringLiteral("editable"), Docx::supportedSourceType(values.value(QStringLiteral("type")).toString())); out.append(values);
    }
    return out;
}
QString ReferencesController::style() const { return m_document->parts().bibliography.style; }
void ReferencesController::clearError() { if (!m_error.isEmpty()) { m_error.clear(); emit errorChanged(); } }
bool ReferencesController::fail(const QString &error) { m_error = error; emit errorChanged(); emit message(error); return false; }
bool ReferencesController::body() const { return m_control->document() == m_document->textDocument(); }
void ReferencesController::request(const QString &kind)
{
    clearError();
    if (!body()) { fail(tr("Close the header or footer before editing references.")); return; }
    if (kind == u"bibliography") insertBibliography();
    else if (kind == u"refresh") refresh();
    else if (kind == u"follow") follow();
    else if (kind == u"editCitation" && citationInfo().isEmpty()) fail(tr("Place the cursor in a supported, unlocked citation first."));
    else emit dialogRequested(kind);
}
void ReferencesController::finish(QTextCursor cursor)
{
    m_control->setTextCursor(cursor);
    auto format = cursor.charFormat(); Docx::clearReferenceFormat(format); m_control->setCurrentCharFormat(format);
    m_document->storyEdited(); clearError(); emit changed();
}
void ReferencesController::updateBoundaryFormat()
{
    const auto cursor = m_control->textCursor(); if (cursor.hasSelection()) return;
    const int pos = cursor.position(); auto format = m_control->currentCharFormat();
    // At an object's right edge, fresh typing must not lengthen its saved
    // field/bookmark result. In the middle of a result editing remains possible.
    QTextCursor right(cursor.document()); right.setPosition(pos);
    if (pos < cursor.document()->characterCount() - 1) right.setPosition(pos + 1, QTextCursor::KeepAnchor);
    const auto next = pos < cursor.document()->characterCount() - 1 ? right.charFormat() : QTextCharFormat();
    if (pos == 0 || format.property(Docx::kReferenceFields) != next.property(Docx::kReferenceFields)) {
        format.clearProperty(Docx::kReferenceFields); format.clearProperty(Docx::kFieldInstruction); format.clearProperty(Docx::kEmptyReference);
    }
    if (pos == 0 || format.property(Docx::kBookmarkRanges) != next.property(Docx::kBookmarkRanges)) { format.clearProperty(Docx::kBookmarkRanges); format.setAnchorNames({}); }
    if (format != m_control->currentCharFormat()) m_control->setCurrentCharFormat(format);
}
bool ReferencesController::saveSource(const QString &oldTag, const QVariantMap &values)
{
    if (!body()) return fail(tr("Close the header or footer first."));
    QString error; if (!Docx::validateSource(values, &error)) return fail(error);
    auto data = m_document->parts().bibliography; const QString tag = values.value(QStringLiteral("tag")).toString();
    if (data.find(tag) && oldTag != tag) return fail(tr("That source tag is already in use."));
    if (!oldTag.isEmpty() && !data.find(oldTag)) return fail(tr("The source was removed. Reopen Manage Sources."));
    if (!oldTag.isEmpty() && !Docx::supportedSourceType(data.find(oldTag)->values.value(QStringLiteral("type")).toString())) return fail(tr("This imported source type is preserved and read-only."));
    if (!oldTag.isEmpty() && tag != oldTag) for (const auto &span : Docx::referenceSpans(*m_document->textDocument())) {
        const auto code = span.field.value(QStringLiteral("code")).toString(); const auto tokens = Docx::fieldTokens(code);
        if (tokens.value(0).toUpper() == u"CITATION" && tokens.contains(oldTag) && (referenceLocked(span.field) || !simpleReferenceCode(code)))
            return fail(tr("A locked or unsupported citation uses this tag. Keep its tag while editing this source."));
    }
    auto cursor = m_control->textCursor(); cursor.beginEditBlock();
    if (oldTag.isEmpty()) { Docx::BibliographySource source; source.values = values; data.sources.append(source); }
    else for (auto &source : data.sources) if (source.tag() == oldTag) { source.values = values; break; }
    if (!oldTag.isEmpty() && tag != oldTag) for (const auto &span : Docx::referenceSpans(*cursor.document())) {
        const QString code = span.field.value(QStringLiteral("code")).toString(); const auto tokens = Docx::fieldTokens(code);
        if (tokens.value(0).toUpper() != u"CITATION" || tokens.value(1) != oldTag) continue;
        // The supported tag token changes; its occurrence and formatting stay.
        QTextCursor c(cursor.document()); c.setPosition(span.start); c.setPosition(span.end, QTextCursor::KeepAnchor);
        auto fields = c.charFormat().property(Docx::kReferenceFields).toList();
        for (auto &entry : fields) { auto field = entry.toMap(); if (field.value(QStringLiteral("id")) == span.field.value(QStringLiteral("id"))) {
            QString renamed = code; const qsizetype at = renamed.indexOf(oldTag, tokens.at(0).size()); renamed.replace(at, oldTag.size(), tag); field.insert(QStringLiteral("code"), renamed); entry = field;
        } }
        QTextCharFormat f; f.setProperty(Docx::kReferenceFields, fields); f.setProperty(Docx::kFieldInstruction, fields.last().toMap().value(QStringLiteral("code"))); c.mergeCharFormat(f);
    }
    m_document->setBibliography(data); refreshReferences(*cursor.document(), data, m_document->pageSize().height());
    cursor.endEditBlock(); finish(cursor); return true;
}
bool ReferencesController::deleteSource(const QString &tag)
{
    if (!body()) return fail(tr("Close the header or footer first."));
    for (const auto &span : Docx::referenceSpans(*m_document->textDocument())) {
        const auto tokens = Docx::fieldTokens(span.field.value(QStringLiteral("code")).toString());
        if (tokens.value(0).toUpper() == u"CITATION" && tokens.contains(tag)) return fail(tr("Remove this source's citations before deleting it."));
    }
    auto data = m_document->parts().bibliography; if (!data.find(tag)) return fail(tr("Source not found."));
    data.sources.removeIf([&](const auto &source) { return source.tag() == tag; });
    auto c = m_control->textCursor(); c.beginEditBlock(); m_document->setBibliography(data);
    refreshReferences(*c.document(), data, m_document->pageSize().height()); c.endEditBlock(); finish(c); return true;
}
bool ReferencesController::setStyle(const QString &style)
{
    if (!body()) return fail(tr("Close the header or footer first."));
    if (style != u"authorDate" && style != u"numeric") return fail(tr("Choose Author-date or Numeric."));
    auto data = m_document->parts().bibliography; data.style = style;
    auto c = m_control->textCursor(); c.beginEditBlock(); m_document->setBibliography(data);
    refreshReferences(*c.document(), data, m_document->pageSize().height()); c.endEditBlock(); finish(c); return true;
}
} // namespace QindaOffice::Apps::Write
