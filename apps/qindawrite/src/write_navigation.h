// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's Navigation pane in QindaWrite (M73, D-082; View ▸ Navigation
// Pane): the document's headings — paragraphs with an outline level, from
// their style (Heading 1–9, w:outlineLvl) or their own — as a tree, each
// with its list number when it has one. A click puts the caret at the
// heading and the page there; a heading's arrow collapses or expands what
// is under it; Move Up / Move Down move a heading with its whole section
// (one undo step), Word's drag in the pane. The caret's heading is marked.

#include "action_model.h"

#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class NavigationModel final : public QObject {
    Q_OBJECT
    // [{index, level (1–9), text, number, expandable, expanded, visible}]
    Q_PROPERTY(QVariantList headings READ headings NOTIFY headingsChanged)
    // The heading the caret is under (index), −1 before the first.
    Q_PROPERTY(int current READ current NOTIFY currentChanged)
public:
    NavigationModel(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model, QObject *parent);

    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;
    [[nodiscard]] QVariantList headings() const { return m_rows; }
    [[nodiscard]] int current() const { return m_current; }

    // The caret to heading `index`, the page there.
    Q_INVOKABLE void go(int index);
    // Collapses or expands what is under heading `index`.
    Q_INVOKABLE void toggle(int index);
    // Moves heading `index` and its section before the previous (`up`) or
    // after the next heading of the same or a higher level.
    Q_INVOKABLE bool move(int index, bool up);
    // Re-reads the headings now (tests; edits re-read after a pause).
    void refresh();

signals:
    void headingsChanged();
    void currentChanged();
    void paneToggleRequested(bool visible);

private:
    struct Heading {
        int position = 0; // the paragraph's start
        int level = 1;
        QString text;
        QString number;
    };
    void syncCurrent();
    // Where heading `index`'s section ends (the next heading of its level
    // or above, or the end of the body).
    [[nodiscard]] int sectionEnd(int index) const;

    PageTextControl *m_control;
    WriteDocument *m_document;
    Quick::ActionEntry *m_pane = nullptr;
    QList<Heading> m_headings;
    QVariantList m_rows;
    QSet<QString> m_collapsed; // "level|text" of the collapsed headings
    int m_current = -1;
    QTimer m_refresh;
};

} // namespace QindaOffice::Apps::Write
