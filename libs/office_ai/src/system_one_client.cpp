// SPDX-License-Identifier: GPL-3.0-or-later
#include "system_one_client.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrl>

namespace QindaOffice::Ai {

SystemOneClient::SystemOneClient(QString baseUrl, QString apiKey, QString model, QObject *parent)
    : QObject(parent)
    , m_baseUrl(std::move(baseUrl))
    , m_apiKey(std::move(apiKey))
    , m_model(std::move(model))
{
    while (m_baseUrl.endsWith(QLatin1Char('/'))) {
        m_baseUrl.chop(1);
    }
    // Accept a base given with or without the /v1 suffix.
    if (m_baseUrl.endsWith(QLatin1String("/v1"))) {
        m_baseUrl.chop(3);
    }
}

bool SystemOneClient::offMachine() const
{
    const QString host = QUrl(m_baseUrl).host();
    return host != QLatin1String("localhost") && host != QLatin1String("127.0.0.1")
        && host != QLatin1String("::1");
}

QHash<QString, SystemOneAnswer> SystemOneClient::parseAnswers(const QJsonObject &body)
{
    QHash<QString, SystemOneAnswer> answers;
    const QJsonObject all = body.value(QStringLiteral("answers")).toObject();
    for (auto it = all.begin(); it != all.end(); ++it) {
        const QJsonObject object = it.value().toObject();
        SystemOneAnswer answer;
        answer.choice = object.value(QStringLiteral("choice")).toString();
        answer.score = object.value(QStringLiteral("score")).toDouble();
        answer.confidence = object.value(QStringLiteral("confidence")).toDouble();
        const QJsonObject probabilities = object.value(QStringLiteral("probabilities")).toObject();
        // A yes/no answer's P(true): Laya sends "noul", Jev "value".
        if (object.contains(QStringLiteral("noul"))) {
            answer.probability = object.value(QStringLiteral("noul")).toDouble();
        } else if (object.contains(QStringLiteral("value"))) {
            const QJsonValue value = object.value(QStringLiteral("value"));
            answer.probability = value.isBool() ? (value.toBool() ? 1.0 : 0.0) : value.toDouble();
        } else if (probabilities.contains(QStringLiteral("true"))) {
            answer.probability = probabilities.value(QStringLiteral("true")).toDouble();
        } else if (answer.choice == QLatin1String("true")) {
            answer.probability = answer.confidence;
        }
        answers.insert(it.key(), answer);
    }
    return answers;
}

void SystemOneClient::ask(const QJsonObject &state, const QJsonObject &questions,
                          const Callback &callback, int timeoutMs)
{
    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/v1/systemone")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_apiKey.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
    }
    request.setTransferTimeout(timeoutMs);
    QJsonObject body{{QStringLiteral("state"), state}, {QStringLiteral("questions"), questions}};
    if (!m_model.isEmpty()) {
        body.insert(QStringLiteral("model"), m_model);
    }
    QNetworkReply *reply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, callback] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (status < 200 || status >= 300) {
            callback({}, status == 0 ? reply->errorString()
                                     : tr("HTTP %1: %2").arg(status).arg(
                                           QString::fromUtf8(payload.left(200))));
            return;
        }
        const QHash<QString, SystemOneAnswer> answers =
            parseAnswers(QJsonDocument::fromJson(payload).object());
        callback(answers, answers.isEmpty() ? tr("no answers in the reply") : QString());
    });
}

} // namespace QindaOffice::Ai
