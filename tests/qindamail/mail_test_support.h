// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared fixture for the QindaMail tests: a MailService over a private temp
// tree (store, attachments, accounts.json, view settings) with an in-memory
// keyring and no automatic sync, plus record builders and a wait helper for
// the service's correlated signals. Headless; no real network (servers in
// these tests listen on 127.0.0.1 only).

#include "qindamail/src/mail_service.h"
#include "secret_backend.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace MailTest {

using QindaOffice::Apps::Mail::MailService;

struct Env {
    QTemporaryDir dir;
    std::shared_ptr<QindaOffice::Mail::InMemorySecretBackend> secrets =
        std::make_shared<QindaOffice::Mail::InMemorySecretBackend>();
    std::unique_ptr<MailService> service;

    Env()
    {
        // Anything that still reaches QStandardPaths stays inside the tree.
        qputenv("XDG_DATA_HOME", dir.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", dir.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", dir.filePath(QStringLiteral("state")).toUtf8());
        qputenv("XDG_CACHE_HOME", dir.filePath(QStringLiteral("cache")).toUtf8());
        service = std::make_unique<MailService>(options());
    }

    [[nodiscard]] MailService::Options options() const
    {
        MailService::Options options;
        options.dataDir = dir.filePath(QStringLiteral("data/qindaoffice/mail"));
        options.accountsPath = dir.filePath(QStringLiteral("config/qindaoffice/mail/accounts.json"));
        options.settingsPath = dir.filePath(QStringLiteral("config/qindaoffice/mail/view.json"));
        options.autoSync = false;
        options.watchNetwork = false;
        options.secretBackend = secrets;
        return options;
    }

    // A second service over the same files (a restart).
    [[nodiscard]] std::unique_ptr<MailService> reopen() const { return std::make_unique<MailService>(options()); }
};

inline QString isoUtc(const QDateTime &time)
{
    return time.toUTC().toString(Qt::ISODate);
}

// A message record in the shape the toolkit providers write (mail_record.h).
inline QVariantMap message(const QString &id, const QString &subject, const QDateTime &date,
                           const QVariantMap &extra = {})
{
    QVariantMap record{
        {QStringLiteral("id"), id},
        {QStringLiteral("messageId"), id},
        {QStringLiteral("internetMessageId"), id + QStringLiteral("@example.org")},
        {QStringLiteral("threadId"), id + QStringLiteral("@example.org")},
        {QStringLiteral("sender"), QStringLiteral("Grace Hopper")},
        {QStringLiteral("senderAddress"), QStringLiteral("grace@example.org")},
        {QStringLiteral("to"), QStringLiteral("Ada <ada@example.org>")},
        {QStringLiteral("cc"), QString()},
        {QStringLiteral("subject"), subject},
        {QStringLiteral("date"), isoUtc(date)},
        {QStringLiteral("snippet"), subject + QStringLiteral(" snippet")},
        {QStringLiteral("bodyPlain"), QStringLiteral("Body of ") + subject},
        {QStringLiteral("bodyTrustMode"), QStringLiteral("plain")},
        {QStringLiteral("unread"), true},
        {QStringLiteral("flagged"), false},
        {QStringLiteral("attachments"), QVariantList()},
    };
    for (auto it = extra.cbegin(); it != extra.cend(); ++it) record.insert(it.key(), it.value());
    return record;
}

// Waits for a (requestId, state, message) signal; returns {state, message}.
inline QPair<QString, QString> waitState(QSignalSpy &spy, const QString &requestId, int timeoutMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        for (const QList<QVariant> &args : std::as_const(spy)) {
            if (args.at(0).toString() == requestId) return {args.at(1).toString(), args.at(2).toString()};
        }
        spy.wait(50);
    }
    return {QStringLiteral("timeout"), QString()};
}

// Waits for a (requestId, result map) signal.
inline QVariantMap waitResult(QSignalSpy &spy, const QString &requestId, int timeoutMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        for (const QList<QVariant> &args : std::as_const(spy)) {
            if (args.at(0).toString() == requestId) return args.at(1).toMap();
        }
        spy.wait(50);
    }
    return {{QStringLiteral("state"), QStringLiteral("timeout")}};
}

} // namespace MailTest

// Offscreen, software Quick, in-memory keyring — pinned here as well as in
// the ctest ENVIRONMENT so a direct run behaves the same.
#define MAIL_TEST_MAIN(TestClass)                                                                                  \
    int main(int argc, char *argv[])                                                                               \
    {                                                                                                              \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                                                   \
        qputenv("QT_QUICK_BACKEND", "software");                                                                   \
        qputenv("QQO_MAIL_SECRET_BACKEND", "memory");                                                              \
        QGuiApplication app(argc, argv);                                                                           \
        QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaMail"));                              \
        TestClass test;                                                                                            \
        return QTest::qExec(&test, argc, argv);                                                                    \
    }
