// SPDX-License-Identifier: GPL-3.0-or-later
#include "http_chat_provider.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

namespace QindaOffice::Ai {

namespace {

QString imageToDataUri(const QImage &image)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

} // namespace

class OpenAiChatRun final : public ChatRun {
    Q_OBJECT
public:
    OpenAiChatRun(OpenAiChatProvider *provider, const ChatRequest &request)
        : ChatRun(provider)
        , m_provider(provider)
    {
        QJsonObject body;
        body.insert(QStringLiteral("model"), provider->m_model);
        body.insert(QStringLiteral("stream"), true);
        body.insert(QStringLiteral("stream_options"),
                    QJsonObject{{QStringLiteral("include_usage"), true}});
        body.insert(QStringLiteral("max_tokens"), request.maxTokens);
        body.insert(QStringLiteral("temperature"), request.temperature);
        if (!request.effort.isEmpty()) {
            body.insert(QStringLiteral("reasoning_effort"), request.effort);
        }
        if (request.jsonSchema.has_value()) {
            body.insert(QStringLiteral("response_format"),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("json_schema")},
                                    {QStringLiteral("json_schema"), *request.jsonSchema}});
        }
        QJsonArray messages;
        if (!request.systemPrompt.isEmpty()) {
            messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                        {QStringLiteral("content"), request.systemPrompt}});
        }
        for (const ChatMessage &message : request.messages) {
            QJsonObject entry;
            entry.insert(QStringLiteral("role"), message.role);
            if (message.images.isEmpty()) {
                entry.insert(QStringLiteral("content"), message.text);
            } else {
                QJsonArray content;
                content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                           {QStringLiteral("text"), message.text}});
                for (const QImage &image : message.images) {
                    content.append(QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("image_url")},
                        {QStringLiteral("image_url"),
                         QJsonObject{{QStringLiteral("url"), imageToDataUri(image)}}}});
                }
                entry.insert(QStringLiteral("content"), content);
            }
            messages.append(entry);
        }
        body.insert(QStringLiteral("messages"), messages);

        QNetworkReply *reply = m_provider->m_nam.post(m_provider->buildRequest(),
                                                      QJsonDocument(body).toJson(QJsonDocument::Compact));
        m_reply = reply;
        // AGENT-NOTE: no transfer timeout - the own idle timer below fires
        // after 60 s without bytes, which also covers "held open" streams.
        m_idleTimer.setSingleShot(true);
        m_idleTimer.start(60000);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
            m_idleTimer.start(60000);
            m_parser.append(reply->readAll());
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            m_idleTimer.stop();
            m_parser.finish();
            if (m_cancelled) {
                emit failed({ChatError::Cancelled, QStringLiteral("cancelled"), false});
                reply->deleteLater();
                return;
            }
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 || status == 403) {
                emit failed({ChatError::Auth, tr("API key rejected (HTTP %1)").arg(status), false});
            } else if (status == 429) {
                emit failed({ChatError::RateLimited, tr("rate limited"), true});
            } else if (status < 200 || status >= 300) {
                emit failed({ChatError::Unknown,
                             tr("HTTP %1: %2").arg(status).arg(QString::fromUtf8(reply->readAll())),
                             status >= 500});
            } else {
                emit finished({m_text, m_usage});
            }
            reply->deleteLater();
        });
        connect(&m_idleTimer, &QTimer::timeout, this, [this, reply] {
            emit failed({ChatError::Timeout, tr("stream idle for 60 s"), true});
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
    QPointer<OpenAiChatProvider> m_provider;
    QNetworkReply *m_reply = nullptr;
    bool m_cancelled = false;
    SseParser m_parser{[this](const QString &, const QString &data) {
        const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
        if (!doc.isObject()) {
            return;
        }
        const QJsonObject object = doc.object();
        const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
        for (const QJsonValue &choice : choices) {
            const QJsonObject delta = choice.toObject().value(QStringLiteral("delta")).toObject();
            const QString piece = delta.value(QStringLiteral("content")).toString();
            if (!piece.isEmpty()) {
                m_text += piece;
                emit textDelta(piece);
            }
        }
        const QJsonObject usage = object.value(QStringLiteral("usage")).toObject();
        m_usage.inputTokens += usage.value(QStringLiteral("prompt_tokens")).toInt();
        m_usage.outputTokens += usage.value(QStringLiteral("completion_tokens")).toInt();
    }};
    QTimer m_idleTimer{this};
    QString m_text;
    ChatUsage m_usage;
};

OpenAiChatProvider::OpenAiChatProvider(QString baseUrl, QString apiKey, QString model,
                                       QObject *parent)
    : ChatProvider(parent)
    , m_baseUrl(std::move(baseUrl))
    , m_apiKey(std::move(apiKey))
    , m_model(std::move(model))
{
}

QNetworkRequest OpenAiChatProvider::buildRequest() const
{
    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/chat/completions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_apiKey.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
    }
    return request;
}

ChatRun *OpenAiChatProvider::start(const ChatRequest &request)
{
    return new OpenAiChatRun(this, request);
}

void OpenAiChatProvider::fetchModels(
    const std::function<void(const QStringList &models, const QString &error)> &cb)
{
    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/models")));
    if (!m_apiKey.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
    }
    QNetworkReply *reply = m_nam.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, cb] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status < 200 || status >= 300) {
            cb({}, QStringLiteral("HTTP %1").arg(status));
        } else {
            QStringList models;
            const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
            for (const QJsonValue &value : object.value(QStringLiteral("data")).toArray()) {
                models.append(value.toObject().value(QStringLiteral("id")).toString());
            }
            models.sort();
            cb(models, QString());
        }
        reply->deleteLater();
    });
}

} // namespace QindaOffice::Ai

#include "http_chat_provider.moc"
