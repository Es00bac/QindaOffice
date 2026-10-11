// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PageTextItem (conversion plan §5.1): the QindaWrite editor surface as a
// QtQuick item. It paints every page of the document — paper, the text
// through PageTextControl, the header/footer/footnote bands through the
// same painter print and PDF use — in DOCUMENT units, stacked with gaps
// (PageGeometry at zoom 1); the Tk.Viewport around it zooms and scrolls.
// Pointer, key and input-method events go to the control with their
// positions mapped page → document; the control's caret movements come
// back as ensureVisibleRequested for the viewport. The host owns the
// document and the control and hands both over once (attach).

#include "page_geometry.h"
#include "page_setup_io.h"
#include "page_text_control.h"

#include <QPointer>
#include <QQuickPaintedItem>
#include <QMetaObject>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace QindaOffice::Apps::Write {

class PictureTool;
class StoryEditor;
class WriteDocument;

class PageTextItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageCountChanged)
    Q_PROPERTY(qreal maxPaintHeight READ maxPaintHeight NOTIFY maxPaintHeightChanged)
    // The hosting viewport's zoom, for the status line only.
    Q_PROPERTY(qreal viewZoom READ viewZoom WRITE setViewZoom NOTIFY viewZoomChanged)
    Q_PROPERTY(bool attached READ isAttached NOTIFY attachedChanged)
    // In-place header/footer editing (page_story_editor.h).
    Q_PROPERTY(bool editingStory READ isEditingStory NOTIFY storyChanged)
    Q_PROPERTY(QString storyLabel READ storyLabel NOTIFY storyChanged)
public:
    explicit PageTextItem(QQuickItem *parent = nullptr);
    ~PageTextItem() override;

    void attach(WriteDocument *document, PageTextControl *control);
    [[nodiscard]] bool isAttached() const { return m_control != nullptr; }
    [[nodiscard]] WriteDocument *writeDocument() const { return m_document; }
    [[nodiscard]] PageTextControl *control() const { return m_control; }
    [[nodiscard]] PageGeometry *geometry() { return &m_geometry; }
    [[nodiscard]] const PageGeometry &geometry() const { return m_geometry; }

    [[nodiscard]] int pageCount() const;
    [[nodiscard]] qreal maxPaintHeight() const;
    [[nodiscard]] qreal viewZoom() const { return m_viewZoom; }
    void setViewZoom(qreal zoom);

    // Page (item) coordinates ↔ document coordinates.
    [[nodiscard]] QPointF viewToDoc(const QPointF &viewPos) const;
    [[nodiscard]] QPointF docToView(const QPointF &docPos) const;
    [[nodiscard]] QRectF docToView(const QRectF &docRect) const;
    // The caret in item coordinates (input method, accessibility).
    [[nodiscard]] QRectF cursorViewRect() const;

    void paint(QPainter *painter) override;

    // Header and footer of the page the caret is on (the body's caret
    // while editing the body); Close returns to the body.
    [[nodiscard]] bool isEditingStory() const;
    [[nodiscard]] QString storyLabel() const;
    [[nodiscard]] StoryEditor *storyEditor() const { return m_story.get(); }
    // Picture selection and resizing (page_picture_tool.h).
    [[nodiscard]] PictureTool *pictures() const { return m_pictures.get(); }
    Q_INVOKABLE void editHeader();
    Q_INVOKABLE void editFooter();
    Q_INVOKABLE void closeStory();

    // Mail merge (M77, D-086): Preview Results shows `document` — a merged
    // copy laid out like the body — read-only instead of the body; null
    // returns to the body. Highlight Merge Fields shades the body's merge
    // fields (display only).
    void setPreviewDocument(QTextDocument *document);
    [[nodiscard]] QTextDocument *previewDocument() const { return m_preview; }
    void setMergeHighlight(bool on);

signals:
    void pageCountChanged();
    void maxPaintHeightChanged();
    void viewZoomChanged();
    void attachedChanged();
    // AGENT-CONTRACT: this rectangle is in the full document surface's
    // coordinates, while docToView() and storyToView() return paint-tile
    // coordinates. The QML viewport scrolls the full surface.
    void ensureVisibleRequested(const QRectF &viewRect);
    // A right press at item (x, y); docPos is where it landed in the text.
    void contextMenuRequested(qreal x, qreal y, const QPointF &docPos);
    void doubleClicked(int position);
    // A left click in the body that selected nothing: at item (x, y),
    // `docPos` in the text's layout, the caret at `position` — form
    // fields answer it (write_controls.h).
    void clicked(qreal x, qreal y, const QPointF &docPos, int position);
    // A left press in the body was released: a selection (or a click) is
    // made — the Format Painter paints it (write_painter.h, M73).
    void selectionReleased();
    void storyChanged();
    // A double-click on a picture: the Picture dialog for the selection.
    void pictureDialogRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;

private:
    QMetaObject::Connection m_screenConnection;
    void forwardMouse(QMouseEvent *event);
    // The open story's events (story coordinates); false when a press fell
    // outside its band.
    bool forwardStoryMouse(QMouseEvent *event);
    // Opens the story whose band was double-clicked; false elsewhere.
    bool openStoryAt(QMouseEvent *event);
    // The page under an item point (-1 in no page) and the point on it.
    [[nodiscard]] int pageAt(const QPointF &viewPos, QPointF *pagePos = nullptr) const;
    // The page the body's caret is on (0-based).
    [[nodiscard]] int caretPage() const;
    [[nodiscard]] QRectF storyToView(const QRectF &storyRect) const;
    void openStory(int kind);
    void updatePageMetrics();
    // page_text_paint.cpp
    void paintPage(QPainter *painter, int index, const QRectF &clip, const PageDecoration &decoration);
    void paintBody(QPainter *painter, const QRectF &pageArea, bool storyOpen, bool overTextPictures);

    QPointer<WriteDocument> m_document;
    QPointer<PageTextControl> m_control;
    QPointer<QTextDocument> m_preview;
    bool m_mergeHighlight = false;
    std::unique_ptr<StoryEditor> m_story;
    std::unique_ptr<PictureTool> m_pictures;
    PageGeometry m_geometry;
    qreal m_viewZoom = 1.0;
    int m_pageCount = 1;
    // The body press a release may turn into clicked().
    QPointF m_pressPoint;
    bool m_pressForwarded = false;
};

} // namespace QindaOffice::Apps::Write
