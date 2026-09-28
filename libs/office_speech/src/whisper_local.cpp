// SPDX-License-Identifier: GPL-3.0-or-later
#include "whisper_local.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace QindaOffice::Speech {

WhisperLocal::WhisperLocal(QString model, QString language, QObject *parent)
    : SpeechRecognizer(parent)
    , m_model(std::move(model))
    , m_language(std::move(language))
{
}

WhisperLocal::~WhisperLocal()
{
    if (m_process != nullptr) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

QString WhisperLocal::helperPath()
{
    const QString overridden = qEnvironmentVariable("QQO_WHISPER_HELPER");
    if (!overridden.isEmpty()) {
        return overridden;
    }
    const QString installed = QStringLiteral(QQO_WHISPER_HELPER_PATH);
    if (QFileInfo(installed).isExecutable()) {
        return installed;
    }
    return QStringLiteral(QQO_WHISPER_HELPER_SOURCE);
}

void WhisperLocal::start()
{
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_stdout.append(m_process->readAllStandardOutput());
        qsizetype newline = m_stdout.indexOf('\n');
        while (newline >= 0) {
            const QByteArray line = m_stdout.left(newline);
            m_stdout.remove(0, newline + 1);
            onLine(line);
            if (m_process == nullptr) {
                return;
            }
            newline = m_stdout.indexOf('\n');
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (!m_finishing) {
            const QString detail = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
            emit failed(tr("The local Whisper helper stopped (exit %1)%2")
                            .arg(code)
                            .arg(detail.isEmpty() ? QString() : QStringLiteral(": ") + detail.right(300)),
                        true);
        }
        emitFinishedOnce();
    });
    QStringList arguments{QStringLiteral("--model"), m_model};
    if (!m_language.isEmpty()) {
        arguments << QStringLiteral("--language") << m_language;
    }
    m_process->start(helperPath(), arguments);
    if (!m_process->waitForStarted(5000)) {
        m_finishing = true; // see ElevenLabsRealtime::start()'s AGENT-GUARD
        emit failed(tr("Cannot start the local Whisper helper %1: %2")
                        .arg(helperPath(), m_process->errorString()),
                    true);
        emitFinishedOnce();
    }
}

void WhisperLocal::feed(const QByteArray &pcm)
{
    if (m_finishing || pcm.isEmpty()) {
        return;
    }
    countFed(pcm.size());
    for (const AudioClip &clip : m_segmenter.feed(pcm)) {
        send(clip);
    }
}

void WhisperLocal::send(const AudioClip &clip)
{
    if (!m_ready) {
        m_waiting.append(clip);
        return;
    }
    const int id = m_nextId++;
    m_inFlight.insert(id, clip);
    if (m_inFlight.size() > kBacklogWarning && !m_warnedBacklog) {
        m_warnedBacklog = true;
        emit failed(tr("Local Whisper is falling behind the audio; the notes will lag. A smaller "
                       "model or a remote engine keeps up better."),
                    false);
    }
    const QJsonObject header{{QStringLiteral("id"), id},
                             {QStringLiteral("bytes"), clip.pcm.size()},
                             {QStringLiteral("prompt"), clipPrompt(m_lastText)}};
    m_process->write(QJsonDocument(header).toJson(QJsonDocument::Compact) + '\n');
    m_process->write(clip.pcm);
}

void WhisperLocal::onLine(const QByteArray &line)
{
    const QJsonObject message = QJsonDocument::fromJson(line).object();
    if (message.contains(QStringLiteral("fatal"))) {
        emit failed(message.value(QStringLiteral("fatal")).toString(), true);
        return;
    }
    if (message.value(QStringLiteral("ready")).toBool()) {
        m_ready = true;
        emit ready();
        const QList<AudioClip> waiting = m_waiting;
        m_waiting.clear();
        for (const AudioClip &clip : waiting) {
            send(clip);
        }
        maybeFinished();
        return;
    }
    const int id = message.value(QStringLiteral("id")).toInt();
    const AudioClip clip = m_inFlight.take(id);
    if (m_inFlight.size() <= 1) {
        m_warnedBacklog = false;
    }
    if (message.contains(QStringLiteral("error"))) {
        emit failed(tr("Whisper could not transcribe a clip: %1")
                        .arg(message.value(QStringLiteral("error")).toString()),
                    false);
    } else {
        const QString text = message.value(QStringLiteral("text")).toString().trimmed();
        if (!text.isEmpty()) {
            m_lastText = text;
            emit committed({text, clip.startMs, clip.endMs});
        }
    }
    maybeFinished();
}

void WhisperLocal::finish()
{
    if (m_finishing) {
        return;
    }
    for (const AudioClip &clip : m_segmenter.flush()) {
        send(clip);
    }
    m_finishing = true;
    maybeFinished();
}

void WhisperLocal::maybeFinished()
{
    if (!m_finishing || !m_waiting.isEmpty() || !m_inFlight.isEmpty() || m_process == nullptr) {
        return;
    }
    if (m_process->state() == QProcess::NotRunning) {
        emitFinishedOnce(); // never started, or already gone
        return;
    }
    if (!m_ready) {
        return; // still loading; the queued clips go out on ready
    }
    m_process->closeWriteChannel(); // the helper exits; finished() follows
}

void WhisperLocal::cancel()
{
    m_finishing = true;
    m_waiting.clear();
    m_inFlight.clear();
    if (m_process != nullptr && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    } else {
        emitFinishedOnce();
    }
}

} // namespace QindaOffice::Speech
