// SPDX-License-Identifier: GPL-3.0-or-later
#include "anthropic_provider.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

namespace QindaOffice::Ai {

namespace {

QString imageToBase64Png(const QImage &image)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QString::fromLatin1(png.toBase64());
}

} // namespace

class AnthropicRun final : public ChatRun {
    Q_OBJECT
public:
    AnthropicRun(AnthropicProvider *provider, const ChatRequest &request)
        : ChatRun(provider)
    {
        QJsonObject body;
        body.insert(QStringLiteral("model"), provider->m_model);
        body.insert(QStringLiteral("max_tokens"), request.maxTokens);
        body.insert(QStringLiteral("stream"), true);
        QJsonArray messages;
        for (const ChatMessage &message : request.messages) {
            if (message.role == QLatin1String("system")) {
                continue; // system goes in the top-level field
            }
            QJsonObject entry{{QStringLiteral("role"), message.role}};
            if (message.images.isEmpty()) {
                entry.insert(QStringLiteral("content"), message.text);
            } else {
                QJsonArray content;
                content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                           {QStringLiteral("text"), message.text}});
                for (const QImage &image : message.images) {
                    content.append(QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("image")},
                        {QStringLiteral("source"),
                         QJsonObject{{QStringLiteral("type"), QStringLiteral("base64")},
                                     {QStringLiteral("media_type"), QStringLiteral("image/png")},
                                     {QStringLiteral("data"), imageToBase64Png(image)}}}});
                }
                entry.insert(QStringLiteral("content"), content);
            }
            messages.append(entry);
        }
        body.insert(QStringLiteral("messages"), messages);
        if (!request.systemPrompt.isEmpty()) {
            body.insert(QStringLiteral("system"), request.systemPrompt);
        }
        QNetworkRequest network(QUrl(provider->m_baseUrl + QStringLiteral("/v1/messages")));
        network.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        network.setRawHeader("x-api-key", provider->m_apiKey.toUtf8());
        network.setRawHeader("anthropic-version", "2023-06-01");
        QNetworkReply *reply = provider->m_nam.post(
            network, QJsonDocument(body).toJson(QJsonDocument::Compact));
        m_reply = reply;
        m_idleTimer.setSingleShot(true);
        m_idleTimer.start(60000);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
            m_idleTimer.start(60000);
            m_parser.append(reply->readAll());
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            m_idleTimer.stop();
            m_parser.finish();
            // Cancellation must surface as failed(Cancelled), not as a
            // "successful" truncated answer (the earlier release had no
            // cancelled branch, so Stop mid-stream enabled Insert/Replace
            // on partial text).
            if (m_cancelled) {
                emitFailed({ChatError::Cancelled, QStringLiteral("cancelled"), false});
                reply->deleteLater();
                return;
            }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401) {
                emitFailed({ChatError::Auth, tr("API key rejected"), false});
            } else if (status == 429) {
                emitFailed({ChatError::RateLimited, tr("rate limited"), true});
            } else if (status < 200 || status >= 300) {
                emitFailed({ChatError::Unknown, tr("HTTP %1").arg(status), status >= 500});
            } else if (m_stopReason == QLatin1String("max_tokens")) {
                emitFailed({ChatError::Unknown, tr("answer hit max_tokens"), true});
            } else {
                emitFinished({m_text, m_usage});
            }
            reply->deleteLater();
        });
        connect(&m_idleTimer, &QTimer::timeout, this, [this, reply] {
            emitFailed({ChatError::Timeout, tr("stream idle for 60 s"), true});
            reply->abort();
        });
    }

    void cancel() override
    {
        m_cancelled = true;
        if (m_reply != nullptr) {
            m_reply->abort();
        }
    }

private:
    QNetworkReply *m_reply = nullptr;
    bool m_cancelled = false;
    QTimer m_idleTimer{this};
    QString m_text;
    QString m_stopReason;
    ChatUsage m_usage;
    SseParser m_parser{[this](const QString &event, const QString &data) {
        const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
        if (!doc.isObject()) {
            return;
        }
        const QJsonObject object = doc.object();
        if (event == QLatin1String("content_block_delta")) {
            const QJsonObject delta = object.value(QStringLiteral("delta")).toObject();
            if (delta.value(QStringLiteral("type")).toString()
                == QLatin1String("text_delta")) {
                const QString piece = delta.value(QStringLiteral("text")).toString();
                m_text += piece;
                emit textDelta(piece);
            }
        } else if (event == QLatin1String("message_delta")) {
            m_stopReason = object.value(QStringLiteral("delta"))
                               .toObject()
                               .value(QStringLiteral("stop_reason"))
                               .toString();
            const QJsonObject usage = object.value(QStringLiteral("usage")).toObject();
            m_usage.inputTokens += usage.value(QStringLiteral("input_tokens")).toInt();
            m_usage.outputTokens += usage.value(QStringLiteral("output_tokens")).toInt();
        } else if (event == QLatin1String("error")) {
            const QJsonObject error = object.value(QStringLiteral("error")).toObject();
            emitFailed({ChatError::Unknown,
                         error.value(QStringLiteral("message")).toString(), true});
        }
    }};
};

AnthropicProvider::AnthropicProvider(QString baseUrl, QString apiKey, QString model, QObject *parent)
    : ChatProvider(parent)
    , m_baseUrl(std::move(baseUrl))
    , m_apiKey(std::move(apiKey))
    , m_model(std::move(model))
{
}

ChatRun *AnthropicProvider::start(const ChatRequest &request)
{
    return new AnthropicRun(this, request);
}

} // namespace QindaOffice::Ai

#include "anthropic_provider.moc"
