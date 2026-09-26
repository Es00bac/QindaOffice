// SPDX-License-Identifier: GPL-3.0-or-later
// BaseFile: the .qbase identity, plain SQLite files left untouched until
// consent, integrity reports, backups, compact, settings, and captured
// changes that undo and redo exactly — cascades included.

#include "base_fixture.h"

#include <QFile>
#include <QDir>

using namespace BaseTest;

class BaseFileTest final : public QObject {
    Q_OBJECT
private slots:
    void createsAQBaseFile()
    {
        Db db;
        QVERIFY2(db.file, qPrintable(db.error));
        QVERIFY(db.file->isQBase());
        QVERIFY(db.file->hasSystemTables());
        QCOMPARE(db.scalar(str("PRAGMA application_id")).toLongLong(), qint64(BaseFile::kApplicationId));
        QCOMPARE(db.scalar(str("PRAGMA journal_mode")).toString(), str("delete"));
        QCOMPARE(db.file->setting(str("schemaVersion")), str("1"));
        QVERIFY(db.file->openProblems().isEmpty());
        // The magic the MIME type matches: header + "QBAS" at offset 68.
        QFile raw(db.file->path());
        QVERIFY(raw.open(QIODevice::ReadOnly));
        const QByteArray head = raw.read(72);
        QVERIFY(head.startsWith(QByteArray("SQLite format 3\0", 16)));
        QCOMPARE(head.mid(68, 4), QByteArray("QBAS"));
        QString error;
        QVERIFY(!BaseFile::create(db.file->path(), &error));
        QVERIFY(error.contains(str("already exists")));
    }

    void opensAPlainSqliteFileWithoutWritingToIt()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(str("club.sqlite"));
        {
            Connection c;
            QVERIFY(c.open(path, true, false, nullptr));
            QVERIFY(c.exec(str("PRAGMA user_version=7; CREATE TABLE members(id INTEGER PRIMARY KEY, name VARCHAR(40) NOT "
                               "NULL, joined TIMESTAMP, dues DECIMAL(8,2)); INSERT INTO members(name) VALUES('Ada');"))
                        .ok);
        }
        const QByteArray before = [&] {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QString error;
        std::unique_ptr<BaseFile> file = BaseFile::open(path, &error);
        QVERIFY2(file, qPrintable(error));
        QVERIFY(!file->isQBase());
        QVERIFY(!file->hasSystemTables());
        QCOMPARE(file->tableNames(), QStringList{str("members")});
        const std::optional<TableDef> members = readTable(*file, str("members"));
        QVERIFY(members);
        QCOMPARE(members->fields.at(0).type, FieldType::AutoNumber);
        QCOMPARE(members->fields.at(1).type, FieldType::Text);
        QVERIFY(members->fields.at(1).required);
        QCOMPARE(members->fields.at(2).type, FieldType::DateTime);
        QCOMPARE(members->fields.at(3).type, FieldType::Number);
        QCOMPARE(file->setting(str("currency.symbol"), str("$")), str("$"));
        file.reset();
        QFile after(path);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), before); // opening wrote nothing
        // Consent: the system tables are added, user_version kept.
        file = BaseFile::open(path, &error);
        QVERIFY(file->ensureSystemTables().ok);
        QVERIFY(file->hasSystemTables());
        QCOMPARE(file->db().scalar(str("PRAGMA user_version")).toInt(), 7);
        QVERIFY(!file->isQBase());
    }

    void refusesWhatIsNotADatabase()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(str("notes.qbase"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hello, this is text");
        f.close();
        QString error;
        QVERIFY(!BaseFile::open(path, &error));
        QVERIFY(error.contains(str("not a SQLite database")));
    }

    void integrityReportsOrphans()
    {
        Db db;
        QVERIFY(db.donorsAndDonations());
        db.file->db().exec(str("PRAGMA foreign_keys=OFF"));
        QVERIFY(db.exec(str("INSERT INTO donations(donor_id, amount) VALUES(99, 500)")).ok);
        db.file->db().exec(str("PRAGMA foreign_keys=ON"));
        const QStringList problems = db.file->integrityProblems(true);
        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().contains(str("donations → donors")));
    }

    void backupsAndCompact()
    {
        Db db;
        QVERIFY(db.donorsAndDonations());
        QVERIFY(db.exec(str("INSERT INTO donors(name) VALUES('Ada')")).ok);
        const QString copy = db.path("copy.qbase");
        QVERIFY(db.file->backupTo(copy).ok);
        QVERIFY(!db.file->backupTo(copy).ok); // never over a file
        QString error;
        std::unique_ptr<BaseFile> reopened = BaseFile::open(copy, &error);
        QVERIFY(reopened);
        QCOMPARE(reopened->db().scalar(str("SELECT name FROM donors")).toString(), str("Ada"));
        const QString automatic = db.file->automaticBackup(str("Delete table donors"), &error);
        QVERIFY2(!automatic.isEmpty(), qPrintable(error));
        QVERIFY(automatic.startsWith(BaseFile::backupDirectory()));
        QVERIFY(automatic.endsWith(str("-delete-table-donors.qbase")));
        QStringList problems;
        QVERIFY(db.file->compact(&problems).ok);
        QVERIFY(problems.isEmpty());
    }

    void settingsPersist()
    {
        Db db;
        QVERIFY(db.file->setSetting(str("currency.symbol"), str("€")).ok);
        QVERIFY(db.file->setSetting(str("currency.decimals"), str("2")).ok);
        QCOMPARE(db.file->currency().symbol, str("€"));
        QVERIFY(db.exec(str("INSERT INTO qb_meta(key, value) VALUES('future.key', 'kept')")).ok);
        const QString path = db.file->path();
        db.file.reset();
        QString error;
        std::unique_ptr<BaseFile> file = BaseFile::open(path, &error);
        QCOMPARE(file->currency().symbol, str("€"));
        QCOMPARE(file->setting(str("future.key")), str("kept"));
    }

    void capturedChangesUndoCascades()
    {
        Db db;
        QVERIFY(db.donorsAndDonations(RefAction::Cascade));
        Change insert;
        QVERIFY(db.file
                    ->capture(str("Add"),
                              [&] {
                                  Result r = db.exec(str("INSERT INTO donors(name) VALUES('Ada')"));
                                  if (r.ok) {
                                      r = db.exec(str("INSERT INTO donations(donor_id, amount) VALUES(1, 2500), (1, 1000)"));
                                  }
                                  return r;
                              },
                              &insert)
                    .ok);
        QVERIFY(insert.undoable());
        QSignalSpy data(db.file.get(), &BaseFile::dataChanged);
        Change remove;
        QVERIFY(db.file->capture(str("Delete Ada"), [&] { return db.exec(str("DELETE FROM donors WHERE id=1")); }, &remove)
                    .ok);
        QCOMPARE(db.scalar(str("SELECT count(*) FROM donations")).toInt(), 0); // cascaded
        QCOMPARE(data.count(), 1);
        QVERIFY(db.file->applyChange(remove, true).ok); // undo: donor and both donations back
        QCOMPARE(db.scalar(str("SELECT count(*) FROM donations")).toInt(), 2);
        QCOMPARE(db.scalar(str("SELECT sum(amount) FROM donations")).toLongLong(), qint64(3500));
        QVERIFY(db.file->applyChange(remove, false).ok); // redo
        QCOMPARE(db.scalar(str("SELECT count(*) FROM donors")).toInt(), 0);
        QCOMPARE(db.scalar(str("SELECT count(*) FROM donations")).toInt(), 0);
        QVERIFY(db.file->applyChange(remove, true).ok);
        QVERIFY(db.file->applyChange(insert, true).ok);
        QCOMPARE(db.scalar(str("SELECT count(*) FROM donors")).toInt(), 0);
        // A failed body leaves nothing behind and no change.
        Change failed;
        const Result r = db.file->capture(str("Bad"), [&] {
            db.exec(str("INSERT INTO donors(name) VALUES('Grace')"));
            return db.exec(str("INSERT INTO donors(name) VALUES(NULL)"));
        }, &failed);
        QVERIFY(!r.ok);
        QCOMPARE(db.scalar(str("SELECT count(*) FROM donors")).toInt(), 0);
        QVERIFY(failed.isEmpty());
    }

    void conflictingUndoIsRefused()
    {
        Db db;
        QVERIFY(db.donorsAndDonations());
        QVERIFY(db.exec(str("INSERT INTO donors(name) VALUES('Ada')")).ok);
        Change rename;
        QVERIFY(db.file->capture(str("Rename"), [&] { return db.exec(str("UPDATE donors SET name='Ada L.'")); }, &rename)
                    .ok);
        QVERIFY(db.exec(str("UPDATE donors SET name='Someone else'")).ok); // changed behind the step
        const Result undone = db.file->applyChange(rename, true);
        QVERIFY(!undone.ok);
        QVERIFY(undone.error.contains(str("changed since")));
        QCOMPARE(db.scalar(str("SELECT name FROM donors")).toString(), str("Someone else"));
    }

    void imagesRestoreTheWholeDatabase()
    {
        Db db;
        QVERIFY(db.donorsAndDonations());
        const QByteArray image = db.file->image();
        QVERIFY(!image.isEmpty());
        QVERIFY(db.exec(str("DROP TABLE donations")).ok);
        QVERIFY(db.file->restoreImage(image).ok);
        QCOMPARE(db.file->tableNames(), (QStringList{str("donations"), str("donors")}));
        QVERIFY(db.file->integrityProblems(true).isEmpty());
    }

    void statementsSplit()
    {
        QString error;
        QCOMPARE(splitStatements(str("SELECT 1; SELECT ';'; -- c\nSELECT 2"), &error),
                 (QStringList{str("SELECT 1"), str("SELECT ';'"), str("-- c\nSELECT 2")}));
        QVERIFY(error.isEmpty());
        QVERIFY(splitStatements(str("SELECT 'open"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QCOMPARE(firstKeyword(str("  /* x */ -- y\n with t as (select 1) select * from t")), str("WITH"));
    }
};

BASE_TEST_MAIN(BaseFileTest)

#include "tst_base_file.moc"
