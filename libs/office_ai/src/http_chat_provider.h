// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// OpenAI-compatible chat/completions provider (plan section 5.3):
// POST {base}/chat/completions with stream:true; delta content from SSE;
// images as data URIs; optional response_format/json_schema; GET {base}/models
// fills pickers. Seeds: GLM, Moonshot, llama.cpp, Ollama, LM Studio.

#include "chat_types.h"
#include "sse_parser.h"

#include <QNetworkAccessManager>
#include <QPointer>

namespace QindaOffice::Ai {

class OpenAiChatRun;

class OpenAiChatProvider final : public ChatProvider {
    Q_OBJECT
public:
    // baseUrl like "https://api.moonshot.ai/v1"; apiKey may be empty for
    // local servers that ignore auth.
    OpenAiChatProvider(QString baseUrl, QString apiKey, QString model, QObject *parent = nullptr);

    [[nodiscard]] ChatRun *start(const ChatRequest &request) override;
    // Fetches model ids through GET {base}/models (fills picker combos).
    void fetchModels(const std::function<void(const QStringList &models, const QString &error)> &cb);

    [[nodiscard]] QString model() const { return m_model; }
    void setModel(const QString &model) { m_model = model; }

private:
    friend class OpenAiChatRun;
    [[nodiscard]] QNetworkRequest buildRequest() const;

    QString m_baseUrl;
    QString m_apiKey;
    QString m_model;
    QNetworkAccessManager m_nam;
};

} // namespace QindaOffice::Ai
