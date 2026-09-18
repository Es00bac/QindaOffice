// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaWrite's typography and insertion commands beyond the M11 set
// (D-018): highlight colour, superscript/subscript, grow/shrink font,
// line spacing, and Insert ▸ Date / Time / Special Character /
// Horizontal Rule / Hyperlink, plus the word count. Entries in the
// window's ActionModel (rule 5); the dialogs are request signals the
// window answers through the host.

#include "action_model.h"

#include <QColor>
#include <QObject>
#include <QTextCharFormat>
#include <QVariantMap>

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class WriteTypography final : public QObject {
    Q_OBJECT
public:
    WriteTypography(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                    QObject *parent);

    [[nodiscard]] QList<Quick::MenuNode> formatMenuItems() const;
    [[nodiscard]] QList<Quick::MenuNode> insertMenuItems() const;
    [[nodiscard]] QStringList stripIds() const;
    [[nodiscard]] QString wordCountId() const;

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
    [[nodiscard]] QVariantMap countsMap() const;
    [[nodiscard]] QStringList symbols() const;

    // Dialog answers.
    void applyHighlight(const QColor &color);
    void insertSymbol(const QString &text);
    void insertHyperlink(const QString &url, const QString &label);

signals:
    void countsChanged();
    void highlightRequested();
    void symbolRequested();
    // `selected` is the selection the link will label (empty: ask for one).
    void hyperlinkRequested(const QString &selected);
    void wordCountRequested(const QVariantMap &counts);

private:
    void createActions(Quick::ActionModel *model);
    void applyCharFormat(const QTextCharFormat &format);
    void applyLineSpacing(qreal percent);
    void stepFontSize(int delta);
    void insertHorizontalRule();
    void insertDateTime(bool time);
    void syncFromCursor();

    PageTextControl *m_control;
    WriteDocument *m_document;
    Quick::ActionEntry *m_highlight = nullptr;
    Quick::ActionEntry *m_superscript = nullptr;
    Quick::ActionEntry *m_subscript = nullptr;
    Quick::ActionEntry *m_growFont = nullptr;
    Quick::ActionEntry *m_shrinkFont = nullptr;
    Quick::ActionEntry *m_spacingSingle = nullptr;
    Quick::ActionEntry *m_spacingOneHalf = nullptr;
    Quick::ActionEntry *m_spacingDouble = nullptr;
    Quick::ActionEntry *m_insertDate = nullptr;
    Quick::ActionEntry *m_insertTime = nullptr;
    Quick::ActionEntry *m_insertSymbol = nullptr;
    Quick::ActionEntry *m_insertRule = nullptr;
    Quick::ActionEntry *m_insertHyperlink = nullptr;
    Quick::ActionEntry *m_wordCount = nullptr;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Write
