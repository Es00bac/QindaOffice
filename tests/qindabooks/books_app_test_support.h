// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared fixture for the QindaBooks app tests: a BooksService over a real
// company file created from a chart template in a temp dir, with "today"
// pinned so status, aging and date presets are deterministic.

#include "qindabooks/src/books_host.h"
#include "qindabooks/src/books_service.h"

#include "books_document.h"
#include "company_file.h"

#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

namespace BooksAppTest {

using QindaOffice::Apps::Books::BooksHost;
using QindaOffice::Apps::Books::BooksService;
using QindaOffice::Books::BooksDocument;

inline const QDate kToday(2026, 3, 31);

inline QUrl url(const QTemporaryDir &dir, const QString &name)
{
    return QUrl::fromLocalFile(dir.filePath(name));
}

inline bool ok(const QVariantMap &result)
{
    return result.value(QStringLiteral("ok")).toBool();
}

inline qint64 idOf(const QVariantMap &result)
{
    return result.value(QStringLiteral("id")).toLongLong();
}

// The row of `list` whose `key` equals `value`, or {}.
inline QVariantMap find(const QVariantList &list, const QString &key, const QVariant &value)
{
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        if (m.value(key) == value) {
            return m;
        }
    }
    return {};
}

struct Company {
    QTemporaryDir dir;
    BooksDocument document;
    BooksService service{&document};
    QVariantMap created;

    explicit Company(const QString &templateId = QStringLiteral("general"), QVariantMap settings = {})
    {
        service.setToday(kToday);
        if (!settings.contains(QStringLiteral("name"))) {
            settings.insert(QStringLiteral("name"), QStringLiteral("Test Books LLC"));
        }
        created = service.createCompany(url(dir, QStringLiteral("company.qbooks")), settings, templateId);
    }
    [[nodiscard]] qint64 account(const QString &number) const
    {
        return find(service.accounts(true), QStringLiteral("number"), number).value(QStringLiteral("id")).toLongLong();
    }
    qint64 customer(const QString &name, const QString &email = {}, qint64 taxRateId = 0)
    {
        return idOf(service.saveParty({{QStringLiteral("kind"), QStringLiteral("customer")},
                                       {QStringLiteral("name"), name},
                                       {QStringLiteral("email"), email},
                                       {QStringLiteral("taxRateId"), taxRateId}}));
    }
    qint64 vendor(const QString &name, qint64 defaultAccountId = 0)
    {
        return idOf(service.saveParty({{QStringLiteral("kind"), QStringLiteral("vendor")},
                                       {QStringLiteral("name"), name},
                                       {QStringLiteral("defaultAccountId"), defaultAccountId}}));
    }
    qint64 item(const QString &name, const QString &price, const QString &incomeNumber, bool taxable = false)
    {
        return idOf(service.saveItem({{QStringLiteral("name"), name},
                                      {QStringLiteral("type"), QStringLiteral("service")},
                                      {QStringLiteral("price"), price},
                                      {QStringLiteral("incomeAccountId"), account(incomeNumber)},
                                      {QStringLiteral("taxable"), taxable}}));
    }
    // An open invoice of `amount` (text) on `date` for the customer.
    qint64 invoice(qint64 customerId, const QString &amount, const QString &date = QStringLiteral("2026-03-01"))
    {
        return idOf(service.saveDocument(
            {{QStringLiteral("kind"), QStringLiteral("invoice")},
             {QStringLiteral("partyId"), customerId},
             {QStringLiteral("date"), date},
             {QStringLiteral("lines"), QVariantList{QVariantMap{{QStringLiteral("description"), QStringLiteral("Work")},
                                                               {QStringLiteral("accountId"), account(QStringLiteral("4100"))},
                                                               {QStringLiteral("amount"), amount}}}}}));
    }
    qint64 bill(qint64 vendorId, const QString &amount, const QString &accountNumber = QStringLiteral("6500"),
                const QString &date = QStringLiteral("2026-03-01"))
    {
        return idOf(service.saveDocument(
            {{QStringLiteral("kind"), QStringLiteral("bill")},
             {QStringLiteral("partyId"), vendorId},
             {QStringLiteral("date"), date},
             {QStringLiteral("lines"), QVariantList{QVariantMap{{QStringLiteral("description"), QStringLiteral("Bill")},
                                                               {QStringLiteral("accountId"), account(accountNumber)},
                                                               {QStringLiteral("amount"), amount}}}}}));
    }
};

inline bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

} // namespace BooksAppTest

#define BOOKS_APP_TEST_MAIN(TestClass)                                                     \
    int main(int argc, char *argv[])                                                       \
    {                                                                                      \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                           \
        qputenv("QT_QUICK_BACKEND", "software");                                           \
        QTemporaryDir home;                                                                \
        qputenv("XDG_CONFIG_HOME", home.filePath(QStringLiteral("config")).toUtf8());      \
        qputenv("XDG_STATE_HOME", home.filePath(QStringLiteral("state")).toUtf8());        \
        qputenv("XDG_DATA_HOME", home.filePath(QStringLiteral("data")).toUtf8());          \
        qputenv("XDG_CACHE_HOME", home.filePath(QStringLiteral("cache")).toUtf8());        \
        QGuiApplication app(argc, argv);                                                   \
        QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaBooks"));     \
        QGuiApplication::setApplicationDisplayName(QStringLiteral("QindaBooks"));          \
        TestClass test;                                                                    \
        return QTest::qExec(&test, argc, argv);                                            \
    }
