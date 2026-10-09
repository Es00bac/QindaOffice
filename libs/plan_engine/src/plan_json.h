// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// JSON pieces shared by the .qplan file and the task clipboard
// (plan_file.h has the format contract).

#include "plan_types.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <initializer_list>

namespace QindaOffice::Plan::Json {

// "08:00" ↔ minutes from midnight ("24:00" = 1440); -1 when invalid.
[[nodiscard]] QString clock(int minute);
[[nodiscard]] int parseClock(const QString &text);
// The keys of `o` not in `known`.
[[nodiscard]] QJsonObject extraOf(const QJsonObject &o, std::initializer_list<const char *> known);
void putString(QJsonObject &o, const char *key, const QString &value);   // omitted when empty
void putDate(QJsonObject &o, const char *key, Minutes value);            // omitted when kNoDate
[[nodiscard]] Minutes date(const QJsonObject &o, const char *key);
[[nodiscard]] qint64 integer(const QJsonObject &o, const char *key, qint64 fallback = 0);

[[nodiscard]] QJsonObject taskToJson(const Task &task);
[[nodiscard]] Task taskFromJson(const QJsonObject &o, QStringList *warnings);
// A task's or an assignment's baselines: [{number, start, finish, duration,
// work, cost}] for the ones that are set (M79: numbers 0–10).
[[nodiscard]] QJsonArray baselinesToJson(const std::array<BaselineData, kBaselineCount> &list);
void baselinesFromJson(const QJsonValue &value, std::array<BaselineData, kBaselineCount> *list);

} // namespace QindaOffice::Plan::Json

namespace QindaOffice::Plan {
using Json::taskFromJson;
using Json::taskToJson;
} // namespace QindaOffice::Plan
