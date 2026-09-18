// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_clipboard.h"

#include "shape_json.h"
#include "slide_canvas_item.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

namespace QindaOffice::Apps::Show {

namespace {
const char kMime[] = "application/x-qindashow-shapes";
const qreal kPasteOffset = 12.0; // pt, one visual grid step
} // namespace

const char *shapeClipboardMimeType()
{
    return kMime;
}

void copySelectedShape(SlideCanvasItem *canvas)
{
    const ShowModel::Shape *shape = canvas->selectedShape();
    if (shape == nullptr) {
        return;
    }
    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(kMime), ShowModel::shapesToJson({*shape}));
    const QString text = shape->displayText();
    if (!text.isEmpty()) {
        mime->setText(text);
    }
    QGuiApplication::clipboard()->setMimeData(mime);
}

void cutSelectedShape(SlideCanvasItem *canvas)
{
    if (canvas->selectedShape() == nullptr) {
        return;
    }
    copySelectedShape(canvas);
    canvas->deleteSelected(); // existing shapeRemoved undo payload
}

bool pasteShapeAtClipboard(SlideCanvasItem *canvas)
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (mime == nullptr || !mime->hasFormat(QString::fromLatin1(kMime))) {
        return false;
    }
    const QList<ShowModel::Shape> shapes =
        ShowModel::shapesFromJson(mime->data(QString::fromLatin1(kMime)));
    if (shapes.isEmpty()) {
        return false;
    }
    // v1 clipboard carries the single selected shape; the codec and the
    // paste loop are shape-list ready.
    for (ShowModel::Shape shape : shapes) {
        shape.rect.translate(kPasteOffset, kPasteOffset);
        canvas->insertShape(shape);
    }
    return true;
}

} // namespace QindaOffice::Apps::Show
