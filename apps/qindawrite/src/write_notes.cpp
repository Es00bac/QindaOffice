// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_notes.h"

#include "menu_tree.h"
#include "page_text_control.h"
#include "write_document.h"
#include "write_footnotes.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

// Removes the mark carrying `id` under `property`.
void removeMark(QTextDocument *document, int property, const QString &id)
{
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().stringProperty(property) == id) {
                QTextCursor cursor(document);
                cursor.setPosition(fragment.position());
                cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                cursor.removeSelectedText();
                return;
            }
        }
    }
}

QVariantMap optionsMap(const Docx::NoteOptions &options, bool endnote)
{
    return {{QStringLiteral("format"), Docx::noteFormat(options, endnote)},
            {QStringLiteral("start"), options.start},
            {QStringLiteral("restart"), options.restart.isEmpty() ? QStringLiteral("continuous") : options.restart},
            {QStringLiteral("position"),
             options.position.isEmpty() ? (endnote ? QStringLiteral("docEnd") : QStringLiteral("pageBottom"))
                                        : options.position}};
}

} // namespace

NotesController::NotesController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                                 QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    m_insertEndnote = model->add(QStringLiteral("writeInsertEndnoteAction"), tr("Insert &Endnote…"),
                                 QKeySequence(QStringLiteral("Ctrl+Alt+D")), QStringLiteral("notebook-pen"));
    m_options = model->add(QStringLiteral("writeNoteOptionsAction"), tr("Footnote and Endnote…"), {},
                           QStringLiteral("settings-2"));
    m_convert = model->add(QStringLiteral("writeConvertNotesAction"), tr("Con&vert Notes…"), {},
                           QStringLiteral("arrow-left-right"));
    connect(m_insertEndnote, &ActionEntry::triggered, this,
            [this] { emit noteRequested(QStringLiteral("endnote"), QString(), QString()); });
    connect(m_options, &ActionEntry::triggered, this, &NotesController::optionsRequested);
    connect(m_convert, &ActionEntry::triggered, this, &NotesController::convertRequested);
}

QList<MenuNode> NotesController::menuItems() const
{
    return {MenuNode::action(m_insertEndnote->id()), MenuNode::action(m_options->id()),
            MenuNode::action(m_convert->id())};
}

bool NotesController::editEndnoteAt(int position)
{
    const QString id = endnoteIdAt(m_document->textDocument(), position);
    if (id.isEmpty()) {
        return false;
    }
    emit noteRequested(QStringLiteral("endnote"), id, m_document->endnoteText(id));
    return true;
}

void NotesController::renumber()
{
    renumberNoteMarkers(m_document->textDocument(), m_document->noteOptions(false), m_document->noteOptions(true));
}

void NotesController::commitNote(const QString &kind, const QString &id, const QString &rawText)
{
    const bool endnote = kind == u"endnote";
    const QString text = rawText.trimmed();
    if (id.isEmpty() && text.isEmpty()) {
        return;
    }
    QHash<QString, QString> footnotes = m_document->footnotes();
    QHash<QString, QString> endnotes = m_document->endnotes();
    QHash<QString, QString> &table = endnote ? endnotes : footnotes;
    QTextCursor cursor = m_control->textCursor();
    cursor.beginEditBlock();
    if (id.isEmpty()) {
        cursor.clearSelection();
        table.insert(endnote ? insertEndnoteMarker(cursor) : insertFootnoteMarker(cursor), text);
    } else if (text.isEmpty()) {
        removeMark(m_document->textDocument(), endnote ? kEndnoteIdProperty : kFootnoteIdProperty, id);
        table.remove(id);
    } else {
        table.insert(id, text);
    }
    m_document->setNoteTexts(footnotes, endnotes);
    renumber();
    cursor.endEditBlock();
    if (id.isEmpty()) {
        m_control->setTextCursor(cursor);
    }
    emit message(id.isEmpty() ? (endnote ? tr("Endnote added") : tr("Footnote added"))
                 : text.isEmpty() ? tr("Note removed")
                                  : tr("Note changed"));
}

QVariantMap NotesController::noteOptions() const
{
    QVariantList formats;
    for (const auto &[format, label] : {std::pair{"decimal", "1, 2, 3, …"}, std::pair{"lowerLetter", "a, b, c, …"},
                                        std::pair{"upperLetter", "A, B, C, …"}, std::pair{"lowerRoman", "i, ii, iii, …"},
                                        std::pair{"upperRoman", "I, II, III, …"}, std::pair{"chicago", "*, †, ‡, §, …"}}) {
        formats.append(QVariantMap{{QStringLiteral("format"), QString::fromLatin1(format)},
                                   {QStringLiteral("label"), QString::fromUtf8(label)}});
    }
    return {{QStringLiteral("footnote"), optionsMap(m_document->noteOptions(false), false)},
            {QStringLiteral("endnote"), optionsMap(m_document->noteOptions(true), true)},
            {QStringLiteral("formats"), formats},
            {QStringLiteral("footnotes"), m_document->footnotes().size()},
            {QStringLiteral("endnotes"), m_document->endnotes().size()}};
}

bool NotesController::applyNoteOptions(const QVariantMap &answer)
{
    const bool endnote = answer.value(QStringLiteral("kind")).toString() == u"endnote";
    Docx::NoteOptions options;
    const QString format = answer.value(QStringLiteral("format")).toString();
    options.numberFormat = format == (endnote ? u"lowerRoman" : u"decimal") ? QString() : format;
    options.start = qMax(1, answer.value(QStringLiteral("start"), 1).toInt());
    const QString restart = answer.value(QStringLiteral("restart")).toString();
    options.restart = restart == u"continuous" ? QString() : restart;
    const QString position = answer.value(QStringLiteral("position")).toString();
    options.position = position == u"pageBottom" || position == u"docEnd" ? QString() : position;
    if (options == m_document->noteOptions(endnote)) {
        return false;
    }
    QTextCursor cursor(m_document->textDocument());
    cursor.beginEditBlock();
    m_document->setNoteOptions(endnote, options);
    cursor.endEditBlock();
    return true;
}

bool NotesController::convertNotes(const QString &mode)
{
    QHash<QString, QString> footToEnd;
    QHash<QString, QString> endToFoot;
    QTextCursor cursor(m_document->textDocument());
    cursor.beginEditBlock();
    const int changed = convertNoteMarkers(m_document->textDocument(), mode, &footToEnd, &endToFoot);
    if (changed > 0) {
        QHash<QString, QString> footnotes = m_document->footnotes();
        QHash<QString, QString> endnotes = m_document->endnotes();
        QHash<QString, QString> newFoot;
        QHash<QString, QString> newEnd;
        for (auto it = footnotes.cbegin(); it != footnotes.cend(); ++it) {
            if (footToEnd.contains(it.key())) {
                newEnd.insert(footToEnd.value(it.key()), it.value());
            } else {
                newFoot.insert(it.key(), it.value());
            }
        }
        for (auto it = endnotes.cbegin(); it != endnotes.cend(); ++it) {
            if (endToFoot.contains(it.key())) {
                newFoot.insert(endToFoot.value(it.key()), it.value());
            } else {
                newEnd.insert(it.key(), it.value());
            }
        }
        m_document->setNoteTexts(newFoot, newEnd);
        renumber();
    }
    cursor.endEditBlock();
    emit message(changed > 0 ? tr("%n note(s) converted", "", changed) : tr("No notes to convert"));
    return changed > 0;
}

} // namespace QindaOffice::Apps::Write
