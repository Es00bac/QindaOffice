// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The result map every office_mail operation emits.
//
// AGENT-CONTRACT: the same shape as the QindaTK provider result contract
// (QindaTK docs/productivity-providers.md, "Common result contract"):
// `requestId`, `ok`, `state` ∈ accepted | offline | rejected | conflict,
// `error`, `retryable`. Hosts (the QindaMail app) route every result through
// one handler, so office_mail objects must not invent other states.
// Operation-specific keys are added next to these; none may carry a
// password or token (docs/mail.md#secrets).

#include <QString>
#include <QVariantMap>

namespace QindaOffice::Mail {

inline constexpr const char *kStateAccepted = "accepted";
inline constexpr const char *kStateOffline = "offline";
inline constexpr const char *kStateRejected = "rejected";
inline constexpr const char *kStateConflict = "conflict";

[[nodiscard]] inline QVariantMap makeResult(const QString &requestId, const char *state,
                                            const QString &error = {}, bool retryable = false)
{
    const bool ok = QLatin1String(state) == QLatin1String(kStateAccepted);
    return QVariantMap{
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("ok"), ok},
        {QStringLiteral("state"), QString::fromLatin1(state)},
        {QStringLiteral("error"), error},
        {QStringLiteral("retryable"), retryable},
    };
}

} // namespace QindaOffice::Mail
