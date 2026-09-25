// SPDX-License-Identifier: GPL-3.0-or-later
#include "chart_templates.h"

#include "accounts.h"
#include "company_file.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// AGENT-NOTE: books_engine is a static library, so its resources need an
// explicit Q_INIT_RESOURCE from inside the library (the linker would
// otherwise drop the unreferenced initializer). Must be called outside any
// namespace.
static void initBooksResources()
{
    Q_INIT_RESOURCE(books_templates);
}

namespace QindaOffice::Books {

namespace {

const QStringList kTemplateIds = {QStringLiteral("general"), QStringLiteral("services"),
                                  QStringLiteral("nonprofit"), QStringLiteral("creative")};

QJsonObject loadTemplate(const QString &id)
{
    static const bool initialised = [] {
        initBooksResources();
        return true;
    }();
    Q_UNUSED(initialised);
    QFile file(QStringLiteral(":/qindaoffice/books/templates/%1.json").arg(id));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

QList<ChartTemplateInfo> chartTemplates()
{
    QList<ChartTemplateInfo> list;
    for (const QString &id : kTemplateIds) {
        const QJsonObject object = loadTemplate(id);
        if (!object.isEmpty()) {
            list << ChartTemplateInfo{id, object.value(QStringLiteral("title")).toString(),
                                      object.value(QStringLiteral("description")).toString()};
        }
    }
    return list;
}

QByteArray chartTemplateJson(const QString &id)
{
    const QJsonObject object = loadTemplate(id);
    return object.isEmpty() ? QByteArray() : QJsonDocument(object).toJson(QJsonDocument::Indented);
}

bool applyChartTemplate(CompanyFile &file, const QString &id, QString *error)
{
    const QJsonObject object = loadTemplate(id);
    if (object.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("unknown chart template \"%1\"").arg(id);
        }
        return false;
    }
    ChartOfAccounts chart(file);
    QHash<QString, qint64> byNumber;
    const QJsonArray accounts = object.value(QStringLiteral("accounts")).toArray();
    for (const QJsonValue &value : accounts) {
        const QJsonObject row = value.toObject();
        const std::optional<AccountType> type =
            accountTypeFromKey(row.value(QStringLiteral("type")).toString());
        if (!type) {
            if (error != nullptr) {
                *error = QStringLiteral("template %1: unknown account type \"%2\"")
                             .arg(id, row.value(QStringLiteral("type")).toString());
            }
            return false;
        }
        Account account;
        account.number = row.value(QStringLiteral("number")).toString();
        account.name = row.value(QStringLiteral("name")).toString();
        account.type = *type;
        account.systemRole = row.value(QStringLiteral("role")).toString();
        account.functionalGroup = row.value(QStringLiteral("group")).toString();
        account.description = row.value(QStringLiteral("description")).toString();
        const QString parent = row.value(QStringLiteral("parent")).toString();
        if (!parent.isEmpty()) {
            account.parentId = byNumber.value(parent);
            if (account.parentId == 0) {
                if (error != nullptr) {
                    *error = QStringLiteral("template %1: parent %2 of %3 is not defined earlier")
                                 .arg(id, parent, account.name);
                }
                return false;
            }
        }
        const Result created = chart.create(account);
        if (!created.ok) {
            if (error != nullptr) {
                *error = QStringLiteral("template %1: %2").arg(id, created.error);
            }
            return false;
        }
        if (!account.number.isEmpty()) {
            byNumber.insert(account.number, created.id);
        }
    }
    return true;
}

} // namespace QindaOffice::Books
