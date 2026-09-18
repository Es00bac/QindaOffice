// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow shape clipboard (M13): copies the selected shape as
// application/x-qindashow-shapes (JSON via show_model's codec, so images
// and text styling survive) plus text/plain of its display text. Paste
// lands a fresh-id copy offset by one grid step and selects it; the
// insert goes through the canvas so the normal undo payload flows.

namespace QindaOffice::Apps::Show {

class SlideCanvasItem;

void copySelectedShape(SlideCanvasItem *canvas);
void cutSelectedShape(SlideCanvasItem *canvas);
bool pasteShapeAtClipboard(SlideCanvasItem *canvas);

[[nodiscard]] const char *shapeClipboardMimeType();

} // namespace QindaOffice::Apps::Show
