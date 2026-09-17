// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaWrite's typography and insertion commands beyond the M11 set:
// highlight colour, superscript/subscript, grow/shrink font, line spacing,
// and Insert ▸ Date / Time / Special Character / Horizontal Rule /
// Hyperlink, plus the word count readout.
//
// Rationale (D-018). The Format menu covered bold/italic/underline/strike,
// one colour, alignment, lists and indent, and Insert covered page break,
// table, image and columns — which leaves out the formatting a word
// processor is actually reached for: highlighting a passage, a footnote
// marker's superscript, changing line spacing, dropping today's date into a
// letter, or turning a URL into a link. There was also no word count
// anywhere, which is the one number people ask a word processor for.
//
// Same discipline as the rest: persistent window-scoped actions with stable
// objectNames, shared between the menu bar, the toolbar and the context
// menu (AGENTS.md rule 5).

#include <QAction>
#include <QObject>
#include <QTextCharFormat>

class QMenu;

namespace QindaOffice::Apps::Write {

class PageTextView;
class WriteDocument;

class WriteTypography final : public QObject {
    Q_OBJECT
public:
    WriteTypography(PageTextView *view, WriteDocument *document, QObject *parent);

    void populateFormatMenu(QMenu *menu);
    void populateInsertMenu(QMenu *menu);
    [[nodiscard]] QList<QAction *> stripActions() const;
    [[nodiscard]] QAction *wordCountAction() const { return m_wordCount; }

    // Words, characters (with and without spaces) and paragraphs of the
    // whole document, or of the selection when there is one.
    struct Counts {
        int words = 0;
        int characters = 0;
        int charactersNoSpaces = 0;
        int paragraphs = 0;
        bool selectionOnly = false;
    };
    [[nodiscard]] Counts counts() const;

signals:
    // Emitted whenever the counts can have changed, so the window can keep
    // the status bar current without polling.
    void countsChanged();

private:
    void createActions();
    void applyCharFormat(const QTextCharFormat &format);
    void applyLineSpacing(qreal percent);
    void stepFontSize(int delta);
    void pickHighlight();
    void insertSymbol();
    void insertHyperlink();
    void insertHorizontalRule();
    void insertDateTime(bool time);
    void showWordCount();
    void syncFromCursor();

    PageTextView *m_view;
    WriteDocument *m_document;
    QAction *m_highlight = nullptr;
    QAction *m_superscript = nullptr;
    QAction *m_subscript = nullptr;
    QAction *m_growFont = nullptr;
    QAction *m_shrinkFont = nullptr;
    QAction *m_spacingSingle = nullptr;
    QAction *m_spacingOneHalf = nullptr;
    QAction *m_spacingDouble = nullptr;
    QAction *m_insertDate = nullptr;
    QAction *m_insertTime = nullptr;
    QAction *m_insertSymbol = nullptr;
    QAction *m_insertRule = nullptr;
    QAction *m_insertHyperlink = nullptr;
    QAction *m_wordCount = nullptr;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Write
