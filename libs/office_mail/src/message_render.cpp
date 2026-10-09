// SPDX-License-Identifier: GPL-3.0-or-later
#include "message_render.h"

#include "html_sanitizer.h"
#include "plain_text_html.h"

#include <QSet>

#include <algorithm>

namespace QindaOffice::Mail {

namespace {

// Pictures attached to a message are shown below the body up to this size
// (bigger ones stay attachments: decoding them for a preview is not worth
// the memory), and at most this many.
constexpr qsizetype kMaximumPreviewBytes = 12 * 1024 * 1024;
constexpr int kMaximumPreviews = 24;
// Qt lays out rich text at roughly 0.3 s per MB of HTML (measured with the
// sanitizer's tests): the pane shows the first 2 M characters of a body and
// says so, instead of freezing on a pathological message.
constexpr qsizetype kMaximumDisplayHtml = 2 * 1024 * 1024;
constexpr qsizetype kMaximumDisplayText = 4 * 1024 * 1024;

int attachmentForCid(const MessageContent &content, const QString &cid)
{
    for (qsizetype i = 0; i < content.attachments.size(); ++i) {
        if (content.attachments.at(i).contentId == cid) return int(i);
    }
    for (qsizetype i = 0; i < content.attachments.size(); ++i) {
        if (content.attachments.at(i).contentId.compare(cid, Qt::CaseInsensitive) == 0) return int(i);
    }
    return -1;
}

} // namespace

QString attachedImageUrl(int index)
{
    return QStringLiteral("qindamail-attachment:%1").arg(index);
}

bool isDisplayableImage(const QString &mimeType)
{
    static const QSet<QString> types{QStringLiteral("image/png"), QStringLiteral("image/jpeg"),
                                     QStringLiteral("image/jpg"), QStringLiteral("image/pjpeg"),
                                     QStringLiteral("image/gif"), QStringLiteral("image/bmp"),
                                     QStringLiteral("image/webp")};
    return types.contains(mimeType.toLower());
}

RenderedBody renderBody(const MessageContent &content, const RenderPolicy &policy)
{
    RenderedBody out;
    QSet<int> shown;
    if (!content.html.trimmed().isEmpty()) {
        HtmlSanitizeOptions options;
        options.allowRemoteImages = policy.allowRemote;
        const SanitizedHtml clean = sanitizeHtml(content.html.left(kMaximumDisplayHtml), options);
        out.html = clean.html;
        out.truncated = clean.truncated || content.html.size() > kMaximumDisplayHtml;
        out.fromHtml = true;
        out.remoteImages = clean.remoteImages;
        out.remoteBlocked = clean.remoteBlocked;
        for (const QString &cid : clean.cidReferences) out.imageUrls.append(cidUrl(cid));
        for (qsizetype i = 0; i < clean.inlineImages.size(); ++i) out.imageUrls.append(inlineImageUrl(int(i)));
        if (policy.allowRemote) out.imageUrls.append(clean.remoteImages);
        for (const QString &cid : clean.cidReferences) {
            const int index = attachmentForCid(content, cid);
            if (index < 0 || content.attachments.at(index).data.isEmpty()) continue;
            out.images.append({cidUrl(cid), content.attachments.at(index).data});
            shown.insert(index);
            if (!out.cidAttachments.contains(index)) out.cidAttachments.append(index);
        }
        for (qsizetype i = 0; i < clean.inlineImages.size(); ++i)
            out.images.append({inlineImageUrl(int(i)), clean.inlineImages.at(i)});
        if (policy.allowRemote) {
            for (const QString &url : clean.remoteImages) {
                const auto found = policy.remoteImages.constFind(url);
                if (found != policy.remoteImages.cend() && !found->isEmpty()) out.images.append({url, *found});
            }
        }
    } else {
        out.html = plainTextToHtml(content.plain.left(kMaximumDisplayText));
        out.truncated = content.plain.size() > kMaximumDisplayText;
    }
    if (policy.attachedImagesBelow) {
        QString previews;
        int count = 0;
        for (qsizetype i = 0; i < content.attachments.size() && count < kMaximumPreviews; ++i) {
            const MessageAttachment &part = content.attachments.at(i);
            // An inline part the HTML never referenced is still a picture
            // of the message: show it below, as Outlook does.
            if (shown.contains(int(i)) || !isDisplayableImage(part.mimeType) || part.data.isEmpty()
                || part.data.size() > kMaximumPreviewBytes) {
                continue;
            }
            previews += QStringLiteral("<p><img src=\"%1\" alt=\"%2\"/></p>")
                            .arg(attachedImageUrl(int(i)), part.name.toHtmlEscaped());
            out.images.append({attachedImageUrl(int(i)), part.data});
            out.imageUrls.append(attachedImageUrl(int(i)));
            shown.insert(int(i));
            ++count;
        }
        if (!previews.isEmpty()) {
            // Inside the body when the sanitized HTML closes one.
            const qsizetype end = out.html.lastIndexOf(QStringLiteral("</body>"), -1, Qt::CaseInsensitive);
            out.html.insert(end < 0 ? out.html.size() : end, QStringLiteral("<hr/>") + previews);
        }
    }
    out.inlineAttachments = QList<int>(shown.cbegin(), shown.cend());
    std::sort(out.inlineAttachments.begin(), out.inlineAttachments.end());
    return out;
}

} // namespace QindaOffice::Mail
