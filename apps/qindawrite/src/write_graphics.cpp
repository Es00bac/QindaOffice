// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_graphics.h"
#include "docx_graphics.h"
#include "menu_tree.h"
#include "page_text_control.h"
#include "write_document.h"

namespace QindaOffice::Apps::Write {
GraphicsController::GraphicsController(PageTextControl *control, WriteDocument *document,
                                         Quick::ActionModel *model, QObject *parent)
    : QObject(parent), m_control(control), m_document(document)
{
    const QStringList ids{QStringLiteral("writeInsertChartAction"), QStringLiteral("writeInsertSmartArtAction"), QStringLiteral("writeInsertShapeAction")};
    const QStringList kinds{QStringLiteral("chart"), QStringLiteral("smartArt"), QStringLiteral("shape")};
    const QStringList labels{tr("&Chart…"), tr("&SmartArt…"), tr("&Shape…")};
    const QStringList icons{QStringLiteral("chart-column"), QStringLiteral("workflow"), QStringLiteral("shapes")};
    for (int i = 0; i < ids.size(); ++i) {
        auto *entry = model->add(ids.at(i), labels.at(i), {}, icons.at(i));
        connect(entry, &Quick::ActionEntry::triggered, this, [this, kind = kinds.at(i)] { if (insert(kind)) open(); });
    }
    m_edit = model->add(QStringLiteral("writeEditGraphicAction"), tr("Edit &Graphic…"), {}, QStringLiteral("pencil"));
    connect(m_edit, &Quick::ActionEntry::triggered, this, [this] { open(); });
    connect(control, &PageTextControl::cursorPositionChanged, this, &GraphicsController::sync);
    connect(control, &PageTextControl::selectionChanged, this, &GraphicsController::sync);
    connect(document, &WriteDocument::contentChanged, this, &GraphicsController::sync);
    sync();
}
QList<Quick::MenuNode> GraphicsController::insertItems() const
{
    return {Quick::MenuNode::action(QStringLiteral("writeInsertChartAction")),
            Quick::MenuNode::action(QStringLiteral("writeInsertSmartArtAction")),
            Quick::MenuNode::action(QStringLiteral("writeInsertShapeAction"))};
}
QTextCursor GraphicsController::selected() const
{
    const auto cursor = m_control->textCursor();
    if (cursor.hasSelection() && cursor.selectionEnd() - cursor.selectionStart() != 1) return {};
    const int start = cursor.hasSelection() ? cursor.selectionStart() : cursor.position();
    for (const int pos : {start, start - 1}) {
        if (pos < 0 || pos >= cursor.document()->characterCount() - 1) continue;
        QTextCursor selected(cursor.document()); selected.setPosition(pos); selected.setPosition(pos + 1, QTextCursor::KeepAnchor);
        if (selected.selectedText() == QString(QChar::ObjectReplacementCharacter) && selected.charFormat().hasProperty(Docx::kGraphicKind)) return selected;
        if (cursor.hasSelection()) break;
    }
    return {};
}
void GraphicsController::sync() { m_edit->setEnabled(!selected().isNull()); }
bool GraphicsController::fail(const QString &error)
{
    m_error = error; emit errorChanged(); emit message(error); return false;
}
bool GraphicsController::insert(const QString &kind)
{
    auto cursor = m_control->textCursor();
    auto format = Docx::newGraphic(kind, *cursor.document());
    if (!format.hasProperty(Docx::kGraphicKind)) return fail(tr("Choose Chart, SmartArt or Shape."));
    cursor.beginEditBlock(); cursor.insertImage(format); cursor.endEditBlock();
    cursor.setPosition(cursor.position() - 1, QTextCursor::KeepAnchor);
    m_control->setTextCursor(cursor); m_document->storyEdited(); sync(); return true;
}
QVariantMap GraphicsController::info() const
{
    const auto cursor = selected(); return cursor.isNull() ? QVariantMap() : Docx::graphicInfo(cursor.charFormat().toImageFormat());
}
bool GraphicsController::open()
{
    m_pending = selected();
    if (m_pending.isNull()) return false;
    m_original = m_pending.charFormat(); m_error.clear(); emit errorChanged();
    emit dialogRequested(Docx::graphicInfo(m_original.toImageFormat())); return true;
}
bool GraphicsController::editAt(int position)
{
    QTextCursor cursor(m_control->document());
    if (position < 0 || position >= cursor.document()->characterCount() - 1) return false;
    cursor.setPosition(position); cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
    if (!cursor.charFormat().hasProperty(Docx::kGraphicKind)) return false;
    m_control->setTextCursor(cursor); return open();
}
bool GraphicsController::apply(const QVariantMap &answer)
{
    // A live cursor tracks edits before the object, but a changed or
    // removed graphic must never be overwritten by a stale dialog.
    if (m_pending.isNull()) { m_pending = selected(); if (!m_pending.isNull()) m_original = m_pending.charFormat(); }
    if (m_pending.isNull() || m_pending.selectedText() != QString(QChar::ObjectReplacementCharacter)
        || m_pending.charFormat() != m_original) return fail(tr("The graphic changed while its editor was open. Reopen Edit Graphic."));
    auto format = m_original.toImageFormat(); QString error;
    if (!Docx::editGraphic(format, answer, *m_pending.document(), &error)) return fail(error);
    if (format != m_original) {
        m_pending.beginEditBlock(); m_pending.setCharFormat(format); m_pending.endEditBlock();
        m_control->setTextCursor(m_pending); m_document->storyEdited();
    }
    m_pending = {}; m_error.clear(); emit errorChanged(); emit message(tr("Graphic updated.")); sync(); return true;
}
} // namespace QindaOffice::Apps::Write
