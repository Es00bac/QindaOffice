// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The one-shot helpers around live notes (M56, D-061): single model calls
// — key terms to teach the recognizer, "catch me up", study cards, action
// items with dates — and the pure text surgery for splitting long notes
// into pages by topic and exporting flashcards for Anki. Each request
// builder is paired with its reply parser so the tests pin both.

#include "chat_types.h"
#include "live_notes_prompt.h"

#include <QDate>
#include <QList>
#include <QString>

#include <functional>

namespace QindaOffice::Ai::LiveNotesExtras {

// Runs one request to completion; `done(text, error)` exactly once. The
// run is parented to `context` and dies with it.
void runOnce(ChatProvider *provider, const ChatRequest &request, QObject *context,
             const std::function<void(const QString &text, const QString &error)> &done);

// Names, jargon and acronyms from prep material, earlier notes and the
// transcript so far → terms for the recognizer and the notes' spelling.
[[nodiscard]] ChatRequest termsRequest(const QString &material);
[[nodiscard]] QStringList parseTerms(const QString &reply);

// A few bullets on the last minutes of the session.
[[nodiscard]] ChatRequest catchUpRequest(const QList<TranscriptLine> &recent,
                                         const QHash<QString, QString> &names);

// A study page from notes: flashcards as a two-column table, practice
// questions with answers.
[[nodiscard]] ChatRequest studyRequest(const QString &title, const QString &notes);

struct ActionItem {
    QString title;
    QString owner;
    QDate due;      // invalid = no date
    QString notes;
};
// The notes' action items, dates resolved against `today`.
[[nodiscard]] ChatRequest actionItemsRequest(const QString &notes, const QDate &today);
[[nodiscard]] QList<ActionItem> parseActionItems(const QString &reply);
// iCalendar: one VTODO per item (QindaMail imports them as tasks) and a
// VEVENT on the due date of each dated item (its calendar).
[[nodiscard]] QByteArray actionItemsIcs(const QList<ActionItem> &items, const QString &source);

// Long notes, one page per `##` topic (#19). The summary, action items,
// definitions and open questions stay on the index page, which lists the
// topic pages' titles.
struct TopicPage {
    QString title;
    QString markdown;
};
struct SplitNotes {
    QString index;              // markdown of the index page
    QList<TopicPage> topics;
};
[[nodiscard]] SplitNotes splitByTopic(const QString &notes, const QString &title);

// Anki import text (tab-separated front/back, one card per line) from the
// markdown tables whose header is Front | Back (or Question | Answer).
[[nodiscard]] QString ankiTsv(const QString &markdown);

// "*(unclear at 12:40: …?)*" spots in notes (#8): where and what.
struct UnclearSpot {
    QString text;     // the whole annotation, as it stands in the notes
    QString guess;    // what the model thought it heard
    qint64 ms = -1;   // -1 when the annotation carries no time
};
[[nodiscard]] QList<UnclearSpot> unclearSpots(const QString &notes);
// "[12:40]" section times in notes: heading text and time (#3).
struct SectionTime {
    QString heading;
    qint64 ms = 0;
};
[[nodiscard]] QList<SectionTime> sectionTimes(const QString &notes);
[[nodiscard]] qint64 parseTimestamp(const QString &text); // "12:40" / "1:02:03" → ms, -1

} // namespace QindaOffice::Ai::LiveNotesExtras
