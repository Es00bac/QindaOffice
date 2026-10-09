// SPDX-License-Identifier: GPL-3.0-or-later
#include "message_view.h"

#include "mail_service.h"
#include "message_content.h"
#include "message_render.h"

#include <QTextDocument>

#include <cmath>

namespace QindaOffice::Apps::Mail {

namespace {

// Re-render after the pane's width settles (a splitter drag), not per pixel.
constexpr int kWidthSettleMs = 150;
// AGENT-GUARD: hysteresis wider than a scroll bar. A re-render can show or
// hide the pane's scroll bar, which changes the width again; reacting to
// that would re-render forever.
constexpr qreal kWidthHysteresis = 32;
// The "loading" hint appears only for a source that is really on its way.
constexpr int kLoadingHintMs = 250;

} // namespace

MessageView::MessageView(QObject *parent)
    : QObject(parent)
{
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(0);
    connect(&m_renderTimer, &QTimer::timeout, this, &MessageView::render);
}

MessageView::~MessageView() = default;

void MessageView::setService(MailService *service)
{
    if (service == m_service) return;
    if (m_service != nullptr) disconnect(m_service, nullptr, this, nullptr);
    m_service = service;
    if (service != nullptr) {
        connect(service, &MailService::sourceLoaded, this, [this](const QString &id) {
            if (id == m_messageId) scheduleRender();
        });
        connect(service, &MailService::picturesChanged, this, [this](const QString &id) {
            if (id.isEmpty() || id == m_messageId) scheduleRender();
        });
        connect(service, &MailService::meetingChanged, this, [this](const QString &id) {
            if (id == m_messageId) scheduleRender();
        });
    }
    emit serviceChanged();
    requestSource();
    scheduleRender();
}

void MessageView::setMessageId(const QString &id)
{
    if (id == m_messageId) return;
    m_messageId = id;
    ++m_generation;
    emit messageIdChanged();
    requestSource();
    // At once from what this computer has: the pane never shows the
    // previous message's body under the new headers.
    render();
}

void MessageView::setDocument(QQuickTextDocument *document)
{
    if (document == m_document) return;
    m_document = document;
    emit documentChanged();
    scheduleRender();
}

void MessageView::setMaxImageWidth(qreal width)
{
    if (qFuzzyCompare(width, m_maxImageWidth)) return;
    m_maxImageWidth = width;
    emit maxImageWidthChanged();
    if (std::abs(width - m_renderedWidth) >= kWidthHysteresis && pictureCount() > 0) m_renderTimer.start(kWidthSettleMs);
}

bool MessageView::picturesShown() const
{
    return m_service != nullptr && (m_service->picturesShown(m_messageId) || senderTrusted());
}

bool MessageView::senderTrusted() const
{
    return m_service != nullptr && !m_senderAddress.isEmpty() && m_service->senderPicturesAllowed(m_senderAddress);
}

void MessageView::showPictures()
{
    if (m_service != nullptr && !m_messageId.isEmpty()) m_service->showPictures(m_messageId);
}

void MessageView::setSenderTrusted(bool trusted)
{
    if (m_service != nullptr && !m_senderAddress.isEmpty()) m_service->setSenderPicturesAllowed(m_senderAddress, trusted);
}

QString MessageView::plainText() const
{
    return m_document != nullptr ? m_document->textDocument()->toPlainText() : QString();
}

void MessageView::scheduleRender()
{
    if (!m_renderTimer.isActive() || m_renderTimer.interval() > 0) {
        m_renderTimer.setInterval(0);
        m_renderTimer.start();
    }
}

void MessageView::requestSource()
{
    if (m_service == nullptr || m_messageId.isEmpty()) {
        if (m_loading) {
            m_loading = false;
            emit changed();
        }
        return;
    }
    const quint64 generation = m_generation;
    QPointer<MessageView> self(this);
    auto answered = std::make_shared<bool>(false);
    QTimer::singleShot(kLoadingHintMs, this, [self, generation, answered] {
        if (self == nullptr || *answered || self->m_generation != generation) return;
        self->m_loading = true;
        emit self->changed();
    });
    m_service->loadSource(m_messageId, [self, generation, answered](const QByteArray &, const QString &) {
        *answered = true;
        if (self == nullptr || self->m_generation != generation) return;
        if (self->m_loading) {
            self->m_loading = false;
            emit self->changed();
        }
        self->scheduleRender();
    });
}

void MessageView::render()
{
    m_renderTimer.stop();
    if (m_document == nullptr) return;
    QTextDocument *document = m_document->textDocument();
    if (m_service == nullptr || m_messageId.isEmpty()) {
        m_images.clear();
        document->clear();
        m_hidden.clear();
        m_senderAddress.clear();
        m_formatted = false;
        m_remoteBlocked = false;
        m_remoteImageCount = 0;
        m_truncated = false;
        m_meeting.clear();
        emit changed();
        return;
    }
    const QindaOffice::Mail::MessageContent content = m_service->contentOf(m_messageId);
    const QindaOffice::Mail::RenderPolicy policy = m_service->renderPolicy(m_messageId);
    const QindaOffice::Mail::RenderedBody body = QindaOffice::Mail::renderBody(content, policy);
    m_images = decodeMessageImages(body);
    setBody(document, body, m_images);
    constrainImages(document, m_images, m_maxImageWidth);
    m_renderedWidth = m_maxImageWidth;
    m_formatted = body.fromHtml;
    m_remoteBlocked = body.remoteBlocked;
    m_remoteImageCount = int(body.remoteImages.size());
    m_truncated = body.truncated;
    m_senderAddress = m_service->messageById(m_messageId).value(QStringLiteral("senderAddress")).toString();
    m_meeting = content.calendar.isEmpty() ? QVariantMap() : m_service->meetingOf(m_messageId);
    m_hidden.clear();
    for (const int index : body.cidAttachments) {
        const QString section = content.attachments.at(index).section;
        if (!section.isEmpty()) m_hidden.append(m_messageId + QLatin1Char('/') + section);
    }
    // The invitation's calendar part is the meeting card, not a file to open.
    if (!m_meeting.isEmpty()) {
        for (const QindaOffice::Mail::MessageAttachment &part : content.attachments) {
            if (part.mimeType == QLatin1String("text/calendar") && !part.section.isEmpty())
                m_hidden.append(m_messageId + QLatin1Char('/') + part.section);
        }
    }
    // Allowed pictures not downloaded yet (a trusted sender's new message).
    if (policy.allowRemote) {
        for (const QString &url : body.remoteImages) {
            if (!policy.remoteImages.contains(url)) {
                m_service->fetchRemoteImages(m_messageId);
                break;
            }
        }
    }
    emit changed();
}

} // namespace QindaOffice::Apps::Mail
