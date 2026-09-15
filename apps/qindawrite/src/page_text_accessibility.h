// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Accessibility for QindaWrite (plan M10): the paged text view exposes a
// QAccessibleInterface + QAccessibleTextInterface to assistive technology,
// delegating to the private QWidgetTextControl so cursor, selection and
// text queries match what the user sees on the page. Registered from
// WriteWindow through an installed factory (Qt owns the interfaces).

#include <QAccessible>
#include <QAccessibleEditableTextInterface>
#include <QAccessibleWidget>
#include <QAccessibleTextInterface>

class QTextCursor;

namespace QindaOffice::Apps::Write {

class PageTextView;

class AccessiblePageTextView final : public QAccessibleWidget,
                                       public QAccessibleTextInterface,
                                       public QAccessibleEditableTextInterface {
public:
    explicit AccessiblePageTextView(PageTextView *view);

    // QAccessibleInterface
    void *interface_cast(QAccessible::InterfaceType type) override;
    [[nodiscard]] QAccessible::Role role() const override;
    [[nodiscard]] QAccessible::State state() const override;
    [[nodiscard]] QString text(QAccessible::Text t) const override;

    // QAccessibleEditableTextInterface
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
    [[nodiscard]] QString attributes(int offset, int *startOffset,
                                     int *endOffset) const override;
    void deleteText(int startOffset, int endOffset) override;
    void insertText(int offset, const QString &text) override;
    void replaceText(int startOffset, int endOffset, const QString &text) override;

private:
    [[nodiscard]] QTextCursor cursorForPosition(int offset) const;
    PageTextView *m_view;
};

// Installs the factory (idempotent). Called from WriteWindow's ctor.
void installPageTextAccessibility();

} // namespace QindaOffice::Apps::Write
