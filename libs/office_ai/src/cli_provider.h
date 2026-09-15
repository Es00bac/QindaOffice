// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// CLI subprocess providers (plan section 5.3): the user's logged-in
// claude/codex/kimi CLIs are the auth mechanism - the suite stores nothing
// for them. Common runner: QProcess in its own session (cancel kills the
// whole group, SIGKILL after 3 s), per-run scratch cwd holding ink.png when
// an image is attached, stdout line loop, stderr tail for diagnostics.
//
// Contract notes (probe-verified where marked):
//  - claude: prompt on stdin; NEVER --bare (breaks OAuth); stream-json
//    events; terminal result line is authoritative.
//  - codex: prompt on stdin ('-' positional); --json events; -o last.txt in
//    the scratch dir is the authoritative final answer.
//  - kimi: prompt is an ARGUMENT (<=200 KB), --output-format stream-json,
//    non-streaming; vision unsupported.

#include "chat_types.h"

#include <QTemporaryDir>

namespace QindaOffice::Ai {

enum class CliKind { Claude, Codex, Kimi };

class CliChatProvider final : public ChatProvider {
    Q_OBJECT
public:
    CliChatProvider(CliKind kind, QString model, QString extraArgs = {},
                    QObject *parent = nullptr);

    [[nodiscard]] CliKind kind() const { return m_kind; }
    [[nodiscard]] QString model() const { return m_model; }
    [[nodiscard]] bool isAvailable() const; // binary on PATH

    [[nodiscard]] ChatRun *start(const ChatRequest &request) override;

private:
    friend class CliChatRun;
    CliKind m_kind;
    QString m_model;
    QString m_extraArgs;
};

} // namespace QindaOffice::Ai
