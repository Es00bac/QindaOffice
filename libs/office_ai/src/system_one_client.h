// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A "System One" decision model client (D-057): POST {base}/v1/systemone
// with {"model", "state", "questions"} and read back calibrated answers.
// Two interchangeable servers speak it:
//   - TypeSafe Jev (hosted): base https://api.typesafe.ai, model
//     "jev-1.13.0", Bearer key (JEV_API_KEY)
//   - Laya (Apache-2.0, self-hosted `laya-serve`, ~400M params): base
//     http://<host>:8000, model "english" / "multilingual", optional key
//     (LAYA_API_KEY)
// Neither generates text: each question is typed — "noul" (yes/no →
// P(true)), "choice" (one label of `criteria`), "score" (a level of an
// ordinal `criteria` list) — and one forward pass answers all of them in
// a few hundred milliseconds. Live notes use it as a cheap gate in front
// of the big model (live_notes_engine.h).
//
// AGENT-CONTRACT: the response is {"answers": {<key>: {"type", "choice" |
// "score" | "value" | "probabilities", "confidence"}}, "usage": {...}}.
// Jev returns "choice" for choice and "score" for score; a noul answer's
// P(true) is read from "noul" (what Laya sends), "value" (Jev), else
// probabilities["true"]. A client that missed "noul" read every Laya
// answer as 0 — everything filler, no notes ever.

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>

#include <functional>

namespace QindaOffice::Ai {

struct SystemOneAnswer {
    QString choice;      // choice questions
    double score = 0;    // score questions: the expected level
    double probability = 0; // noul questions: P(true)
    double confidence = 0;
};

class SystemOneClient : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const QHash<QString, SystemOneAnswer> &answers,
                                        const QString &error)>;

    SystemOneClient(QString baseUrl, QString apiKey, QString model, QObject *parent = nullptr);

    // One request; the callback runs exactly once (answers or an error).
    // `timeoutMs` bounds it: a gate that is slower than the thing it
    // gates is worse than none.
    void ask(const QJsonObject &state, const QJsonObject &questions, const Callback &callback,
             int timeoutMs = 6000);

    [[nodiscard]] bool offMachine() const;
    [[nodiscard]] QString model() const { return m_model; }

    // Parses a response body (exposed for the tests).
    [[nodiscard]] static QHash<QString, SystemOneAnswer> parseAnswers(const QJsonObject &body);

private:
    QString m_baseUrl;
    QString m_apiKey;
    QString m_model;
    QNetworkAccessManager m_nam;
};

} // namespace QindaOffice::Ai
