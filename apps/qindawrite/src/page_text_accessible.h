// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Accessibility for QindaWrite's page item (plan §5.1, same coverage as
// the Widgets view had): a QAccessibleQuickItem with the text and
// editable-text interfaces over the control's cursor and the document,
// so cursor, selection and text queries match what the user sees on the
// page. Registered once through QAccessible::installFactory; Qt owns the
// interfaces.

#include <QAccessible>
#include <QAccessibleEditableTextInterface>
#include <QtQuick/private/qaccessiblequickitem_p.h>

class QTextCursor;

namespace QindaOffice::Apps::Write {

class PageTextItem;

class AccessiblePageTextItem final : public QAccessibleQuickItem,
                                     public QAccessibleEditableTextInterface {
public:
    explicit AccessiblePageTextItem(PageTextItem *item);

    [[nodiscard]] QAccessible::Role role() const override;
    [[nodiscard]] QAccessible::State state() const override;
    [[nodiscard]] QString text(QAccessible::Text t) const override;

    // QAccessibleTextInterface
    void selection(int selectionIndex, int *startOffset, int *endOffset) const override;
    [[nodiscard]] int selectionCount() const override;
    void addSelection(int startOffset, int endOffset) override;
    void removeSelection(int selectionIndex) override;
    void setSelection(int selectionIndex, int startOffset, int endOffset) override;
    [[nodiscard]] int cursorPosition() const override;
    void setCursorPosition(int position) override;
    [[nodiscard]] QString text(int startOffset, int endOffset) const override;
    [[nodiscard]] int characterCount() const override;
    [[nodiscard]] QRect characterRect(int offset) const override;
    [[nodiscard]] int offsetAtPoint(const QPoint &point) const override;
    void scrollToSubstring(int startIndex, int endIndex) override;
    [[nodiscard]] QString attributes(int offset, int *startOffset, int *endOffset) const override;
    // QAccessibleEditableTextInterface
    void deleteText(int startOffset, int endOffset) override;
    void insertText(int offset, const QString &text) override;
    void replaceText(int startOffset, int endOffset, const QString &text) override;

protected:
    void *interface_cast(QAccessible::InterfaceType type) override;

private:
    [[nodiscard]] QTextCursor cursorForPosition(int offset) const;
    PageTextItem *m_item;
};

// Installs the factory (idempotent). Called from WriteHost's ctor.
void installPageTextAccessibility();

} // namespace QindaOffice::Apps::Write
