// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_revisions.h"

#include "page_text_view.h"
#include "write_document.h"

#include <QtWidgets/private/qwidgettextcontrol_p.h>

#include <QKeyEvent>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

namespace {

// Revision marks are document CONTENT, not window chrome, so they carry
// their own colours the way a hyperlink does — they must read the same on
// screen, on paper and in the PDF, where the desktop palette does not
// apply. Blue for "added", red for "removed", the convention every word
// processor shares.
const QColor kInsertColor(0x1a, 0x5f, 0xb4);
const QColor kDeleteColor(0xc0, 0x39, 0x2b);

bool isMarked(const QTextCharFormat &format)
{
    return format.hasProperty(kRevisionInsertProperty)
        || format.hasProperty(kRevisionDeleteProperty);
}

} // namespace

WriteRevisions::WriteRevisions(PageTextView *view, WriteDocument *document, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_document(document)
{
    m_author = QString::fromLocal8Bit(qgetenv("USER"));
    if (m_author.isEmpty()) {
        m_author = tr("Author");
    }
    connect(m_document->textDocument(), &QTextDocument::contentsChange, this,
            [this](int position, int charsRemoved, int charsAdded) {
                Q_UNUSED(charsRemoved);
                if (m_recording && !m_applying && charsAdded > 0) {
                    markInserted(position, charsAdded);
                }
            });
}

void WriteRevisions::setRecording(bool recording)
{
    if (m_recording == recording) {
        return;
    }
    m_recording = recording;
    if (recording) {
        // The filter only exists while recording: intercepting delete keys
        // when nothing is being tracked would be a behaviour change with no
        // upside.
        m_view->installEventFilter(this);
    } else {
        m_view->removeEventFilter(this);
    }
    emit recordingChanged(recording);
}

void WriteRevisions::markInserted(int position, int charsAdded)
{
    QTextDocument *text = m_document->textDocument();
    QTextCursor cursor(text);
    cursor.setPosition(position);
    cursor.setPosition(qMin(position + charsAdded, text->characterCount() - 1),
                       QTextCursor::KeepAnchor);
    if (!cursor.hasSelection()) {
        return;
    }
    QTextCharFormat mark;
    mark.setProperty(kRevisionInsertProperty, m_author);
    mark.setFontUnderline(true);
    mark.setForeground(kInsertColor);
    m_applying = true;
    // AGENT-GUARD: merging a format from inside contentsChange re-enters
    // contentsChange. m_applying is what stops the mark from marking itself
    // forever.
    cursor.mergeCharFormat(mark);
    m_applying = false;
    emit changesChanged();
}

void WriteRevisions::strikeSelection(bool forward)
{
    QWidgetTextControl *control = m_view->control();
    QTextCursor cursor = control->textCursor();
    if (!cursor.hasSelection()) {
        if (forward) {
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        } else {
            cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);
        }
    }
    if (!cursor.hasSelection()) {
        return;
    }
    m_applying = true;
    // Text this session inserted and has not had accepted is removed
    // outright: "undo my own typing" cannot mean "strike my own typing".
    if (cursor.charFormat().hasProperty(kRevisionInsertProperty)) {
        cursor.removeSelectedText();
    } else {
        QTextCharFormat mark;
        mark.setProperty(kRevisionDeleteProperty, m_author);
        mark.setFontStrikeOut(true);
        mark.setForeground(kDeleteColor);
        cursor.mergeCharFormat(mark);
        // Step past the struck text so the next press keeps moving instead
        // of re-striking the same character.
        cursor.setPosition(forward ? cursor.selectionEnd() : cursor.selectionStart());
    }
    m_applying = false;
    control->setTextCursor(cursor);
    emit changesChanged();
}

bool WriteRevisions::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_recording || watched != m_view || event->type() != QEvent::KeyPress) {
        return QObject::eventFilter(watched, event);
    }
    auto *key = static_cast<QKeyEvent *>(event);
    const bool hasSelection = m_view->control()->textCursor().hasSelection();
    if (key->key() == Qt::Key_Backspace) {
        strikeSelection(false);
        return true;
    }
    if (key->key() == Qt::Key_Delete) {
        strikeSelection(true);
        return true;
    }
    // Typing over a selection is a delete followed by an insert; mark the
    // delete half here and let the control perform the insert.
    if (hasSelection && !key->text().isEmpty() && key->text().at(0).isPrint()
        && (key->modifiers() & Qt::ControlModifier) == Qt::NoModifier) {
        strikeSelection(true);
        return false;
    }
    return QObject::eventFilter(watched, event);
}

int WriteRevisions::changeCount() const
{
    int count = 0;
    const QTextDocument *text = m_document->textDocument();
    for (QTextBlock block = text->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && isMarked(fragment.charFormat())) {
                ++count;
            }
        }
    }
    return count;
}

bool WriteRevisions::resolve(bool accept, int position)
{
    QTextDocument *text = m_document->textDocument();
    struct Run {
        int position;
        int length;
        bool inserted;
    };
    QList<Run> runs;
    for (QTextBlock block = text->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !isMarked(fragment.charFormat())) {
                continue;
            }
            if (position >= 0
                && (position < fragment.position()
                    || position > fragment.position() + fragment.length())) {
                continue;
            }
            runs.append({fragment.position(), fragment.length(),
                         fragment.charFormat().hasProperty(kRevisionInsertProperty)});
        }
    }
    if (runs.isEmpty()) {
        return false;
    }
    QTextCursor cursor(text);
    cursor.beginEditBlock();
    m_applying = true;
    // AGENT-GUARD: resolve from the END backwards — removing a run shifts
    // every later position, so a forward pass would edit at stale offsets.
    for (int i = int(runs.size()) - 1; i >= 0; --i) {
        const Run &run = runs.at(i);
        cursor.setPosition(run.position);
        cursor.setPosition(run.position + run.length, QTextCursor::KeepAnchor);
        const bool drop = accept ? !run.inserted : run.inserted;
        if (drop) {
            cursor.removeSelectedText();
            continue;
        }
        // Kept: clear the mark and the formatting that showed it.
        QTextCharFormat plain = cursor.charFormat();
        plain.clearProperty(kRevisionInsertProperty);
        plain.clearProperty(kRevisionDeleteProperty);
        plain.setFontUnderline(false);
        plain.setFontStrikeOut(false);
        plain.clearForeground();
        cursor.setCharFormat(plain);
    }
    m_applying = false;
    cursor.endEditBlock();
    m_document->textDocument()->setModified(true);
    emit changesChanged();
    return true;
}

void WriteRevisions::acceptAll()
{
    resolve(true, -1);
}

void WriteRevisions::rejectAll()
{
    resolve(false, -1);
}

bool WriteRevisions::acceptAtCursor()
{
    return resolve(true, m_view->control()->textCursor().position());
}

bool WriteRevisions::rejectAtCursor()
{
    return resolve(false, m_view->control()->textCursor().position());
}

bool WriteRevisions::goToNextChange()
{
    QWidgetTextControl *control = m_view->control();
    const int from = control->textCursor().position();
    const QTextDocument *text = m_document->textDocument();
    int first = -1;
    int next = -1;
    for (QTextBlock block = text->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !isMarked(fragment.charFormat())) {
                continue;
            }
            if (first < 0) {
                first = fragment.position();
            }
            if (next < 0 && fragment.position() > from) {
                next = fragment.position();
            }
        }
    }
    // Wrap: with the cursor past the last change, the next one is the first.
    const int target = next >= 0 ? next : first;
    if (target < 0) {
        return false;
    }
    QTextCursor cursor = control->textCursor();
    cursor.setPosition(target);
    control->setTextCursor(cursor);
    return true;
}

} // namespace QindaOffice::Apps::Write
