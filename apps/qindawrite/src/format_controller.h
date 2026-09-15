// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// FormatController (M11 UI completeness): owns QindaWrite's persistent
// format/insert actions, the formatting toolbar (font family, size,
// character styles, alignment, lists, indent, inserts), all format
// application behavior, and the two-way sync between the text cursor and
// the UI. Menus and the toolbar share the same QActions, so a check mark
// can never disagree between them.

#include "write_actions.h"

#include <QObject>
#include <QTextCharFormat>

class QComboBox;
class QFontComboBox;
class QMenu;
class QToolBar;

namespace QindaOffice::Apps::Write {

class PageTextView;
class WriteDocument;

class FormatController final : public QObject {
    Q_OBJECT
public:
    FormatController(PageTextView *view, WriteDocument *document, QObject *parent);

    [[nodiscard]] const WriteActions &actions() const { return m_actions; }
    // The toolbar is created on first call and returned after; the window
    // owns placement (addToolBar).
    [[nodiscard]] QToolBar *toolBar();
    // Reuses the persistent actions so menus and toolbar stay in sync.
    void populateFormatMenu(QMenu *menu);
    void populateInsertMenu(QMenu *menu);

    // The actual insertions, separated from their dialogs so tests (and
    // future scripting) can drive them without modal UI.
    void insertTableAtCursor(int rows, int columns);
    void insertColumnsAtCursor(int columns, int gutterPx);

private:
    void applyCharFormat(const QTextCharFormat &format);
    void mergeBlockFormat(const QTextBlockFormat &format);
    void applyAlignment(Qt::Alignment alignment);
    void toggleList(QTextListFormat::Style style);
    void adjustIndent(int delta);
    void pickTextColor();
    void clearDirectFormatting();
    void insertTable();
    void insertImage();
    void insertColumns();
    void insertPageBreak();
    void syncFromControl();

    PageTextView *m_view;
    WriteDocument *m_document;
    WriteActions m_actions;
    QToolBar *m_toolBar = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QComboBox *m_sizeCombo = nullptr;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Write
