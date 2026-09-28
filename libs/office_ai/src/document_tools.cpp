// SPDX-License-Identifier: GPL-3.0-or-later
#include "document_tools.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

namespace QindaOffice::Ai::EditProtocol {

namespace {
const QLatin1String kFence("```qinda-edits");
}

QString systemPrompt(DocumentTools &tools)
{
    return QStringLiteral(
               "You are the assistant inside an office application, working on %1 the user has "
               "open. You can both answer questions about it and CHANGE it.\n\n"
               "When the user asks for a change (rewrite, condense, fix, fill in, restructure, "
               "add, remove, format…), make it yourself: reply with one or two sentences saying "
               "what you changed, then exactly one fenced block:\n\n"
               "```qinda-edits\n{\"edits\": [ … ]}\n```\n\n"
               "using only the operations below, addressed by the ids in the snapshot. The "
               "edits are applied at once as one undo step, so do the whole job in one block; "
               "never paste the new content as ordinary text instead. When the user only asks a "
               "question, answer it and do not include a block. Keep the user's language, "
               "facts and meaning; do not invent content.\n\n"
               "OPERATIONS\n%2")
        .arg(tools.documentNoun(), tools.operationGuide());
}

ParsedReply parse(const QString &reply)
{
    ParsedReply parsed;
    const qsizetype open = reply.indexOf(kFence);
    if (open < 0) {
        parsed.prose = reply.trimmed();
        return parsed;
    }
    const qsizetype bodyStart = reply.indexOf(QLatin1Char('\n'), open);
    const qsizetype close = bodyStart < 0 ? -1 : reply.indexOf(QLatin1String("```"), bodyStart);
    const QString body = bodyStart < 0 ? QString()
        : reply.mid(bodyStart + 1, (close < 0 ? reply.size() : close) - bodyStart - 1);
    parsed.prose = (reply.left(open) + (close < 0 ? QString() : reply.mid(close + 3))).trimmed();
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(body.trimmed().toUtf8(), &error);
    if (document.isArray()) {
        parsed.edits = document.array();
    } else if (document.isObject() && document.object().value(QStringLiteral("edits")).isArray()) {
        parsed.edits = document.object().value(QStringLiteral("edits")).toArray();
    } else {
        parsed.error = close < 0 ? QObject::tr("the edits block was cut off")
                                 : QObject::tr("the edits block is not valid JSON (%1)")
                                       .arg(error.errorString());
    }
    return parsed;
}

} // namespace QindaOffice::Ai::EditProtocol
