// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Starter charts of accounts shipped as JSON resources
// (:/qindaoffice/books/templates/<id>.json).
//
// AGENT-CONTRACT (template format, docs/books.md "Chart templates"):
//   {"id", "title", "description",
//    "accounts": [{"number"?, "name", "type": <AccountTypeInfo::key>,
//                  "parent"?: <number of an EARLIER account>,
//                  "role"?: <SystemRole>, "group"?: <functional group>,
//                  "description"?}]}
// Every template must name every SystemRole (tst_books_company checks).

#include <QList>
#include <QString>

namespace QindaOffice::Books {

class CompanyFile;

struct ChartTemplateInfo {
    QString id;
    QString title;
    QString description;
};

// general, services, nonprofit, creative — in that order.
[[nodiscard]] QList<ChartTemplateInfo> chartTemplates();
[[nodiscard]] QByteArray chartTemplateJson(const QString &id);
// Creates the template's accounts. Runs inside the caller's transaction.
bool applyChartTemplate(CompanyFile &file, const QString &id, QString *error);

} // namespace QindaOffice::Books
