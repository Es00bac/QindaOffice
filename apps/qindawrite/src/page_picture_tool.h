// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Selecting and resizing pictures on the page, as in Word: a click on a
// picture selects it (the editing control's selection is its one object
// character), eight handles are drawn around it, a corner handle scales it
// with its aspect ratio (Shift: freely), a side handle stretches one
// dimension, and the new size is one undo step. The Picture dialog (size,
// aspect lock, alternative text) edits the same character through
// setPicture(). Everything here speaks DOCUMENT coordinates; the page item
// maps. A picture keeps its original bytes (WordParts::media): only its
// display size changes, and the .docx writer patches the extents of a Word
// picture's own drawing (alt text, crop and anchoring stay).
//
// Floating pictures (M77, docx_float.h): a click on one selects it (one
// behind the text only where no text is under the pointer), and dragging
// a selected floating picture by its body places it — new offsets in what
// its position is relative to, one undo step.

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

class QPainter;
class QTextDocument;

namespace QindaOffice::Apps::Write {

class PageTextControl;

class PictureTool {
public:
    explicit PictureTool(PageTextControl *control);

    // The picture character under `docPos`, or -1.
    [[nodiscard]] int pictureAt(const QPointF &docPos) const;
    // The selected picture (the selection is exactly one picture), or -1.
    [[nodiscard]] int selected() const;
    void select(int position);
    // Where the picture at `position` is drawn (document coordinates).
    [[nodiscard]] QRectF pictureRect(int position) const;
    // A handle of the selected picture under `docPos`: 0–7 clockwise from
    // the top-left corner, or -1.
    [[nodiscard]] int handleAt(const QPointF &docPos) const;

    bool beginDrag(int handle, const QPointF &docPos);
    // Starts placing the selected floating picture under `docPos`; false
    // for an inline picture or a point off it.
    bool beginMove(const QPointF &docPos);
    // Moves the floating picture at `position` by `delta`, one undo step.
    bool movePicture(int position, const QPointF &delta);
    // Whether the selected picture floats (wrap other than In Line).
    [[nodiscard]] bool selectedFloats() const;
    void dragTo(const QPointF &docPos, bool freeAspect);
    // Applies the dragged size; false when there was no drag.
    bool finishDrag();
    void cancelDrag()
    {
        m_handle = -1;
        m_moving = false;
    }
    [[nodiscard]] bool isDragging() const { return m_handle >= 0 || m_moving; }

    // The picture's size and natural (pixel) size, and its alt text.
    [[nodiscard]] QSizeF pictureSize(int position) const;
    [[nodiscard]] QSizeF naturalSize(int position) const;
    [[nodiscard]] QString altText(int position) const;
    // Size (px) and alt text, as one undo step; false for no picture there.
    bool setPicture(int position, const QSizeF &size, const QString &altText);

    // Handles around the selected picture and, while dragging, the new
    // outline, for the part of the document in `clip`.
    void draw(QPainter *painter, const QRectF &clip) const;

private:
    [[nodiscard]] QTextDocument *document() const;
    [[nodiscard]] QList<QRectF> handleRects(const QRectF &picture) const;
    // The positions of the floating pictures, in text order.
    [[nodiscard]] QList<int> floatingPictures() const;

    PageTextControl *m_control;
    int m_handle = -1;
    int m_position = -1;
    QRectF m_start;
    QRectF m_drag;
    bool m_moving = false;
    QPointF m_moveFrom;
};

} // namespace QindaOffice::Apps::Write
