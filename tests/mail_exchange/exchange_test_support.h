// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Helpers shared by the EWS and Graph adapter tests: waiting for one
// correlated result, finding records, and the secret-leak audit (results,
// transcripts, captured log output and the saved store files).

#include "hosted_calendar_provider.h"

#include <QFile>
#include <QJsonDocument>
#include <QMutex>
#include <QSignalSpy>
#include <QtTest>
#include <functional>

namespace ExchangeTest {

inline QVariantMap record(const QVariantList &rows, const QString &id)
{
    for (const QVariant &value : rows)
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    return {};
}

inline QVariantMap byTitle(const QVariantList &rows, const QString &title)
{
    for (const QVariant &value : rows)
        if (value.toMap().value(QStringLiteral("title")).toString() == title) return value.toMap();
    return {};
}

// Every result a test saw, for the final leak audit.
inline QList<QVariantMap> &seenResults()
{
    static QList<QVariantMap> results;
    return results;
}

inline QVariantMap waitFor(QSignalSpy &spy)
{
    if (spy.isEmpty() && !spy.wait(20000)) return {};
    const QVariantMap result = spy.takeFirst().at(1).toMap();
    seenResults().append(result);
    return result;
}

inline QVariantMap syncNow(QindaOffice::Mail::HostedCalendarProvider &provider, const QString &requestId)
{
    QSignalSpy spy(&provider, &QindaOffice::Mail::HostedCalendarProvider::syncFinished);
    provider.sync(requestId);
    return waitFor(spy);
}

inline QVariantMap changeNow(QindaOffice::Mail::HostedCalendarProvider &provider, const std::function<void()> &start)
{
    QSignalSpy spy(&provider, &QindaOffice::Mail::HostedCalendarProvider::changeFinished);
    start();
    return waitFor(spy);
}

inline QVariantMap calendarsNow(QindaOffice::Mail::HostedCalendarProvider &provider, const QString &requestId)
{
    QSignalSpy spy(&provider, &QindaOffice::Mail::HostedCalendarProvider::calendarsFinished);
    provider.listCalendars(requestId);
    return waitFor(spy);
}

inline QString errorOf(const QVariantMap &result) { return result.value(QStringLiteral("error")).toString(); }

inline QString transcriptOf(const QVariantMap &result)
{
    return result.value(QStringLiteral("transcript")).toStringList().join(QLatin1Char('\n'));
}

// Captured qDebug/qWarning/... output of the whole test run, all threads.
struct LogCapture {
    static QStringList &lines()
    {
        static QStringList captured;
        return captured;
    }
    static QMutex &mutex()
    {
        static QMutex lock;
        return lock;
    }
    static void install()
    {
        // Network internals too: a leak through Qt's own logging counts.
        QLoggingCategory::setFilterRules(QStringLiteral("qt.network.*=true\nqindaoffice.mail.exchange.debug=true"));
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
            QMutexLocker locker(&mutex());
            lines().append(message);
        });
    }
};

// True when none of `needles` occurs in `haystack`.
inline bool clean(const QByteArray &haystack, const QList<QByteArray> &needles, QByteArray *found)
{
    for (const QByteArray &needle : needles)
        if (!needle.isEmpty() && haystack.contains(needle)) { *found = needle; return false; }
    return true;
}

inline QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Audits every seen result, the log and the store files for the secrets.
inline void auditSecrets(const QList<QByteArray> &needles, const QString &storePath)
{
    QByteArray found;
    for (const QVariantMap &result : seenResults()) {
        const QByteArray text = QJsonDocument::fromVariant(result).toJson();
        QVERIFY2(clean(text, needles, &found), "secret in a result: " + found);
    }
    {
        QMutexLocker locker(&LogCapture::mutex());
        QVERIFY(LogCapture::lines().join(QLatin1Char('\n')).contains(QStringLiteral("[credentials redacted]")));   // the audit saw traffic
        QVERIFY2(clean(LogCapture::lines().join(QLatin1Char('\n')).toUtf8(), needles, &found), "secret in the log: " + found);
    }
    QVERIFY(!fileBytes(storePath).isEmpty());
    QVERIFY2(clean(fileBytes(storePath), needles, &found), "secret in the store file: " + found);
    QVERIFY2(clean(fileBytes(storePath + QStringLiteral(".recovery")), needles, &found), "secret in the recovery file: " + found);
}

} // namespace ExchangeTest
