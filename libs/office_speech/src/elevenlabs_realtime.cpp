// SPDX-License-Identifier: GPL-3.0-or-later
#include "elevenlabs_realtime.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QWebSocket>

namespace QindaOffice::Speech {

namespace {

const QUrl kProductionEndpoint(
    QStringLiteral("wss://api.elevenlabs.io/v1/speech-to-text/realtime"));
constexpr int kMaxReconnects = 5;
constexpr int kFinishWaitMs = 8000;

} // namespace

ElevenLabsRealtime::ElevenLabsRealtime(QString apiKey, QString language, QObject *parent)
    : SpeechRecognizer(parent)
    , m_apiKey(std::move(apiKey))
    , m_language(std::move(language))
    , m_endpoint(kProductionEndpoint)
{
    m_finishTimer.setSingleShot(true);
    connect(&m_finishTimer, &QTimer::timeout, this, [this] {
        if (m_socket != nullptr) {
            m_socket->close();
        }
        emitFinishedOnce();
    });
}

QUrl ElevenLabsRealtime::requestUrl() const
{
    QUrl url = m_endpoint;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("model_id"), QStringLiteral("scribe_v2_realtime"));
    query.addQueryItem(QStringLiteral("audio_format"), QStringLiteral("pcm_16000"));
    query.addQueryItem(QStringLiteral("commit_strategy"), QStringLiteral("vad"));
    query.addQueryItem(QStringLiteral("vad_silence_threshold_secs"), QStringLiteral("1.0"));
    if (!m_language.isEmpty()) {
        query.addQueryItem(QStringLiteral("language_code"), m_language);
    }
    for (const QString &term : keyterms()) {
        query.addQueryItem(QStringLiteral("keyterms"), term);
    }
    url.setQuery(query);
    return url;
}

void ElevenLabsRealtime::start()
{
    if (m_apiKey.isEmpty()) {
        // AGENT-GUARD: cancelled BEFORE the signal — a receiver that calls
        // finish() from it must not start the 8 s finish wait.
        m_cancelled = true;
        emit failed(tr("No ElevenLabs API key. Add one in Live Notes ▸ Settings, or set "
                       "ELEVENLABS_API_KEY."),
                    true);
        emitFinishedOnce();
        return;
    }
    connectSocket();
}

void ElevenLabsRealtime::connectSocket()
{
    if (m_socket != nullptr) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
    }
    m_open = false;
    m_firstChunk = true;
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &ElevenLabsRealtime::onText);
    connect(m_socket, &QWebSocket::disconnected, this, &ElevenLabsRealtime::onClosed);
    // A connection that never opens (DNS, TLS, HTTP 401 on the upgrade)
    // reports errorOccurred and may never say disconnected.
    connect(m_socket, &QWebSocket::errorOccurred, this, [this] {
        if (m_socket != nullptr && m_socket->state() == QAbstractSocket::UnconnectedState) {
            onClosed();
        }
    });
    m_closeHandled = false;
    QNetworkRequest request(requestUrl());
    request.setRawHeader("xi-api-key", m_apiKey.toUtf8());
    m_socket->open(request);
}

void ElevenLabsRealtime::feed(const QByteArray &pcm)
{
    if (m_cancelled || m_finishing || pcm.isEmpty()) {
        return;
    }
    countFed(pcm.size());
    if (!m_open) {
        m_buffer.append(pcm);
        const qsizetype limit = qsizetype(kMaxBufferMs) * kBytesPerMs;
        if (m_buffer.size() > limit) {
            // Bounded: the oldest audio goes, and the user is told.
            m_buffer.remove(0, m_buffer.size() - limit);
            emit failed(tr("ElevenLabs is not answering; audio older than %1 s was dropped.")
                            .arg(kMaxBufferMs / 1000),
                        false);
        }
        return;
    }
    m_uncommittedMs += pcm.size() / kBytesPerMs;
    const bool force = m_uncommittedMs >= kForceCommitMs;
    sendChunk(pcm, force);
    if (force) {
        m_uncommittedMs = 0;
    }
}

void ElevenLabsRealtime::sendChunk(const QByteArray &pcm, bool commit)
{
    QJsonObject message{{QStringLiteral("message_type"), QStringLiteral("input_audio_chunk")},
                        {QStringLiteral("audio_base_64"), QString::fromLatin1(pcm.toBase64())},
                        {QStringLiteral("sample_rate"), kSampleRate}};
    if (commit) {
        message.insert(QStringLiteral("commit"), true);
    }
    if (m_firstChunk && !m_history.isEmpty()) {
        // Only allowed on the first chunk of a session (API reference).
        message.insert(QStringLiteral("previous_text"), m_history.right(50));
    }
    m_firstChunk = false;
    m_socket->sendTextMessage(
        QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
}

void ElevenLabsRealtime::flushBuffer()
{
    const qsizetype step = 250 * kBytesPerMs; // 250 ms per message
    for (qsizetype offset = 0; offset < m_buffer.size(); offset += step) {
        sendChunk(m_buffer.mid(offset, step), false);
    }
    m_uncommittedMs += m_buffer.size() / kBytesPerMs;
    m_buffer.clear();
}

void ElevenLabsRealtime::onText(const QString &raw)
{
    const QJsonObject message = QJsonDocument::fromJson(raw.toUtf8()).object();
    const QString type = message.value(QStringLiteral("message_type")).toString();
    if (type == QLatin1String("session_started")) {
        m_open = true;
        m_reconnects = 0;
        flushBuffer();
        if (m_finishing) {
            sendChunk(QByteArray(), true);
        }
        emit ready();
        return;
    }
    if (type == QLatin1String("partial_transcript")) {
        const QString text = message.value(QStringLiteral("text")).toString();
        if (m_segmentStartMs < 0 && !text.trimmed().isEmpty()) {
            m_segmentStartMs = qMax<qint64>(0, audioMs() - 1500);
        }
        emit partial(text);
        return;
    }
    if (type == QLatin1String("committed_transcript")) {
        const QString text = message.value(QStringLiteral("text")).toString().trimmed();
        m_uncommittedMs = 0;
        if (!text.isEmpty()) {
            const qint64 end = audioMs();
            const qint64 start = m_segmentStartMs >= 0 ? m_segmentStartMs : qMax<qint64>(0, end - 3000);
            m_history = (m_history + QLatin1Char(' ') + text).right(400);
            emit committed({text, start, end});
        }
        m_segmentStartMs = -1;
        emit partial(QString());
        if (m_finishing) {
            // The final commit answered; give the server a moment for any
            // trailing segment, then close.
            m_finishTimer.start(600);
        }
        return;
    }
    if (type.startsWith(QLatin1String("committed_transcript_"))) {
        return; // the _with_timestamps / _entities twins: text already taken
    }
    const QString error = message.value(QStringLiteral("error")).toString();
    if (type == QLatin1String("auth_error") || type == QLatin1String("unaccepted_terms")) {
        emit failed(tr("ElevenLabs rejected the API key: %1").arg(error), true);
        cancel();
    } else if (type == QLatin1String("quota_exceeded")) {
        emit failed(tr("ElevenLabs quota exceeded: %1").arg(error), true);
        cancel();
    } else if (type == QLatin1String("session_time_limit_exceeded")) {
        // Long lectures outlive one session: reconnect and carry on.
        m_open = false;
        connectSocket();
    } else if (type == QLatin1String("insufficient_audio_activity")
               || type == QLatin1String("commit_throttled")) {
        // Silence or a commit too soon after the VAD's own: harmless.
    } else if (!type.isEmpty()) {
        emit failed(tr("ElevenLabs: %1 %2").arg(type, error), false);
    }
}

void ElevenLabsRealtime::onClosed()
{
    // AGENT-GUARD: errorOccurred and disconnected can both fire for one
    // socket; handling both reconnected twice and leaked a socket.
    if (m_closeHandled) {
        return;
    }
    m_closeHandled = true;
    m_open = false;
    if (m_cancelled) {
        emitFinishedOnce();
        return;
    }
    if (m_finishing) {
        emitFinishedOnce();
        return;
    }
    if (m_reconnects >= kMaxReconnects) {
        QString reason = m_socket != nullptr ? m_socket->closeReason() : QString();
        if (reason.isEmpty() && m_socket != nullptr) {
            reason = m_socket->errorString(); // e.g. the upgrade's HTTP 401
        }
        emit failed(tr("Lost the connection to ElevenLabs%1.")
                        .arg(reason.isEmpty() ? QString() : QStringLiteral(": ") + reason),
                    true);
        emitFinishedOnce();
        return;
    }
    ++m_reconnects;
    // Back off 0.5 s, 1 s, 2 s, … so a network blip does not hammer the API.
    QTimer::singleShot(500 * (1 << (m_reconnects - 1)), this, [this] {
        if (!m_cancelled && !m_finishing) {
            connectSocket();
        }
    });
}

void ElevenLabsRealtime::finish()
{
    if (m_finishing || m_cancelled) {
        return;
    }
    m_finishing = true;
    m_finishTimer.start(kFinishWaitMs);
    if (m_open) {
        sendChunk(QByteArray(), true);
    }
    // Not open yet: session_started sends the buffer and the commit.
}

void ElevenLabsRealtime::cancel()
{
    if (m_cancelled) {
        return;
    }
    m_cancelled = true;
    m_finishTimer.stop();
    m_buffer.clear();
    if (m_socket != nullptr && m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->close();
    } else {
        emitFinishedOnce();
    }
}

} // namespace QindaOffice::Speech
