// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A rendered message body in a QTextDocument (M64, D-072): the shared step
// of the reading pane (MessageView, a QML TextEdit's document) and the
// printer (MessagePrinter, a private document): decode the renderer's image
// bytes, register EVERY image URL the HTML names as a document resource, set
// the HTML, and scale pictures down to the width available.
//
// AGENT-GUARD (privacy): QTextDocument::resource() looks in the registered
// resources first and only then calls loadResource() — which, for a QML
// TextEdit's document, is QQuickTextEdit's loader: a network fetch through
// the QML engine (and for a plain document a local-file read). A resource
// provider is consulted only AFTER that loader, so it is no guard. Hence
// setBody(): clear() (which drops the previous message's resources), every
// URL of RenderedBody::imageUrls registered — the decoded picture or an
// empty image — and only then setHtml(). message_render.h keeps remote URLs
// out of the HTML unless the user allowed them, and those, too, are
// registered before any layout can ask for them.

#include "message_render.h"

#include <QHash>
#include <QImage>
#include <QUrl>

class QTextDocument;

namespace QindaOffice::Apps::Mail {

using MessageImages = QHash<QUrl, QImage>;

// Decodes each image (bounded: a picture over 10 000 px a side or 40
// megapixels is skipped, never allocated); EXIF orientation applied.
MessageImages decodeMessageImages(const QindaOffice::Mail::RenderedBody &body);

// Replaces the document's content with `body`: clears it, registers every
// image URL of the body (decoded or empty), then sets the HTML.
void setBody(QTextDocument *document, const QindaOffice::Mail::RenderedBody &body, const MessageImages &images);

// Pictures wider than `maxWidth` (document units) are scaled to it, keeping
// their aspect; others keep the size the HTML gave them.
void constrainImages(QTextDocument *document, const MessageImages &images, qreal maxWidth);

} // namespace QindaOffice::Apps::Mail
