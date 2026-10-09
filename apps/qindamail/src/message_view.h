// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The reading pane's body (M64, D-072): shows one message in a QML
// `TextEdit { textFormat: TextEdit.RichText; readOnly: true }` — HTML mail as
// formatted text (sanitized, remote pictures blocked until the user asks),
// inline `cid:` pictures, attached pictures below the body, plain text with
// clickable links — selectable and copyable like any text.
//
// AGENT-CONTRACT (ReadingPane.qml): bind `document: body.textDocument`,
// `messageId`, `service` and `maxImageWidth`; never bind the TextEdit's
// `text` (this object sets the document). `remoteBlocked` drives the "Show
// pictures" bar; `hiddenAttachmentIds` are attachment ids the body already
// shows in place (cid parts), which the attachment list leaves out.
// The body appears at once from what this computer has; an IMAP message's
// source is fetched in the background (`loading`) and the body re-renders.

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QStringList>
#include <QVariantMap>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include "message_document.h"

namespace QindaOffice::Apps::Mail {

class MailService;

// The property type must be complete for moc (Qt 6).
Q_MOC_INCLUDE("mail_service.h")

class MessageView : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaOffice::Apps::Mail::MailService *service READ service WRITE setService NOTIFY serviceChanged)
    Q_PROPERTY(QString messageId READ messageId WRITE setMessageId NOTIFY messageIdChanged)
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(qreal maxImageWidth READ maxImageWidth WRITE setMaxImageWidth NOTIFY maxImageWidthChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool formatted READ formatted NOTIFY changed)
    Q_PROPERTY(bool remoteBlocked READ remoteBlocked NOTIFY changed)
    Q_PROPERTY(bool picturesShown READ picturesShown NOTIFY changed)
    Q_PROPERTY(bool senderTrusted READ senderTrusted NOTIFY changed)
    Q_PROPERTY(QString senderAddress READ senderAddress NOTIFY changed)
    Q_PROPERTY(QStringList hiddenAttachmentIds READ hiddenAttachmentIds NOTIFY changed)
    Q_PROPERTY(int pictureCount READ pictureCount NOTIFY changed)
    Q_PROPERTY(int remoteImageCount READ remoteImageCount NOTIFY changed)
    Q_PROPERTY(bool truncated READ truncated NOTIFY changed)
    // MailService::meetingOf for a meeting request ({} otherwise).
    Q_PROPERTY(QVariantMap meeting READ meeting NOTIFY changed)
public:
    explicit MessageView(QObject *parent = nullptr);
    ~MessageView() override;

    [[nodiscard]] MailService *service() const { return m_service; }
    void setService(MailService *service);
    [[nodiscard]] QString messageId() const { return m_messageId; }
    void setMessageId(const QString &id);
    [[nodiscard]] QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
    [[nodiscard]] qreal maxImageWidth() const { return m_maxImageWidth; }
    void setMaxImageWidth(qreal width);
    [[nodiscard]] bool loading() const { return m_loading; }
    [[nodiscard]] bool formatted() const { return m_formatted; }
    [[nodiscard]] bool remoteBlocked() const { return m_remoteBlocked; }
    [[nodiscard]] bool picturesShown() const;
    [[nodiscard]] bool senderTrusted() const;
    [[nodiscard]] QString senderAddress() const { return m_senderAddress; }
    [[nodiscard]] QStringList hiddenAttachmentIds() const { return m_hidden; }
    [[nodiscard]] int pictureCount() const { return int(m_images.size()); }
    [[nodiscard]] int remoteImageCount() const { return m_remoteImageCount; }
    [[nodiscard]] QVariantMap meeting() const { return m_meeting; }
    [[nodiscard]] bool truncated() const { return m_truncated; }

    // "Show pictures" for this message; "always for this sender" on/off.
    Q_INVOKABLE void showPictures();
    Q_INVOKABLE void setSenderTrusted(bool trusted);
    // The plain text of the body (tests, the accessibility name).
    Q_INVOKABLE QString plainText() const;

signals:
    void serviceChanged();
    void messageIdChanged();
    void documentChanged();
    void maxImageWidthChanged();
    void changed();

private:
    void render();
    void scheduleRender();
    void requestSource();

    QPointer<MailService> m_service;
    QPointer<QQuickTextDocument> m_document;
    QString m_messageId;
    QString m_senderAddress;
    QStringList m_hidden;
    QVariantMap m_meeting;
    MessageImages m_images;
    QTimer m_renderTimer;
    qreal m_maxImageWidth = 0;
    qreal m_renderedWidth = 0;
    quint64 m_generation = 0;
    int m_remoteImageCount = 0;
    bool m_loading = false;
    bool m_formatted = false;
    bool m_remoteBlocked = false;
    bool m_truncated = false;
};

} // namespace QindaOffice::Apps::Mail
