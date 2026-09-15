// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// FindReplaceBar (M11 UI completeness): the classic word-processor find
// strip docked under the page view. Driven by the shell's standard
// editFind / editReplace / editFindNext / editFindPrevious actions, so
// Ctrl+F, Ctrl+H and F3/Shift+F3 all land here. Esc returns focus to the
// document. Search wraps around the document and reports honestly in the
// status bar.

#include <QWidget>

class QCheckBox;
class QKeyEvent;
class QLineEdit;
class QToolButton;

namespace QindaOffice::Apps::Write {

class PageTextView;
class WriteDocument;

class FindReplaceBar final : public QWidget {
    Q_OBJECT
public:
    FindReplaceBar(PageTextView *view, WriteDocument *document, QWidget *parent);

    // The two shell entry points: Ctrl+F focuses the find field, Ctrl+H
    // the replace field. Both show the bar.
    void openFind();
    void openReplace();
    void findNext();
    void findPrevious();
    // Public for symmetry with the buttons and for tests: replace the
    // current match (if any) and jump to the next, or replace every match.
    void replaceCurrent();
    void replaceAll();

signals:
    void message(const QString &text);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    [[nodiscard]] bool findIn(bool backward);

    PageTextView *m_view;
    WriteDocument *m_document;
    QLineEdit *m_findEdit;
    QLineEdit *m_replaceEdit;
    QCheckBox *m_caseCheck;
    QCheckBox *m_wordsCheck;
};

} // namespace QindaOffice::Apps::Write
