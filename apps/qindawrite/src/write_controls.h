// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Filling in a Word form in QindaWrite (D-080; the file side is
// docx_controls.h). A click on a content control's check box ticks or
// clears it; on a drop-down list or combo box it offers the items, on a
// date control a calendar (popupRequested, opened by FormFieldPopup.qml);
// a click into a control still showing its placeholder selects the
// placeholder, so typing replaces it. Every change is one undo step on the
// text: the control's state (ticked, chosen item, date) is read back from
// its text when the document is saved, so Undo and Redo need nothing else.

#include <QObject>
#include <QPointF>
#include <QStringList>

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class WriteControls final : public QObject {
    Q_OBJECT
    // The open field: "list", "date" or "" — with its items, its date (ISO,
    // today when its text is not a date) and its title (w:alias).
    Q_PROPERTY(QString kind READ kind NOTIFY activeChanged)
    Q_PROPERTY(QStringList items READ items NOTIFY activeChanged)
    Q_PROPERTY(QString date READ date NOTIFY activeChanged)
    Q_PROPERTY(QString title READ title NOTIFY activeChanged)
public:
    WriteControls(WriteDocument *document, PageTextControl *control, QObject *parent = nullptr);

    [[nodiscard]] QString kind() const { return m_kind; }
    [[nodiscard]] QStringList items() const { return m_items; }
    [[nodiscard]] QString date() const { return m_date; }
    [[nodiscard]] QString title() const { return m_title; }

    // A click at body `position`: `docPoint` (document layout coordinates)
    // must be on a check box's symbol to tick it (an invalid point skips
    // that test); `viewPoint` is where a popup opens (page item
    // coordinates). Returns what it did: "checkbox", "list", "date",
    // "placeholder" or "" (not a form field).
    QString activate(int position, const QPointF &docPoint = QPointF(-1, -1),
                     const QPointF &viewPoint = QPointF());
    // The open list's item `index`, or a date (ISO yyyy-MM-dd) for the open
    // date field, replaces the field's text; false when nothing is open.
    Q_INVOKABLE bool choose(int index);
    Q_INVOKABLE bool chooseDate(const QString &isoDate);
    Q_INVOKABLE void dismiss();

signals:
    void activeChanged();
    // Open the list or the calendar at (x, y) in the page item.
    void popupRequested(qreal x, qreal y);

private:
    // Replaces [m_start, m_end) with `text` in one undo step, keeping the
    // field's run format without the placeholder look.
    bool replaceField(const QString &text);
    [[nodiscard]] bool onSymbol(int start, int end, const QPointF &docPoint) const;

    WriteDocument *m_document;
    PageTextControl *m_control;
    QString m_key;
    int m_start = -1;
    int m_end = -1;
    QString m_kind;
    QStringList m_items;
    QString m_date;
    QString m_title;
};

} // namespace QindaOffice::Apps::Write
