// SPDX-License-Identifier: GPL-3.0-or-later
// BooksDocument: the Core::Document lifecycle of a company file — never
// dirty once it has a path, Save is a no-op, Save As copies and switches,
// an untitled company is dirty until saved, recovery is not used.
#include "books_document.h"
#include "atomic_file_store.h"
#include "books_fixture.h"

#include <QFileInfo>
#include <QSignalSpy>

using namespace BooksTest;

class TstBooksDocument : public QObject {
    Q_OBJECT
private slots:
    void lifecycle()
    {
        QTemporaryDir dir;
        CompanySettings s;
        s.legalName = QStringLiteral("Fresh Start Center");
        BooksDocument doc;
        QSignalSpy replaced(&doc, &BooksDocument::companyChanged);
        const QString path = dir.filePath(QStringLiteral("center.qbooks"));
        QString error;
        QVERIFY2(doc.createCompany(path, s, QStringLiteral("nonprofit"), &error), qPrintable(error));
        QCOMPARE(replaced.count(), 1);
        QVERIFY(!doc.isDirty());
        QVERIFY(!doc.isUntitled());
        QCOMPARE(doc.canonicalPath(), QFileInfo(path).absoluteFilePath());
        QCOMPARE(doc.mimeTypes(), QStringList{QStringLiteral("application/x-qindabooks")});

        // A posting commits at once: still not dirty, and the byte revision
        // follows our own writes (the host's overwrite check stays quiet).
        QSignalSpy booksChanged(&doc, &BooksDocument::booksChanged);
        const quint64 before = doc.byteRevision();
        const qint64 bank = ChartOfAccounts(*doc.company()).find(QStringLiteral("1000"));
        const qint64 grants = ChartOfAccounts(*doc.company()).find(QStringLiteral("4110"));
        QTest::qWait(20); // let the file mtime move
        QVERIFY(Ledger(*doc.company()).postJournalEntry({QDate(2026, 1, 5), {}, {}, {debit(bank, 500000), credit(grants, 500000)}}).ok);
        QVERIFY(booksChanged.count() >= 1);
        QVERIFY(!doc.isDirty());
        QCOMPARE(doc.byteRevision(), QindaOffice::Core::fileRevision(path));
        QVERIFY(doc.byteRevision() != before);

        // Save = no-op success.
        QVERIFY(doc.save(path, &error));
        QCOMPARE(replaced.count(), 1);

        // Save As: a copy at the new path, and the window works on it.
        const QString copy = dir.filePath(QStringLiteral("center-2026.qbooks"));
        QVERIFY2(doc.save(copy, &error), qPrintable(error));
        QCOMPARE(replaced.count(), 2);
        QCOMPARE(doc.canonicalPath(), QFileInfo(copy).absoluteFilePath());
        QCOMPARE(doc.company()->path(), QFileInfo(copy).absoluteFilePath());
        QCOMPARE(ChartOfAccounts(*doc.company()).balance(bank).minor(), 500000);
        QVERIFY(!QFileInfo::exists(copy + QStringLiteral(".saving")));
        // Postings now go to the copy only.
        QVERIFY(Ledger(*doc.company()).postJournalEntry({QDate(2026, 1, 6), {}, {}, {debit(bank, 1), credit(grants, 1)}}).ok);
        auto original = CompanyFile::open(path);
        QCOMPARE(ChartOfAccounts(*original).balance(bank).minor(), 500000);
        // Save As over an existing file (the host asked first) replaces it.
        original.reset();
        QVERIFY(doc.save(path, &error));
        QCOMPARE(ChartOfAccounts(*doc.company()).balance(bank).minor(), 500001);

        // Recovery is not used.
        QVERIFY(doc.recoverySnapshot().isEmpty());
        QVERIFY(!doc.loadRecovery(QByteArray("x"), &error));
        QVERIFY(!error.isEmpty());

        // load() reopens a company; a non-company file is refused.
        BooksDocument other;
        QVERIFY(other.load(copy, &error));
        QCOMPARE(other.company()->settings().legalName, QStringLiteral("Fresh Start Center"));
        QVERIFY(!other.load(dir.filePath(QStringLiteral("nope.qbooks")), &error));
    }

    void untitledIsDirtyUntilSaved()
    {
        QTemporaryDir dir;
        CompanySettings s;
        s.legalName = QStringLiteral("Sloom Studio");
        BooksDocument doc;
        QString error;
        QVERIFY2(doc.createUntitled(s, QStringLiteral("creative"), &error), qPrintable(error));
        QVERIFY(doc.isUntitled());
        QVERIFY(doc.isDirty());
        QVERIFY(doc.canonicalPath().isEmpty());
        const QString scratch = doc.company()->path();
        QVERIFY(QFileInfo::exists(scratch));
        const QString target = dir.filePath(QStringLiteral("studio.qbooks"));
        QVERIFY2(doc.save(target, &error), qPrintable(error));
        QVERIFY(!doc.isDirty());
        QVERIFY(!doc.isUntitled());
        QVERIFY(!QFileInfo::exists(scratch)); // the scratch copy is gone
        QCOMPARE(doc.company()->settings().legalName, QStringLiteral("Sloom Studio"));
    }
};

QTEST_MAIN(TstBooksDocument)
#include "tst_books_document.moc"
