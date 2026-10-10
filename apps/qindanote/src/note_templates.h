// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// OneNote's page templates (M84, D-093; Insert ▸ Page Templates): Blank,
// Lined, Grid, Meeting notes and To-do list. A template is a new page's
// rule lines and its first blocks — formatted text with To Do tags — so it
// is ordinary content afterwards.
//
// AGENT-CONTRACT: the ids (blank, lined, grid, meeting, todo) are the
// suffixes of the notePageTemplate_<id> commands (note_host_pages.cpp);
// never rename one.

#include <QDate>
#include <QJsonArray>
#include <QString>
#include <QStringList>

namespace QindaOffice::Apps::Notes::Templates {

struct PageTemplate {
    QString title;      // empty: the section's next "Page n"
    QString background; // blank | ruled | grid
    QJsonArray blocks;
};

[[nodiscard]] QStringList ids();
[[nodiscard]] QString label(const QString &id);
[[nodiscard]] QString iconName(const QString &id);
[[nodiscard]] PageTemplate make(const QString &id, const QDate &date);

} // namespace QindaOffice::Apps::Notes::Templates
