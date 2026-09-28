// SPDX-License-Identifier: GPL-3.0-or-later
#include "pcm_capture.h"

#include "speech_recognizer.h"

#include <QProcess>
#include <QtEndian>

#include <cmath>

namespace QindaOffice::Speech {

namespace {

constexpr qsizetype kChunkBytes = 100 * kBytesPerMs; // 100 ms

QString recorderProgram()
{
    const QString overridden = qEnvironmentVariable("QQO_PW_RECORD");
    return overridden.isEmpty() ? QStringLiteral("pw-record") : overridden;
}

QStringList baseArguments()
{
    return {QStringLiteral("--rate"), QString::number(kSampleRate), QStringLiteral("--channels"),
            QStringLiteral("1"), QStringLiteral("--format"), QStringLiteral("s16"),
            QStringLiteral("--raw")};
}

} // namespace

PcmCapture::PcmCapture(QObject *parent)
    : QObject(parent)
{
}

PcmCapture::~PcmCapture()
{
    // No last-chunk signals from a destructor: the receivers may already
    // be gone.
    m_pending.clear();
    m_mixing = false;
    stop();
}

QString PcmCapture::sourceName(Source source)
{
    switch (source) {
    case Source::Microphone:
        return QStringLiteral("microphone");
    case Source::DesktopAudio:
        return QStringLiteral("desktop");
    case Source::Both:
        return QStringLiteral("both");
    }
    return QStringLiteral("microphone");
}

PcmCapture::Source PcmCapture::sourceFromName(const QString &name)
{
    if (name == QLatin1String("desktop")) {
        return Source::DesktopAudio;
    }
    if (name == QLatin1String("both")) {
        return Source::Both;
    }
    return Source::Microphone;
}

QString PcmCapture::defaultSink()
{
    const QString overridden = qEnvironmentVariable("QQO_DEFAULT_SINK");
    if (!overridden.isEmpty()) {
        return overridden;
    }
    QProcess pactl;
    pactl.start(QStringLiteral("pactl"), {QStringLiteral("get-default-sink")});
    if (!pactl.waitForFinished(3000) || pactl.exitCode() != 0) {
        pactl.kill();
        return {};
    }
    return QString::fromUtf8(pactl.readAllStandardOutput()).trimmed();
}

bool PcmCapture::start(Source source, const QString &microphone)
{
    stop();
    m_stopping = false;
    m_mixer = PcmMixer();
    m_pending.clear();
    m_mixing = source == Source::Both;
    int stream = 0;
    if (source == Source::Microphone || source == Source::Both) {
        QStringList arguments = baseArguments();
        if (!microphone.isEmpty()) {
            arguments << QStringLiteral("--target") << microphone;
        }
        arguments << QStringLiteral("-");
        if (spawn(stream++, arguments) == nullptr) {
            stop();
            return false;
        }
    }
    if (source == Source::DesktopAudio || source == Source::Both) {
        const QString sink = defaultSink();
        if (sink.isEmpty()) {
            stop();
            emit failed(tr("Cannot find the desktop's audio output (pactl get-default-sink "
                           "failed). Is PipeWire running?"));
            return false;
        }
        // AGENT-NOTE: stream.capture.sink makes PipeWire link the SINK's
        // monitor ports to this capture stream: what you hear, not what the
        // microphone hears.
        QStringList arguments = baseArguments();
        arguments << QStringLiteral("-P") << QStringLiteral("{ stream.capture.sink = true }")
                  << QStringLiteral("--target") << sink << QStringLiteral("-");
        if (spawn(stream, arguments) == nullptr) {
            stop();
            return false;
        }
    }
    return true;
}

QProcess *PcmCapture::spawn(int stream, const QStringList &arguments)
{
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, stream] {
        deliver(stream, process->readAllStandardOutput());
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus) {
        if (m_stopping) {
            return;
        }
        const QString detail = QString::fromUtf8(process->readAllStandardError()).trimmed();
        emit failed(detail.isEmpty() ? tr("The audio recorder stopped (exit code %1).").arg(code)
                                     : tr("The audio recorder stopped: %1").arg(detail.left(300)));
    });
    process->start(recorderProgram(), arguments);
    if (!process->waitForStarted(5000)) {
        emit failed(tr("Cannot start %1 (%2). Live notes need PipeWire's pw-record.")
                        .arg(recorderProgram(), process->errorString()));
        process->deleteLater();
        return nullptr;
    }
    m_processes[stream] = process;
    return process;
}

void PcmCapture::stop()
{
    m_stopping = true;
    for (QPointer<QProcess> &process : m_processes) {
        if (process == nullptr) {
            continue;
        }
        process->disconnect(this);
        process->terminate();
        if (!process->waitForFinished(1500)) {
            process->kill();
            process->waitForFinished(1000);
        }
        process->deleteLater();
        process = nullptr;
    }
    if (m_mixing) {
        const QByteArray rest = m_mixer.drain();
        if (!rest.isEmpty()) {
            emitChunk(rest);
        }
    }
    if (!m_pending.isEmpty()) {
        const QByteArray rest = m_pending;
        m_pending.clear();
        emitChunk(rest);
    }
}

bool PcmCapture::running() const
{
    return m_processes[0] != nullptr || m_processes[1] != nullptr;
}

void PcmCapture::deliver(int stream, const QByteArray &bytes)
{
    const QByteArray mono = m_mixing ? m_mixer.push(stream, bytes) : bytes;
    if (mono.isEmpty()) {
        return;
    }
    m_pending.append(mono);
    while (m_pending.size() >= kChunkBytes) {
        const QByteArray chunk = m_pending.left(kChunkBytes);
        m_pending.remove(0, kChunkBytes);
        emitChunk(chunk);
    }
}

void PcmCapture::emitChunk(const QByteArray &chunk)
{
    // RMS in dBFS mapped to 0..1 over -60..0 dB: a meter that moves for
    // speech at normal distance, not only for shouting.
    double sum = 0;
    const qsizetype samples = chunk.size() / 2;
    for (qsizetype i = 0; i < samples; ++i) {
        const double s = qFromLittleEndian<qint16>(chunk.constData() + i * 2) / 32768.0;
        sum += s * s;
    }
    const double rms = samples > 0 ? std::sqrt(sum / double(samples)) : 0.0;
    const double db = rms > 1e-6 ? 20.0 * std::log10(rms) : -120.0;
    emit level(qBound(0.0, (db + 60.0) / 60.0, 1.0));
    if (!m_paused) {
        emit pcm(chunk);
    }
}

} // namespace QindaOffice::Speech
