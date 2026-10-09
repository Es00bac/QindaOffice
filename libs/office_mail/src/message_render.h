// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MessageContent → what QindaMail displays and prints (M64, D-072): the
// body as safe HTML for Qt's rich-text engine plus the image bytes it
// refers to — inline `cid:` parts of the message, `data:` images, attached
// pictures shown below the body and, only when the user allowed them for
// this message or sender, remote images the app fetched. Plain-text mail
// becomes escaped HTML with clickable links.
//
// AGENT-CONTRACT (the QindaMail reading pane and message printer):
// - `images` maps the exact URL strings the HTML uses to encoded image
//   bytes; `imageUrls` lists EVERY image URL the HTML names. The caller
//   decodes the bytes (bounded) and registers every URL of `imageUrls` with
//   QTextDocument::addResource(ImageResource, QUrl(url), image or an empty
//   image) BEFORE setting the HTML (the app's message_document.h), so no
//   remote or local file is ever fetched by the text engine.
// - `inlineAttachments` are indexes into `content.attachments` that the
//   body shows (inline cid parts, attached pictures); the attachment list
//   hides inline cid parts only.
// - Remote images appear in the HTML only with policy.allowRemote; their
//   bytes come from policy.remoteImages (url → bytes) — the renderer never
//   fetches anything itself.

#include "message_content.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace QindaOffice::Mail {

struct RenderPolicy {
    bool allowRemote = false;
    QHash<QString, QByteArray> remoteImages;   // fetched remote URL → bytes
    bool attachedImagesBelow = true;           // show image attachments after the body
};

struct RenderedBody {
    QString html;                               // safe HTML (body only, no headers)
    QList<QPair<QString, QByteArray>> images;   // URL string → encoded image bytes
    QStringList remoteImages;                   // every remote image URL the body references
    bool remoteBlocked = false;                 // remote content was left out (offer "Show pictures")
    bool fromHtml = false;                      // the body was text/html (else text/plain)
    QList<int> inlineAttachments;               // attachment indexes the body shows
    QList<int> cidAttachments;                  // …of which the HTML itself places (cid:), not listed again
    bool truncated = false;                     // only the start of a very long body is shown
    // EVERY image URL the HTML names (resolved or not): the caller registers
    // each one, an empty image for those without bytes, so no loader of the
    // text engine ever runs (message_document.h).
    QStringList imageUrls;
};

RenderedBody renderBody(const MessageContent &content, const RenderPolicy &policy);

// The URL the renderer gives attachment `index` when it shows it below the body.
QString attachedImageUrl(int index);

// True for the image types Qt reads everywhere (png, jpeg, gif, bmp, webp).
bool isDisplayableImage(const QString &mimeType);

} // namespace QindaOffice::Mail
