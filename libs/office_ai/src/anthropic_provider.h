// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Anthropic Messages provider (plan section 5.3): POST {base}/v1/messages
// with x-api-key + anthropic-version headers; SSE events message_start /
// content_block_delta (text_delta) / message_delta (stop_reason, usage) /
// message_stop; refusal/max_tokens handled; images as base64 blocks.

#include "chat_types.h"
#include "sse_parser.h"

#include <QNetworkAccessManager>

namespace QindaOffice::Ai {

class AnthropicProvider final : public ChatProvider {
    Q_OBJECT
public:
    AnthropicProvider(QString baseUrl, QString apiKey, QString model, QObject *parent = nullptr);

    [[nodiscard]] ChatRun *start(const ChatRequest &request) override;

private:
    friend class AnthropicRun;
    friend class OpenAiChatRun;
    QString m_baseUrl;
    QString m_apiKey;
    QString m_model;
    QNetworkAccessManager m_nam;
};

} // namespace QindaOffice::Ai
