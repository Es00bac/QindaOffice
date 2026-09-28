// SPDX-License-Identifier: GPL-3.0-or-later
#include "http_transcription.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QtEndian>

namespace QindaOffice::Speech {

namespace {

QHttpPart textPart(const QString &name, const QString &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"").arg(name));
    part.setBody(value.toUtf8());
    return part;
}

bool isLoopback(const QUrl &url)
{
    const QString host = url.host();
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1")
        || host == QLatin1String("::1");
}

} // namespace

HttpTranscription::HttpTranscription(QString baseUrl, QString apiKey, QString model,
                                     QString language, QObject *parent)
    : SpeechRecognizer(parent)
    , m_baseUrl(std::move(baseUrl))
    , m_apiKey(std::move(apiKey))
    , m_model(std::move(model))
    , m_language(std::move(language))
{
    while (m_baseUrl.endsWith(QLatin1Char('/'))) {
        m_baseUrl.chop(1);
    }
}

QString HttpTranscription::displayName() const
{
    return tr("%1 (%2)").arg(m_model, QUrl(m_baseUrl).host());
}

bool HttpTranscription::offMachine() const
{
    return !isLoopback(QUrl(m_baseUrl));
}

QByteArray HttpTranscription::wavFile(const QByteArray &pcm)
{
    QByteArray wav(44, '\0');
    char *h = wav.data();
    const auto put32 = [](char *at, quint32 v) { qToLittleEndian<quint32>(v, at); };
    const auto put16 = [](char *at, quint16 v) { qToLittleEndian<quint16>(v, at); };
    memcpy(h, "RIFF", 4);
    put32(h + 4, quint32(36 + pcm.size()));
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);             // fmt chunk size
    put16(h + 20, 1);              // PCM
    put16(h + 22, 1);              // mono
    put32(h + 24, kSampleRate);
    put32(h + 28, kSampleRate * 2); // byte rate
    put16(h + 32, 2);              // block align
    put16(h + 34, 16);             // bits per sample
    memcpy(h + 36, "data", 4);
    put32(h + 40, quint32(pcm.size()));
    return wav + pcm;
}

void HttpTranscription::start()
{
    if (m_baseUrl.isEmpty() || m_model.isEmpty()) {
        emit failed(tr("The transcription endpoint needs a base URL and a model."), true);
        return;
    }
    emit ready();
}

void HttpTranscription::feed(const QByteArray &pcm)
{
    if (m_finishing || m_cancelled || pcm.isEmpty()) {
        return;
    }
    countFed(pcm.size());
    m_queue.append(m_segmenter.feed(pcm));
    pump();
}

void HttpTranscription::pump()
{
    if (m_reply != nullptr || m_cancelled) {
        return;
    }
    if (m_queue.isEmpty()) {
        if (m_finishing) {
            emitFinishedOnce();
        }
        return;
    }
    const AudioClip clip = m_queue.takeFirst();
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"clip.wav\""));
    file.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    file.setBody(wavFile(clip.pcm));
    multipart->append(file);
    multipart->append(textPart(QStringLiteral("model"), m_model));
    multipart->append(textPart(QStringLiteral("response_format"), QStringLiteral("json")));
    if (!m_language.isEmpty()) {
        multipart->append(textPart(QStringLiteral("language"), m_language));
    }
    const QString prompt = clipPrompt(m_lastText);
    if (!prompt.isEmpty()) {
        multipart->append(textPart(QStringLiteral("prompt"), prompt));
    }
    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/audio/transcriptions")));
    if (!m_apiKey.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
    }
    request.setTransferTimeout(60000);
    QNetworkReply *reply = m_nam.post(request, multipart);
    multipart->setParent(reply);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, clip] {
        reply->deleteLater();
        m_reply = nullptr;
        if (m_cancelled) {
            emitFinishedOnce();
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (status == 401 || status == 403) {
            emit failed(tr("The transcription service rejected the API key (HTTP %1).").arg(status),
                        true);
            m_queue.clear();
            emitFinishedOnce();
            return;
        }
        if (status < 200 || status >= 300) {
            emit failed(tr("Transcription failed (HTTP %1): %2")
                            .arg(status)
                            .arg(status == 0 ? reply->errorString()
                                             : QString::fromUtf8(body.left(300))),
                        false);
        } else {
            const QString text = QJsonDocument::fromJson(body)
                                     .object()
                                     .value(QStringLiteral("text"))
                                     .toString()
                                     .trimmed();
            if (!text.isEmpty()) {
                m_lastText = text;
                emit committed({text, clip.startMs, clip.endMs});
            }
        }
        pump();
    });
}

void HttpTranscription::finish()
{
    if (m_finishing) {
        return;
    }
    m_finishing = true;
    m_queue.append(m_segmenter.flush());
    pump();
}

void HttpTranscription::cancel()
{
    m_cancelled = true;
    m_queue.clear();
    if (m_reply != nullptr) {
        m_reply->abort();
    } else {
        emitFinishedOnce();
    }
}

} // namespace QindaOffice::Speech
