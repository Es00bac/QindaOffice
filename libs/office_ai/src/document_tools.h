// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DocumentTools (D-058): the assistant's hands in a document. The EditApplier
// seam (document_context.h) can only paste the reply at the cursor; with
// DocumentTools the model sees the document with addressable parts and
// answers with EDIT OPERATIONS the app applies — as one undo step.
//
// AGENT-CONTRACT (model ↔ app, both sides: AssistantController here and each
// app's *_tools.cpp): the model replies with prose and, when it changes the
// document, exactly one fenced block
//
//     ```qinda-edits
//     {"edits": [ {"op": "...", ...}, ... ]}
//     ```
//
// whose operations are the ones the app's operationGuide() documents. A
// fenced block rather than native tool calling because every provider the
// suite drives — the Claude/Codex/Kimi CLIs, llama-server, OpenAI- and
// Anthropic-shaped HTTP — can produce it, and the parse is ours.

#include <QJsonArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace QindaOffice::Ai {

struct EditOutcome {
    int applied = 0;          // operations that took effect
    QStringList changes;      // one human line per applied operation
    QStringList errors;       // one line per refused operation
};

class DocumentTools {
public:
    virtual ~DocumentTools() = default;

    // "the note page", "the document", "the workbook", "the presentation".
    [[nodiscard]] virtual QString documentNoun() const = 0;
    // The document as the model sees it: its parts labelled with the ids
    // the operations use, the selection/cursor where there is one. At most
    // about `budget` characters (the app elides and says so).
    [[nodiscard]] virtual QString snapshot(int budget) = 0;
    // The operation manual: every op, its fields, one example each.
    [[nodiscard]] virtual QString operationGuide() const = 0;
    // Optional live revision contract. A reply based on a different revision
    // is refused by AssistantController, including when another client edits.
    [[nodiscard]] virtual QString revisionToken() const { return {}; }
    // Undo ignores harmless focus/viewport changes, but must detect content
    // changes and unfinished input. Defaults to the conservative live token.
    [[nodiscard]] virtual QString undoRevisionToken() const { return revisionToken(); }
    // Applies `edits` against the document as it is NOW, as ONE undo step
    // (nothing when no operation applies). Operations refer to the ids of
    // the latest snapshot(); an op whose target no longer matches is
    // refused, never guessed.
    virtual EditOutcome apply(const QJsonArray &edits) = 0;
    // Undoes the last apply() (the app's own undo).
    virtual void undo() = 0;
};

namespace EditProtocol {

// The instructions that turn the assistant into an editor of `tools`.
[[nodiscard]] QString systemPrompt(DocumentTools &tools);

struct ParsedReply {
    QString prose;                    // the reply without the edits block
    std::optional<QJsonArray> edits;  // present when a block was found
    QString error;                    // the block did not parse
};
[[nodiscard]] ParsedReply parse(const QString &reply);

} // namespace EditProtocol

} // namespace QindaOffice::Ai
