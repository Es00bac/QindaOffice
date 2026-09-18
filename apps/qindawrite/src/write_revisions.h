// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Track changes for QindaWrite.
//
// Qt has no revision model, so this is built from the two things
// QTextDocument does give us: a char-format property per run, and the
// ability to see every edit before and after it happens.
//
// * An INSERTION is marked after the fact, from contentsChange: the run
//   that appeared gets kRevisionInsertProperty (the author) plus underline
//   and the insertion colour.
// * A DELETION cannot be marked after the fact — the text is already gone —
//   so while recording, the delete gestures are intercepted on the item and
//   turned into marks instead: the run gets kRevisionDeleteProperty plus a
//   strikethrough and stays in the document until the change is accepted.
//
// AGENT-CONTRACT: accepting makes the document what it would have been
// without the marks (struck text removed, insert marks cleared); rejecting
// makes it what it was before (inserted text removed, strikethroughs
// cleared). Anything that adds a new mark must keep both halves true.

#include <QObject>
#include <QString>
#include <QTextFormat>

class QTextDocument;

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

// Author of the change; presence of the property IS the mark.
inline constexpr int kRevisionInsertProperty = QTextFormat::UserProperty + 9;
inline constexpr int kRevisionDeleteProperty = QTextFormat::UserProperty + 10;

class WriteRevisions final : public QObject {
    Q_OBJECT
public:
    WriteRevisions(PageTextControl *control, WriteDocument *document, QObject *parent);

    // The object whose key presses the delete interception watches while
    // recording (the page item); events are filtered, never consumed
    // otherwise.
    void setInputTarget(QObject *target);

    [[nodiscard]] bool isRecording() const { return m_recording; }
    void setRecording(bool recording);
    [[nodiscard]] QString author() const { return m_author; }

    // Marked runs currently in the document.
    [[nodiscard]] int changeCount() const;

    void acceptAll();
    void rejectAll();
    // Acts on the marked run under the cursor; false when there is none.
    bool acceptAtCursor();
    bool rejectAtCursor();
    // Moves the cursor to the next marked run, wrapping. False when the
    // document has no marks at all.
    bool goToNextChange();

signals:
    void recordingChanged(bool recording);
    void changesChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Applies `accept` semantics to every marked run, or only to the run
    // containing `position` when `position` is >= 0. Returns true when
    // something changed.
    bool resolve(bool accept, int position);
    void markInserted(int position, int charsAdded);
    // Strikes the cursor's selection as deleted and collapses the cursor;
    // text that was itself an unaccepted insertion is removed outright,
    // which is what "undo my own typing" has to mean.
    void strikeSelection(bool forward);

    PageTextControl *m_control;
    WriteDocument *m_document;
    QObject *m_inputTarget = nullptr;
    QString m_author;
    bool m_recording = false;
    // True while this class is editing, so its own edits are not tracked
    // as user insertions.
    bool m_applying = false;
};

} // namespace QindaOffice::Apps::Write
