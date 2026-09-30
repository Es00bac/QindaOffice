// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "chat_types.h"
#include <QVariantList>
namespace QindaOffice::Ai::LiveNotesFollowUp {
QVariantList kinds();
QString title(const QString &kind, const QString &sourceTitle);
ChatRequest request(const QString &kind, const QString &sourceTitle, const QString &evidence);
bool known(const QString &kind);
}
