// SPDX-License-Identifier: GPL-3.0-or-later
#include "elevenlabs_speakers.h"

#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSet>

#include <algorithm>

namespace QindaOffice::Speech {

namespace {

const QUrl kBatchEndpoint(QStringLiteral("https://api.elevenlabs.io/v1/speech-to-text"));
constexpr int kMinTailMs = 1000; // a shorter tail at the end is not worth a request

QHttpPart textPart(const QString &name, const QString &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"").arg(name));
    part.setBody(value.toUtf8());
    return part;
}

qint64 overlapMs(qint64 a0, qint64 a1, qint64 b0, qint64 b1)
{
    return qMax<qint64>(0, qMin(a1, b1) - qMax(a0, b0));
}

} // namespace

ElevenLabsSpeakers::ElevenLabsSpeakers(QString apiKey, QString language, QObject *parent)
    : SpeechRecognizer(parent)
    , m_apiKey(std::move(apiKey))
    , m_language(std::move(language))
    , m_endpoint(kBatchEndpoint)
{
}

void ElevenLabsSpeakers::start()
{
    if (m_apiKey.isEmpty()) {
        m_cancelled = true; // see ElevenLabsRealtime::start()'s AGENT-GUARD
        emit failed(tr("No ElevenLabs API key. Add one in Live Notes ▸ Settings, or set "
                       "ELEVENLABS_API_KEY."),
                    true);
        emitFinishedOnce();
        return;
    }
    emit ready();
}

void ElevenLabsSpeakers::feed(const QByteArray &pcm)
{
    if (m_cancelled || m_finishing || pcm.isEmpty()) {
        return;
    }
    countFed(pcm.size());
    m_audio.append(pcm);
    maybeSend();
}

void ElevenLabsSpeakers::maybeSend()
{
    if (m_reply != nullptr || m_cancelled) {
        return;
    }
    const qint64 now = audioMs();
    const qint64 fresh = now - m_sentUntilMs;
    if (fresh >= m_stepMs || (m_finishing && fresh >= kMinTailMs)) {
        send(qMax<qint64>(0, m_sentUntilMs - m_overlapMs), now);
    } else if (m_finishing) {
        emitFinishedOnce();
    }
}

void ElevenLabsSpeakers::send(qint64 fromMs, qint64 toMs)
{
    const qsizetype offset = (fromMs - m_audioStartMs) * kBytesPerMs;
    QByteArray window;
    m_sentSamples.clear();
    for (const Sample &sample : m_samples) {
        const qint64 at = window.size() / kBytesPerMs;
        window += sample.pcm;
        m_sentSamples.append({sample.speaker, at, at + sample.pcm.size() / kBytesPerMs});
        window += QByteArray(kGapMs * kBytesPerMs, '\0');
    }
    m_prefixMs = window.size() / kBytesPerMs;
    window += m_audio.mid(qMax<qsizetype>(0, offset), (toMs - fromMs) * kBytesPerMs);
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"window.pcm\""));
    file.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    file.setBody(window);
    multipart->append(file);
    multipart->append(textPart(QStringLiteral("model_id"), QStringLiteral("scribe_v2")));
    multipart->append(textPart(QStringLiteral("file_format"), QStringLiteral("pcm_s16le_16")));
    multipart->append(textPart(QStringLiteral("diarize"), QStringLiteral("true")));
    multipart->append(textPart(QStringLiteral("timestamps_granularity"), QStringLiteral("word")));
    multipart->append(textPart(QStringLiteral("tag_audio_events"), QStringLiteral("false")));
    if (!m_language.isEmpty()) {
        multipart->append(textPart(QStringLiteral("language_code"), m_language));
    }
    for (const QString &term : keyterms()) {
        multipart->append(textPart(QStringLiteral("keyterms"), term.left(49)));
    }
    QNetworkRequest request(m_endpoint);
    request.setRawHeader("xi-api-key", m_apiKey.toUtf8());
    request.setTransferTimeout(120000);
    m_billedMs += window.size() / kBytesPerMs;
    QNetworkReply *reply = m_nam.post(request, multipart);
    multipart->setParent(reply);
    m_reply = reply;
    m_sentUntilMs = toMs;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, fromMs, toMs] { onReply(reply, fromMs, toMs); });
}

QHash<QString, QString> ElevenLabsSpeakers::assignSpeakers(
    const QHash<QString, QHash<QString, qint64>> &votes, const QStringList &locals, int *nextSpeaker)
{
    QHash<QString, QString> mapping;
    QSet<QString> taken;
    for (const QString &local : locals) {
        if (mapping.contains(local)) {
            continue;
        }
        QString best;
        qint64 bestTime = 0;
        const QHash<QString, qint64> candidates = votes.value(local);
        for (auto it = candidates.begin(); it != candidates.end(); ++it) {
            if (it.value() > bestTime && !taken.contains(it.key())) {
                best = it.key();
                bestTime = it.value();
            }
        }
        if (best.isEmpty()) {
            best = QStringLiteral("Speaker %1").arg((*nextSpeaker)++);
        }
        taken.insert(best);
        mapping.insert(local, best);
    }
    return mapping;
}

void ElevenLabsSpeakers::onReply(QNetworkReply *reply, qint64 fromMs, qint64 toMs)
{
    reply->deleteLater();
    m_reply = nullptr;
    if (m_cancelled) {
        emitFinishedOnce();
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (status == 401 || status == 403) {
        emit failed(tr("ElevenLabs rejected the API key (HTTP %1).").arg(status), true);
        m_cancelled = true;
        emitFinishedOnce();
        return;
    }
    if (status < 200 || status >= 300) {
        emit failed(tr("ElevenLabs could not transcribe a stretch (HTTP %1): %2")
                        .arg(status)
                        .arg(status == 0 ? reply->errorString() : QString::fromUtf8(body.left(200))),
                    false);
    } else {
        // Words with absolute times and their window-local speaker ids;
        // words inside the voice samples only vote.
        QList<std::pair<Word, QString>> window;
        QHash<QString, QHash<QString, qint64>> votes;
        QStringList locals;
        QString pendingSpace;
        for (const QJsonValue &value : QJsonDocument::fromJson(body).object().value(QStringLiteral("words")).toArray()) {
            const QJsonObject w = value.toObject();
            const QString type = w.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("spacing")) {
                pendingSpace = w.value(QStringLiteral("text")).toString();
                continue;
            }
            if (type != QLatin1String("word")) {
                continue; // audio events
            }
            const qint64 start = qint64(w.value(QStringLiteral("start")).toDouble() * 1000);
            const qint64 end = qint64(w.value(QStringLiteral("end")).toDouble() * 1000);
            const QString local = w.value(QStringLiteral("speaker_id")).toString();
            const QString text = pendingSpace + w.value(QStringLiteral("text")).toString();
            pendingSpace.clear();
            if (!locals.contains(local)) {
                locals.append(local);
            }
            if (start < m_prefixMs) {
                for (const SentSample &sample : std::as_const(m_sentSamples)) {
                    const qint64 shared = overlapMs(start, end, sample.startMs, sample.endMs);
                    if (shared > 0) {
                        votes[local][sample.speaker] += 2 * shared; // a sample is strong evidence
                    }
                }
                continue;
            }
            Word word;
            word.startMs = fromMs + start - m_prefixMs;
            word.endMs = fromMs + end - m_prefixMs;
            word.text = text;
            window.append({word, local});
        }
        const qint64 overlapEnd = fromMs == 0 ? 0 : fromMs + qMin<qint64>(m_overlapMs, toMs - fromMs);
        for (const auto &[word, local] : std::as_const(window)) {
            if (word.startMs >= overlapEnd) {
                continue;
            }
            for (const Word &old : std::as_const(m_previous)) {
                const qint64 shared = overlapMs(word.startMs, word.endMs, old.startMs, old.endMs);
                if (shared > 0) {
                    votes[local][old.speaker] += shared;
                }
            }
        }
        const QHash<QString, QString> mapping = assignSpeakers(votes, locals, &m_nextSpeaker);
        // Emit the words past what the previous window already emitted,
        // one segment per speaker turn.
        const qint64 emitFrom = overlapEnd;
        QList<Word> labelled;
        SpeechSegment segment;
        const auto flush = [this, &segment] {
            segment.text = segment.text.simplified();
            if (!segment.text.isEmpty()) {
                emit committed(segment);
            }
            segment = SpeechSegment();
        };
        for (auto [word, local] : window) {
            word.speaker = mapping.value(local);
            labelled.append(word);
            if (word.startMs < emitFrom) {
                continue;
            }
            if (!segment.text.isEmpty() && segment.speaker != word.speaker) {
                flush();
            }
            if (segment.text.isEmpty()) {
                segment.startMs = word.startMs;
                segment.speaker = word.speaker;
            }
            segment.text += word.text;
            segment.endMs = word.endMs;
        }
        flush();
        m_previous = labelled;
        updateSamples(labelled);
    }
    // Keep only the audio the next window's overlap needs.
    const qint64 keepFrom = qMax<qint64>(m_audioStartMs, m_sentUntilMs - m_overlapMs);
    m_audio.remove(0, (keepFrom - m_audioStartMs) * kBytesPerMs);
    m_audioStartMs = keepFrom;
    maybeSend();
}

void ElevenLabsSpeakers::updateSamples(const QList<Word> &words)
{
    // Each speaker's longest run of words in this window, as a voice sample
    // (kept if longer than the one we have, up to kSampleMs).
    qsizetype i = 0;
    while (i < words.size()) {
        qsizetype j = i;
        while (j + 1 < words.size() && words.at(j + 1).speaker == words.at(i).speaker) {
            ++j;
        }
        const QString speaker = words.at(i).speaker;
        const qint64 start = qMax(words.at(i).startMs, m_audioStartMs);
        const qint64 end = qMin(words.at(j).endMs, start + kSampleMs);
        i = j + 1;
        if (end - start < 300) {
            continue;
        }
        const QByteArray pcm = m_audio.mid((start - m_audioStartMs) * kBytesPerMs, (end - start) * kBytesPerMs);
        auto it = std::find_if(m_samples.begin(), m_samples.end(),
                               [&speaker](const Sample &s) { return s.speaker == speaker; });
        if (it == m_samples.end()) {
            if (m_samples.size() < kMaxSamples) {
                m_samples.append({speaker, pcm});
            }
        } else if (pcm.size() > it->pcm.size()) {
            it->pcm = pcm;
        }
    }
}

void ElevenLabsSpeakers::finish()
{
    if (m_finishing || m_cancelled) {
        return;
    }
    m_finishing = true;
    maybeSend();
}

void ElevenLabsSpeakers::cancel()
{
    m_cancelled = true;
    if (m_reply != nullptr) {
        m_reply->abort();
    } else {
        emitFinishedOnce();
    }
}

} // namespace QindaOffice::Speech
