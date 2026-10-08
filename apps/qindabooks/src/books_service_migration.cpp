// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService (M63, D-071): moving from QuickBooks Desktop — an IIF file's
// lists and transactions into this company, previewed first (the same
// import, rolled back) and then imported as one batch after a backup copy.
//
// AGENT-CONTRACT (QML, IifImportDialog.qml): previewIif / importIif take
// {accountMap: {"QuickBooks name": accountId}, autoApply (default true),
// skipRefused} and answer {ok, committed, error, backupPath, posted,
// postedText, lists[{key, label, added, existing, skipped}],
// transactions[{type, count, documents, journal, duplicates, refused}],
// refused[{firstLine, lastLine, type, reason}], unmapped[{name, lines,
// linesText}], notes[]}.
#include "books_service.h"

#include "backups.h"
#include "company_file.h"
#include "iif_import.h"

#include <QFile>
#include <QStandardPaths>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

IifImportOptions optionsFrom(const QVariantMap &map, bool commit)
{
    IifImportOptions o;
    o.commit = commit;
    o.autoApply = map.value(QStringLiteral("autoApply"), true).toBool();
    o.skipRefused = map.value(QStringLiteral("skipRefused")).toBool();
    const QVariantMap accounts = map.value(QStringLiteral("accountMap")).toMap();
    for (auto it = accounts.cbegin(); it != accounts.cend(); ++it) {
        if (const qint64 id = Format::id(it.value())) {
            o.accountMap.insert(it.key(), id);
        }
    }
    return o;
}

QVariantMap listRow(const QString &key, const QString &label, const IifListCount &c)
{
    return {{QStringLiteral("key"), key},
            {QStringLiteral("label"), label},
            {QStringLiteral("added"), c.added},
            {QStringLiteral("existing"), c.existing},
            {QStringLiteral("skipped"), c.skipped}};
}

} // namespace

QVariantMap BooksService::runIif(const QUrl &fileUrl, const QVariantMap &options, bool commit)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const QString path = localPath(fileUrl);
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        return Format::failure(tr("%1 could not be read: %2").arg(path, in.errorString()));
    }
    const QByteArray bytes = in.readAll();
    QString backup;
    if (commit) {
        // AGENT-NOTE: the import is one batch but postings are never undone
        // (D-045); the copy made first is the way back.
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/qindaoffice/books/backups");
        const Result saved = Backups::createTimestamped(*c, dir, 20, &backup);
        if (!saved.ok) {
            return Format::failure(tr("No backup copy could be made, so nothing was imported: %1").arg(saved.error));
        }
    }
    const IifImportReport report = IifImport::run(*c, bytes, optionsFrom(options, commit));
    QVariantMap out = report.ok ? Format::success() : Format::failure(report.error);
    out.insert(QStringLiteral("committed"), report.committed);
    out.insert(QStringLiteral("backupPath"), backup);
    out.insert(QStringLiteral("posted"), report.posted);
    out.insert(QStringLiteral("postedText"), moneyFormat().text(report.postedDebits));
    out.insert(QStringLiteral("lists"),
               QVariantList{listRow(QStringLiteral("accounts"), tr("Accounts"), report.accounts),
                            listRow(QStringLiteral("customers"), tr("Customers"), report.customers),
                            listRow(QStringLiteral("vendors"), tr("Vendors"), report.vendors),
                            listRow(QStringLiteral("items"), tr("Products and services"), report.items),
                            listRow(QStringLiteral("classes"), tr("Classes"), report.classes)});
    QVariantList types;
    for (const IifTypeCount &t : report.transactions) {
        types << QVariantMap{{QStringLiteral("type"), t.type},         {QStringLiteral("count"), t.count},
                             {QStringLiteral("documents"), t.documents}, {QStringLiteral("journal"), t.journal},
                             {QStringLiteral("duplicates"), t.duplicates}, {QStringLiteral("refused"), t.refused}};
    }
    out.insert(QStringLiteral("transactions"), types);
    QVariantList refused;
    for (const IifRefusal &r : report.refused) {
        refused << QVariantMap{{QStringLiteral("firstLine"), r.firstLine},
                               {QStringLiteral("lastLine"), r.lastLine},
                               {QStringLiteral("type"), r.type},
                               {QStringLiteral("reason"), r.reason}};
    }
    out.insert(QStringLiteral("refused"), refused);
    QVariantList unmapped;
    for (const IifUnmappedAccount &u : report.unmappedAccounts) {
        QStringList lines;
        QVariantList numbers;
        for (int line : u.lines) {
            lines << QString::number(line);
            numbers << line;
        }
        unmapped << QVariantMap{{QStringLiteral("name"), u.name},
                                {QStringLiteral("lines"), numbers},
                                {QStringLiteral("linesText"), lines.join(QStringLiteral(", "))}};
    }
    out.insert(QStringLiteral("unmapped"), unmapped);
    out.insert(QStringLiteral("notes"), report.notes);
    if (report.committed) {
        emit notify(tr("Imported from QuickBooks"),
                    tr("%n transaction(s) posted. A copy of the books from before the import is in your backups.",
                       nullptr, report.posted),
                    QStringLiteral("success"));
    }
    return out;
}

QVariantMap BooksService::previewIif(const QUrl &fileUrl, const QVariantMap &options)
{
    return runIif(fileUrl, options, false);
}

QVariantMap BooksService::importIif(const QUrl &fileUrl, const QVariantMap &options)
{
    return runIif(fileUrl, options, true);
}

} // namespace QindaOffice::Apps::Books
