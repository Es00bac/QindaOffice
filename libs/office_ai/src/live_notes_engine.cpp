// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_notes_engine.h"

#include "system_one_client.h"

#include <QJsonArray>
#include <QJsonObject>

namespace QindaOffice::Ai {

namespace {

constexpr int kMaxApplyRetries = 30; // × 1 s tick
constexpr int kGateOffMs = 60000;

int wordCount(const QString &text)
{
    return int(text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size());
}

// The three questions the gate answers for every stretch (D-057). Short
// on purpose: Laya's English checkpoint reads 512 tokens.
QJsonObject gateQuestions()
{
    return QJsonObject{
        {QStringLiteral("substance"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("noul")},
                     {QStringLiteral("instructions"),
                      QStringLiteral("Does the passage contain substantive content worth "
                                     "writing into notes (facts, ideas, explanations, "
                                     "decisions, instructions) rather than filler, greetings, "
                                     "small talk, ads, music or transcription noise?")}}},
        {QStringLiteral("topic"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("choice")},
                     {QStringLiteral("instructions"),
                      QStringLiteral("How does the passage relate to the recent context?")},
                     {QStringLiteral("criteria"),
                      QJsonObject{{QStringLiteral("continues"),
                                   QStringLiteral("continues the same topic")},
                                  {QStringLiteral("new_topic"),
                                   QStringLiteral("starts a clearly different topic or section")},
                                  {QStringLiteral("wrap_up"),
                                   QStringLiteral("summarizes or concludes")}}}}},
        {QStringLiteral("importance"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("score")},
                     {QStringLiteral("instructions"),
                      QStringLiteral("How important is the passage for someone studying these "
                                     "notes later?")},
                     {QStringLiteral("criteria"),
                      QJsonArray{QStringLiteral("aside or minor detail"),
                                 QStringLiteral("useful supporting point"),
                                 QStringLiteral("important point"),
                                 QStringLiteral("key point: a definition, conclusion, formula, "
                                                "deadline or something the speaker stresses")}}}},
    };
}

} // namespace

LiveNotesEngine::LiveNotesEngine(QObject *parent)
    : QObject(parent)
{
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, &LiveNotesEngine::tick);
}

LiveNotesEngine::~LiveNotesEngine()
{
    if (m_run != nullptr) {
        m_run->disconnect(this);
        m_run->cancel();
        m_run->deleteLater();
    }
}

void LiveNotesEngine::setNotesAccess(Reader reader, Writer writer)
{
    m_read = std::move(reader);
    m_write = std::move(writer);
}

qint64 LiveNotesEngine::elapsedMs() const
{
    return m_clock.isValid() ? m_clock.elapsed() : 0;
}

void LiveNotesEngine::start()
{
    m_started = true;
    m_clock.start();
    m_lastUpdateMs = 0;
    m_tick.start();
}

void LiveNotesEngine::addSegment(const QString &text, qint64 ms)
{
    const QString clean = text.simplified();
    if (clean.isEmpty()) {
        return;
    }
    m_lines.append({ms, clean});
    maybeGate();
}

int LiveNotesEngine::pendingWords() const
{
    int words = 0;
    for (qsizetype i = m_covered; i < m_lines.size(); ++i) {
        if (!m_lines.at(i).filler) {
            words += wordCount(m_lines.at(i).text);
        }
    }
    return words;
}

QString LiveNotesEngine::transcriptText() const
{
    return LiveNotesPrompt::formatLines(m_lines);
}

void LiveNotesEngine::maybeGate()
{
    const bool gateOn = m_gate != nullptr
        && (m_gateOffUntilMs < 0 || elapsedMs() >= m_gateOffUntilMs);
    if (!gateOn) {
        m_gated = m_lines.size(); // everything counts as substantive
        return;
    }
    if (m_gateBusy) {
        return;
    }
    int words = 0;
    for (qsizetype i = m_gated; i < m_lines.size(); ++i) {
        words += wordCount(m_lines.at(i).text);
    }
    if (words < m_options.gateBatchWords && !m_finalizing) {
        return;
    }
    if (m_gated >= m_lines.size()) {
        return;
    }
    const qsizetype from = m_gated;
    const qsizetype to = m_lines.size();
    QStringList passage;
    for (qsizetype i = from; i < to; ++i) {
        passage.append(m_lines.at(i).text);
    }
    QStringList recent;
    for (qsizetype i = from - 1; i >= 0 && recent.join(QLatin1Char(' ')).size() < 300; --i) {
        recent.prepend(m_lines.at(i).text);
    }
    const QJsonObject state{
        {QStringLiteral("recent_context"), recent.join(QLatin1Char(' ')).right(300)},
        {QStringLiteral("passage"), passage.join(QLatin1Char(' ')).left(1200)}};
    m_gateBusy = true;
    QPointer<LiveNotesEngine> self(this);
    m_gate->ask(state, gateQuestions(),
                [self, from, to](const QHash<QString, SystemOneAnswer> &answers,
                                 const QString &error) {
                    if (self != nullptr) {
                        self->onGateAnswers(from, to, answers, error);
                    }
                });
}

void LiveNotesEngine::onGateAnswers(qsizetype from, qsizetype to,
                                    const QHash<QString, SystemOneAnswer> &answers,
                                    const QString &error)
{
    m_gateBusy = false;
    to = qMin(to, m_lines.size());
    if (!error.isEmpty()) {
        m_gateOffUntilMs = elapsedMs() + kGateOffMs;
        emit status(tr("The gate model failed (%1); counting every word for a minute.").arg(error));
        m_gated = m_lines.size();
        return;
    }
    const bool filler = answers.value(QStringLiteral("substance")).probability < 0.3;
    const SystemOneAnswer topic = answers.value(QStringLiteral("topic"));
    const bool newTopic = topic.choice == QLatin1String("new_topic") && topic.confidence >= 0.5;
    const bool key = answers.value(QStringLiteral("importance")).score >= 2.5;
    for (qsizetype i = from; i < to; ++i) {
        m_lines[i].filler = filler;
        m_lines[i].key = key && !filler;
    }
    if (newTopic && from < to) {
        m_lines[from].newTopic = true;
        m_topicChangePending = true;
    }
    if (filler) {
        emit status(tr("Skipped a stretch that sounded like filler."));
    }
    m_gated = qMax(m_gated, to);
    maybeGate(); // lines that arrived meanwhile
}

bool LiveNotesEngine::fullFits() const
{
    qsizetype chars = 0;
    for (const TranscriptLine &line : m_lines) {
        chars += line.text.size() + 10;
    }
    const QString notes = m_read ? m_read() : QString();
    return chars <= m_options.fullTranscriptBudget && !LiveNotesPrompt::useTailMode(notes);
}

void LiveNotesEngine::tick()
{
    tryApplyPending();
    if (!m_started || busy() || !m_pending.isEmpty()) {
        return;
    }
    if (m_gate != nullptr && m_gateOffUntilMs >= 0 && elapsedMs() >= m_gateOffUntilMs) {
        m_gateOffUntilMs = -1;
    }
    maybeGate();
    const qint64 now = elapsedMs();
    const qint64 since = now - m_lastUpdateMs;
    const int words = pendingWords();
    const bool anyNew = m_covered < m_lines.size();
    if (m_finalizing) {
        if ((anyNew || (m_updates > 0 && fullFits())) && m_finalAttempts++ < 3) {
            runUpdate(fullFits());
        } else {
            m_tick.stop();
            emit finalized();
        }
        return;
    }
    const bool due = (words >= m_options.minWords && since >= m_options.minIntervalMs)
        || (words > 0 && since >= m_options.maxIntervalMs)
        || (m_topicChangePending && words >= m_options.topicChangeMinWords && since >= 8000);
    if (!due) {
        return;
    }
    const bool full = m_options.fullRevisionEvery > 0 && m_updates > 0
        && (m_updates + 1) % m_options.fullRevisionEvery == 0 && fullFits();
    runUpdate(full);
}

void LiveNotesEngine::runUpdate(bool full)
{
    if (m_provider == nullptr) {
        emit failed(tr("No model is available for live notes."));
        if (m_finalizing) {
            m_tick.stop();
            emit finalized();
        }
        return;
    }
    m_topicChangePending = false;
    m_sentNotes = m_read ? m_read() : QString();
    LiveNotesPrompt::Request request;
    request.notes = m_sentNotes;
    request.topic = m_options.topic;
    request.glossary = m_options.glossary;
    request.elapsedMs = m_lines.isEmpty() ? elapsedMs() : m_lines.last().ms;
    request.fullRevision = full;
    if (full) {
        request.fresh = m_lines;
    } else {
        request.context = m_lines.mid(0, m_covered);
        request.fresh = m_lines.mid(m_covered);
    }
    m_requestEnd = m_lines.size();
    m_runFull = full;
    m_runTail = !full && LiveNotesPrompt::useTailMode(m_sentNotes);

    ChatRequest chat;
    chat.systemPrompt = LiveNotesPrompt::systemPrompt();
    chat.messages.append({QStringLiteral("user"), LiveNotesPrompt::userMessage(request), {}});
    chat.maxTokens = m_options.maxOutputTokens;
    chat.temperature = 0.3;
    m_lastUpdateMs = elapsedMs();
    m_run = m_provider->start(chat);
    emit busyChanged(true);
    emit status(full ? tr("Revising the whole set of notes…") : tr("Updating the notes…"));
    connect(m_run, &ChatRun::finished, this, [this](const ChatResult &result) {
        m_run->deleteLater();
        m_run = nullptr;
        emit busyChanged(false);
        onReply(result.text);
    });
    connect(m_run, &ChatRun::failed, this, [this](const ChatError &error) {
        m_run->deleteLater();
        m_run = nullptr;
        emit busyChanged(false);
        if (error.kind != ChatError::Cancelled) {
            emit failed(tr("The notes model failed: %1").arg(error.message));
        }
        if (m_finalizing) {
            m_tick.stop();
            emit finalized();
        }
    });
}

void LiveNotesEngine::onReply(const QString &text)
{
    const QString current = m_read ? m_read() : QString();
    if (current != m_sentNotes) {
        emit status(tr("You edited the notes while they were being updated; your edit is kept "
                       "and goes into the next update."));
        return; // m_covered unchanged: the same lines go again
    }
    const QString reply = LiveNotesPrompt::sanitizeReply(text);
    QString notes = reply;
    if (m_runTail) {
        notes = LiveNotesPrompt::mergeTail(m_sentNotes, reply);
        if (notes.isEmpty()) {
            emit failed(tr("The model's reply had no sections; the notes were kept."));
            return;
        }
    }
    QString why;
    if (!LiveNotesPrompt::plausibleRevision(m_sentNotes, notes, &why)) {
        emit failed(tr("Kept the previous notes: %1.").arg(why));
        m_covered = m_requestEnd; // do not loop on the same material
        return;
    }
    m_pending = notes;
    m_pendingRetries = 0;
    tryApplyPending();
}

void LiveNotesEngine::tryApplyPending()
{
    if (m_pending.isEmpty()) {
        return;
    }
    if (m_read && m_read() != m_sentNotes) {
        m_pending.clear(); // edited while waiting: the edit wins
        return;
    }
    if (m_write && !m_write(m_pending)) {
        if (++m_pendingRetries > kMaxApplyRetries) {
            m_pending.clear();
            emit status(tr("The notes were open in the editor too long; the update was dropped "
                           "and will be redone."));
        }
        return;
    }
    const int lines = int(m_pending.count(QLatin1Char('\n'))) + 1;
    m_pending.clear();
    m_covered = m_requestEnd;
    ++m_updates;
    emit status(tr("Notes updated at %1 — %n line(s).", "", lines)
                    .arg(LiveNotesPrompt::formatTime(
                        m_lines.isEmpty() ? elapsedMs() : m_lines.last().ms)));
    emit notesApplied(m_updates);
    if (m_finalizing && m_runFull) {
        m_tick.stop();
        emit finalized();
    }
}

void LiveNotesEngine::finalize()
{
    if (m_finalizing) {
        return;
    }
    m_finalizing = true;
    m_started = true;
    if (!m_clock.isValid()) {
        m_clock.start();
    }
    m_tick.start();
    // Gate what is left right away rather than waiting for a full batch.
    maybeGate();
}

void LiveNotesEngine::cancel()
{
    m_tick.stop();
    m_pending.clear();
    if (m_run != nullptr) {
        m_run->cancel();
    }
}

} // namespace QindaOffice::Ai
