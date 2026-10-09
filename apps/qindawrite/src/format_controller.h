// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// FormatController: QindaWrite's format/insert commands as ActionEntries
// over the page control — character styles, alignment, lists, indent,
// the inserts — with the two-way sync between the text cursor and the
// entries' check marks. The font family/size and the colour picker are
// read and written by the host (the strip's combos and Tk.ColorButton);
// the dialogs (table, columns, image, colour) are request signals the
// window answers through the host's invokables.

#include "write_actions.h"

#include <QColor>
#include <QObject>
#include <QTextCharFormat>
#include <QTextListFormat>

#include <functional>

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class FormatController final : public QObject {
    Q_OBJECT
public:
    FormatController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                     QObject *parent);

    [[nodiscard]] const WriteActions &actions() const { return m_actions; }
    [[nodiscard]] QList<Quick::MenuNode> formatMenuItems() const;
    [[nodiscard]] QList<Quick::MenuNode> insertMenuItems() const;

    // The font the caret sits in (the document default when the char
    // format carries none — the combos never go blank).
    [[nodiscard]] QString fontFamily() const;
    [[nodiscard]] qreal fontPointSize() const;

    // Merges into the selection, or pins for the next keystroke.
    void applyCharFormat(const QTextCharFormat &format);
    // Increase / Decrease Indent ask this first (ListController changes the
    // level of list items); true when it handled the command.
    void setListIndentHook(std::function<bool(int)> hook) { m_listIndent = std::move(hook); }
    // Clear Formatting asks this first (StylesController keeps the style,
    // M73); true when it handled the command.
    void setClearFormattingHook(std::function<bool()> hook) { m_clearFormatting = std::move(hook); }
    // The actual insertions, separated from their dialogs so tests (and
    // future scripting) can drive them without modal UI.
    void insertTableAtCursor(int rows, int columns);
    void insertColumnsAtCursor(int columns, int gutterPx);
    bool insertImageFile(const QString &path);

signals:
    // Dialog requests the window answers.
    void textColorRequested(const QColor &current);
    void tableDialogRequested();
    void columnsDialogRequested();
    void imageDialogRequested();
    // The caret's format changed (the combos re-read fontFamily/size).
    void formatChanged();

private:
    void mergeBlockFormat(const QTextBlockFormat &format);
    void applyAlignment(Qt::Alignment alignment);
    void adjustIndent(int delta);
    void clearDirectFormatting();
    void insertPageBreak();
    void syncFromControl();

    PageTextControl *m_control;
    WriteDocument *m_document;
    WriteActions m_actions;
    std::function<bool(int)> m_listIndent;
    std::function<bool()> m_clearFormatting;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Write
