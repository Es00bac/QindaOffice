// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's keep-with-next and widow/orphan control on QTextDocument's pages
// (D-068 layout adjustments: display only, never written).
//
// Qt paginates a paged QTextDocument line by line and knows neither rule,
// so a heading could end a page with its text on the next one, and a
// paragraph's first line could sit alone at the bottom of a page. KeepRules
// adds the page breaks Word's layout makes, as display-only breaks
// (Docx::kSyntheticBreak keeps the block's own flags, so the .docx writer,
// the import snapshot and HTML export never see them), and re-derives them
// after edits, from the first changed paragraph on.
//
// Rules, as far as a break before a paragraph can express them:
// - keep with next: a run of keepNext paragraphs moves to the next page
//   when its last line and the next paragraph's first line fall on
//   different pages, unless the run already starts a page (a run longer
//   than a page stays where it is, as in Word);
// - orphan: a paragraph whose first line alone ends a page moves;
// - widow: a two-line paragraph split one and one moves (a longer one would
//   need a break inside the paragraph, which a block format cannot say).
// Paragraphs inside tables are not moved; a table can be the "next".
//
// AGENT-GUARD: the breaks are format edits on the live document. A pass
// joins them to the previous undo step (or, with nothing to undo, drops its
// own undo item) and restores the modified flag, so Undo and the dirty
// state never show them. A pass never runs while Redo is available: any new
// undo item would discard the user's redo history.

#include <QObject>
#include <QTimer>

class QTextDocument;

namespace QindaOffice::Apps::Write {

class KeepRules final : public QObject {
    Q_OBJECT
public:
    // `document` must outlive this object (WriteDocument owns both).
    explicit KeepRules(QTextDocument *document, QObject *parent = nullptr);

    // Re-derives every break now: after a load, before printing.
    void applyAll();
    // Re-derives from `position` on after the debounce (a page layout
    // change passes 0).
    void schedule(int position);
    // Display-only breaks currently in `document`.
    [[nodiscard]] static int count(const QTextDocument *document);
    // Debounce after edits; tests shrink it.
    void setDelay(int milliseconds) { m_timer.setInterval(milliseconds); }

private:
    void run();
    void applyFrom(int position);

    QTextDocument *m_document;
    QTimer m_timer;
    int m_from = -1;
    bool m_applying = false;
};

} // namespace QindaOffice::Apps::Write
