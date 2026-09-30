// SPDX-License-Identifier: GPL-3.0-or-later
// LiveNotesSession's M56 features (D-061, note_live_session.h): the "this
// matters" flag, catch-up, speaker names, key terms learned from prep
// material and the first minutes, prep material and series memory, the
// session recording, silence auto-pause/auto-stop, screen capture for
// models that read images, the usage meter, study pages and action items
// handed to QindaMail.

#include "note_live_session.h"

#include "note_host.h"

#include "model_catalog.h"
#include "paths.h"
#include "pcm_capture.h"
#include "speech_recognizer.h"
#include "wav_recorder.h"

#include <poppler-qt6.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextDocument>

namespace QindaOffice::Apps::Notes {

namespace {

constexpr qreal kQuietLevel = 0.18;    // meter 0..1 over -60..0 dBFS
constexpr qreal kSoundLevel = 0.26;    // hysteresis: sound is back
constexpr qint64 kPauseAfterMs = 20000;
constexpr int kScreenEveryMs = 20000;
constexpr int kLearnAfterMs = 180000;  // terms from the first 3 minutes

QImage thumbnail(const QImage &image)
{
    return image.scaled(32, 18, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_Grayscale8);
}

int meanDifference(const QImage &a, const QImage &b)
{
    if (a.isNull() || b.isNull() || a.size() != b.size()) {
        return 255;
    }
    qint64 sum = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar *ra = a.constScanLine(y);
        const uchar *rb = b.constScanLine(y);
        for (int x = 0; x < a.width(); ++x) {
            sum += qAbs(int(ra[x]) - int(rb[x]));
        }
    }
    return int(sum / (a.width() * a.height()));
}

} // namespace

// ---- settings ----

#define QQO_LIVE_FLAG(Setter, member)                                                             \
    void LiveNotesSession::Setter(bool on)                                                         \
    {                                                                                             \
        if (member == on) {                                                                       \
            return;                                                                               \
        }                                                                                         \
        member = on;                                                                              \
        saveSettings();                                                                           \
    }
QQO_LIVE_FLAG(setKeepRecording, m_keepRecording)
QQO_LIVE_FLAG(setCaptureScreen, m_captureScreen)
QQO_LIVE_FLAG(setContinueSeries, m_continueSeries)
QQO_LIVE_FLAG(setSplitLongSessions, m_splitLongSessions)
QQO_LIVE_FLAG(setAutoPause, m_autoPause)
#undef QQO_LIVE_FLAG

void LiveNotesSession::setAutoStopMinutes(int minutes)
{
    minutes = qBound(0, minutes, 240);
    if (m_autoStopMinutes == minutes) {
        return;
    }
    m_autoStopMinutes = minutes;
    saveSettings();
}

void LiveNotesSession::setPrepFiles(const QStringList &files)
{
    if (m_prepFiles == files) {
        return;
    }
    m_prepFiles = files;
    emit settingsChanged(); // per session, not persisted
}

void LiveNotesSession::addPrepFile(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!path.isEmpty() && !m_prepFiles.contains(path)) {
        setPrepFiles(m_prepFiles + QStringList{path});
    }
}

void LiveNotesSession::removePrepFile(int index)
{
    if (index >= 0 && index < m_prepFiles.size()) {
        QStringList files = m_prepFiles;
        files.removeAt(index);
        setPrepFiles(files);
    }
}

// ---- prep material and series memory ----

QString LiveNotesSession::readPrepFile(const QString &path, QString *error)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("pdf")) {
        const std::unique_ptr<Poppler::Document> pdf = Poppler::Document::load(path);
        if (pdf == nullptr || pdf->isLocked()) {
            if (error != nullptr) {
                *error = tr("cannot read the PDF %1").arg(QFileInfo(path).fileName());
            }
            return {};
        }
        QStringList pages;
        for (int i = 0; i < pdf->numPages(); ++i) {
            if (const std::unique_ptr<Poppler::Page> page = pdf->page(i)) {
                pages.append(page->text(QRectF()));
            }
        }
        return pages.join(QStringLiteral("\n\n"));
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return {};
    }
    const QString text = QString::fromUtf8(file.read(2 * 1024 * 1024));
    if (suffix == QLatin1String("html") || suffix == QLatin1String("htm")) {
        QTextDocument document;
        document.setHtml(text);
        return document.toPlainText();
    }
    return text;
}

Ai::SessionContext LiveNotesSession::buildContext() const
{
    Ai::SessionContext context;
    for (const QString &path : m_prepFiles) {
        QString error;
        const QString text = readPrepFile(path, &error);
        if (!text.trimmed().isEmpty()) {
            context.reference += QStringLiteral("\n\n--- %1 ---\n%2").arg(QFileInfo(path).fileName(), text.trimmed());
        }
    }
    if (m_continueSeries) {
        context.previousNotes = m_host->earlierNotesInSection(Ai::SessionContext::kPreviousBudget);
    }
    return context;
}

void LiveNotesSession::learnTerms(const QString &material)
{
    if (material.trimmed().isEmpty()) {
        return;
    }
    QPointer<LiveNotesSession> self(this);
    const int generation = m_launchGeneration;
    Ai::LiveNotesExtras::runOnce(m_provider, Ai::LiveNotesExtras::termsRequest(material), this,
                                 [self, generation](const QString &text, const QString &) {
        if (self == nullptr || self->m_engineCore == nullptr || generation != self->m_launchGeneration) {
            return;
        }
        const QStringList terms = Ai::LiveNotesExtras::parseTerms(text);
        if (terms.isEmpty()) {
            return;
        }
        self->m_engineCore->addGlossary(terms);
        self->m_learnedTerms += int(terms.size());
        if (self->m_recognizer != nullptr) {
            // Whisper and the batch engines use them on the next clip;
            // realtime ElevenLabs on its next connection.
            self->m_recognizer->setKeyterms(self->m_engineCore->options().glossary);
        }
        self->m_engineCore->addUsage({});
        emit self->usageChanged();
    });
}

// ---- flag, speakers, catch-up ----

void LiveNotesSession::flag()
{
    if (m_engineCore == nullptr || !active()) {
        return;
    }
    m_engineCore->flagMoment();
    ++m_flagCount;
    setStatus(tr("Flagged — this moment will be highlighted in the notes."), m_error);
    emit transcriptChanged();
}

QVariantList LiveNotesSession::speakers() const
{
    QVariantList list;
    if (m_engineCore == nullptr) {
        return list;
    }
    QStringList seen;
    for (const Ai::TranscriptLine &line : m_engineCore->transcript()) {
        if (!line.speaker.isEmpty() && !seen.contains(line.speaker)) {
            seen.append(line.speaker);
            list.append(QVariantMap{{QStringLiteral("label"), line.speaker},
                                    {QStringLiteral("name"), m_speakerNames.value(line.speaker)}});
        }
    }
    return list;
}

void LiveNotesSession::setSpeakerName(const QString &label, const QString &name)
{
    if (m_speakerNames.value(label) == name.trimmed()) {
        return;
    }
    m_speakerNames.insert(label, name.trimmed());
    if (m_engineCore != nullptr) {
        m_engineCore->setSpeakerNames(m_speakerNames);
    }
    emit transcriptChanged();
}

void LiveNotesSession::catchUp(int minutes)
{
    if (m_engineCore == nullptr || m_catchingUp) {
        return;
    }
    const QList<Ai::TranscriptLine> &lines = m_engineCore->transcript();
    minutes = qBound(1, minutes, 30);
    const qint64 from = lines.isEmpty() ? 0 : lines.last().ms - qint64(minutes) * 60000;
    QList<Ai::TranscriptLine> recent;
    for (const Ai::TranscriptLine &line : lines) {
        if (line.ms >= from) {
            recent.append(line);
        }
    }
    if (recent.isEmpty()) {
        m_catchUp = tr("Nothing has been said yet.");
        emit catchUpChanged();
        return;
    }
    m_catchingUp = true;
    emit catchUpChanged();
    QPointer<LiveNotesSession> self(this);
    const int generation = m_launchGeneration;
    Ai::LiveNotesExtras::runOnce(m_provider, Ai::LiveNotesExtras::catchUpRequest(recent, m_speakerNames),
                                 this, [self, generation](const QString &text, const QString &error) {
        if (self == nullptr || generation != self->m_launchGeneration || self->m_engineCore == nullptr) {
            return;
        }
        self->m_catchingUp = false;
        self->m_catchUp = error.isEmpty() ? Ai::LiveNotesPrompt::sanitizeReply(text)
                                          : tr("Could not catch up: %1").arg(error);
        emit self->catchUpChanged();
    });
}

QVariantMap LiveNotesSession::usage() const
{
    const Ai::LiveNotesEngine::Usage engine =
        m_engineCore != nullptr ? m_engineCore->usage() : Ai::LiveNotesEngine::Usage{};
    const qint64 audio = m_recognizer != nullptr ? m_recognizer->billedAudioMs() : 0;
    return {{QStringLiteral("audioMinutes"), double(audio) / 60000.0},
            {QStringLiteral("requests"), engine.requests},
            {QStringLiteral("inputTokens"), engine.inputTokens},
            {QStringLiteral("outputTokens"), engine.outputTokens},
            {QStringLiteral("gateCalls"), engine.gateCalls},
            {QStringLiteral("screens"), engine.screens},
            {QStringLiteral("terms"), m_learnedTerms}};
}

// ---- recording and silence ----

void LiveNotesSession::startRecording()
{
    if (!m_keepRecording) {
        return;
    }
    m_recordingPath = m_host->recordingPathFor(m_target);
    m_recorder = std::make_unique<Speech::WavRecorder>();
    QString error;
    if (!m_recorder->open(m_recordingPath, &error)) {
        setStatus(m_status, tr("Could not keep the recording: %1").arg(error));
        m_recorder.reset();
        m_recordingPath.clear();
        return;
    }
    m_host->setLiveRecording(m_target, m_recordingPath);
}

void LiveNotesSession::stopRecording()
{
    if (m_recorder != nullptr) {
        m_recorder->close();
        m_recorder.reset();
    }
}

void LiveNotesSession::onPcm(const QByteArray &pcm)
{
    if (m_recorder != nullptr) {
        m_recorder->append(pcm); // the recording keeps the silences too
    }
    if (m_recognizer != nullptr && !m_silencePaused) {
        m_recognizer->feed(pcm);
    }
}

void LiveNotesSession::observeLevel(qreal level)
{
    m_level = level;
    emit levelChanged();
    if (level >= kSoundLevel) {
        m_quietMs = 0;
        if (m_silencePaused) {
            m_silencePaused = false;
            setStatus(tr("Sound is back — listening."), m_error);
        }
    }
}

void LiveNotesSession::silenceTick()
{
    if (m_state != QLatin1String("listening")) {
        return;
    }
    if (m_level < kQuietLevel) {
        m_quietMs += 1000;
    }
    if (m_autoPause && !m_silencePaused && m_quietMs >= kPauseAfterMs) {
        m_silencePaused = true;
        setStatus(tr("Quiet for a while: paused sending audio. It resumes by itself."), m_error);
    }
    if (m_autoStopMinutes > 0 && m_quietMs >= qint64(m_autoStopMinutes) * 60000) {
        setStatus(tr("Stopped after %n minute(s) of silence.", "", m_autoStopMinutes), m_error);
        stop();
        return;
    }
    // Key terms from the first minutes, once.
    if (!m_learnedFromTranscript && m_engineCore != nullptr && !m_engineCore->transcript().isEmpty()
        && m_engineCore->transcript().last().ms >= kLearnAfterMs) {
        m_learnedFromTranscript = true;
        learnTerms(m_engineCore->transcriptText());
    }
    emit usageChanged();
}

// ---- screen capture ----

void LiveNotesSession::captureScreenNow()
{
    if (m_state != QLatin1String("listening") || m_silencePaused) {
        return;
    }
    // AGENT-NOTE: spectacle's background mode asks KWin's ScreenShot2
    // interface (spectacle is an authorised client), so no portal prompt
    // interrupts a lecture. QQO_SCREENSHOT_COMMAND replaces it (tests).
    const QString dir = QDir::tempPath() + QStringLiteral("/qindanote-screens");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/screen-%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    auto *process = new QProcess(this);
    const int generation = m_launchGeneration;
    QString program = qEnvironmentVariable("QQO_SCREENSHOT_COMMAND");
    QStringList arguments;
    if (program.isEmpty()) {
        program = QStringLiteral("spectacle");
        arguments = {QStringLiteral("-b"), QStringLiteral("-n"), QStringLiteral("-f"),
                     QStringLiteral("-o"), path};
    } else {
        arguments = {path};
    }
    connect(process, &QProcess::finished, this, [this, process, path, generation](int code, QProcess::ExitStatus) {
        process->deleteLater();
        if (code == 0 && generation == m_launchGeneration && m_state == QLatin1String("listening")) {
            onScreenCaptured(path);
        }
        QFile::remove(path);
    });
    // A missing helper emits FailedToStart without finished: release it,
    // stop repeated launch attempts, and keep the audio pipeline alive.
    connect(process, &QProcess::errorOccurred, this,
        [this, process, path, generation](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        process->deleteLater();
        QFile::remove(path);
        if (generation == m_launchGeneration && m_state == QLatin1String("listening")) {
            m_screenTimer.stop();
            setStatus({}, tr("Screen capture could not start; audio notes continue."));
        }
    });
    process->start(program, arguments);
    QTimer::singleShot(20000, process, [process] { if (process->state() != QProcess::NotRunning) process->kill(); });
}

void LiveNotesSession::onScreenCaptured(const QString &path)
{
    QImage image(path);
    if (image.isNull() || m_engineCore == nullptr) {
        return;
    }
    const QImage thumb = thumbnail(image);
    if (meanDifference(thumb, m_lastScreen) < 6) {
        return; // the same slide
    }
    m_lastScreen = thumb;
    m_engineCore->addScreen(image.width() > 1280 ? image.scaledToWidth(1280, Qt::SmoothTransformation)
                                                 : image);
}

} // namespace QindaOffice::Apps::Notes
