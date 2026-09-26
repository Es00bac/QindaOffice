// SPDX-License-Identifier: GPL-3.0-or-later
#include "document_host.h"
#include "standard_actions.h"

#include "app_services.h"
#include "document.h"
#include "recovery_journal.h"

#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Quick;

namespace {

// The minimum an app engine is: a Document whose save writes its text.
class StubDocument final : public QindaOffice::Core::Document {
public:
    QString text;
    bool load(const QString &path, QString *error = nullptr) override
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = file.errorString();
            return false;
        }
        text = QString::fromUtf8(file.readAll());
        setCanonicalPath(path);
        refreshByteRevision();
        setDirty(false);
        return true;
    }
    bool save(const QString &path, QString *error = nullptr) override
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) *error = file.errorString();
            return false;
        }
        file.write(text.toUtf8());
        file.close();
        setCanonicalPath(path);
        refreshByteRevision();
        setDirty(false);
        return true;
    }
    [[nodiscard]] QStringList mimeTypes() const override { return {QStringLiteral("text/plain")}; }
    [[nodiscard]] QByteArray recoverySnapshot() const override { return text.toUtf8(); }
    bool loadRecovery(const QByteArray &snapshot, QString * = nullptr) override
    {
        text = QString::fromUtf8(snapshot);
        setDirty(true);
        notifyContentChanged(); // as the real engines do (WriteDocument)
        return true;
    }
    void edit(const QString &value)
    {
        text = value;
        setDirty(true);
        notifyContentChanged();
    }
};

class TextHost final : public DocumentHost {
public:
    TextHost()
    {
        setDocument(new StubDocument);
        finalizeHost();
    }
    [[nodiscard]] StubDocument *stub() const { return static_cast<StubDocument *>(document()); }

protected:
    [[nodiscard]] QString filterString() const override
    {
        return QStringLiteral("Text (*.txt);;All files (*)");
    }
};

// Just enough application for the journal flows.
class JournalServices final : public AppServices {
public:
    explicit JournalServices(const QString &dir) : journalStore(dir)
    {
        journalStore.setDebounceInterval(10);
    }
    QindaOffice::Core::RecoveryJournal journalStore;
    QindaOffice::Core::RecoveryJournal *journal() override { return &journalStore; }
    QindaOffice::Core::RecentFiles *recentFiles() override { return nullptr; }
    bool isPathOpenInAnotherWindow(const QString &, const DocumentHost *) const override
    {
        return false;
    }
    QString allocateUntitledJournalKey() override { return journalStore.allocateUntitledKey(); }
    QString journalKeyForPath(const QString &path) const override
    {
        return QindaOffice::Core::RecoveryJournal::keyForPath(path);
    }
    void documentSaved(const QString &) override {}
    bool restorePreviousDocuments() const override { return false; }
    void setRestorePreviousDocuments(bool) override {}
};

} // namespace

class DocumentHostTest final : public QObject {
    Q_OBJECT
private slots:
    void hostWithADocumentCanSaveAndIsPristine()
    {
        TextHost host;
        QVERIFY(host.hasDocument());
        QVERIFY(host.isPristineUntitled());
        QVERIFY(host.action(QString::fromLatin1(ActionId::kFileSave))->isEnabled());
        QVERIFY(host.action(QString::fromLatin1(ActionId::kFileSaveAs))->isEnabled());
        QVERIFY(!host.action(QString::fromLatin1(ActionId::kFilePrint))->isEnabled());
        QCOMPARE(host.openNameFilters(), QStringList({QStringLiteral("Text (*.txt)"),
                                                      QStringLiteral("All files (*)")}));
        QVERIFY(host.windowTitle().startsWith(host.title()));
        host.stub()->edit(QStringLiteral("hello"));
        QVERIFY(!host.isPristineUntitled());
        QVERIFY(host.windowTitle().startsWith(QStringLiteral("• ")));
    }

    void closeAsksBeforeLosingEdits()
    {
        TextHost host;
        QSignalSpy consent(&host, &DocumentHost::closeConsentRequested);
        QSignalSpy closing(&host, &DocumentHost::closing);
        QVERIFY(host.requestClose()); // pristine: closes at once
        QCOMPARE(closing.count(), 1);

        TextHost dirty;
        QSignalSpy consent2(&dirty, &DocumentHost::closeConsentRequested);
        QSignalSpy closing2(&dirty, &DocumentHost::closing);
        dirty.stub()->edit(QStringLiteral("x"));
        QVERIFY(!dirty.requestClose());
        QCOMPARE(consent2.count(), 1);
        dirty.resolveClose(QStringLiteral("cancel"));
        QVERIFY(!dirty.closeAuthorized());
        dirty.resolveClose(QStringLiteral("discard"));
        QVERIFY(dirty.closeAuthorized());
        // With no window attached the authorised close reports through
        // requestClose() the next time the window asks.
        QVERIFY(dirty.requestClose());
        QCOMPARE(closing2.count(), 1);
    }

    void saveAsCompletesTheSuffixAndReportsBack()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        TextHost host;
        host.stub()->edit(QStringLiteral("body"));
        QSignalSpy dialog(&host, &DocumentHost::saveAsDialogRequested);
        QSignalSpy finished(&host, &DocumentHost::saveFinished);
        host.save(); // untitled → Save As
        QCOMPARE(dialog.count(), 1);
        QCOMPARE(dialog.first().at(1).toStringList().first(), QStringLiteral("Text (*.txt)"));
        host.saveAsPathChosen(QUrl::fromLocalFile(dir.path() + QStringLiteral("/notes")),
                              QStringLiteral("Text (*.txt)"));
        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.first().first().toBool());
        QVERIFY(QFile::exists(dir.path() + QStringLiteral("/notes.txt")));
        QVERIFY(!host.isDirty());
        QCOMPARE(host.title(), QStringLiteral("notes.txt"));
        QVERIFY(host.statusMessage().startsWith(QStringLiteral("Saved")));

        // Cancelling the dialog is a failed save, not a hang.
        host.stub()->edit(QStringLiteral("more"));
        host.saveAs();
        host.saveAsCancelled();
        QCOMPARE(finished.count(), 2);
        QVERIFY(!finished.last().first().toBool());
    }

    void saveThenCloseChainsThroughConsent()
    {
        QTemporaryDir dir;
        TextHost host;
        const QString path = dir.path() + QStringLiteral("/a.txt");
        host.stub()->text = QStringLiteral("v1");
        QVERIFY(host.stub()->save(path));
        host.stub()->edit(QStringLiteral("v2"));
        QSignalSpy closing(&host, &DocumentHost::closing);
        QVERIFY(!host.requestClose());
        host.resolveClose(QStringLiteral("save"));
        QVERIFY(host.closeAuthorized());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("v2"));
    }

    void aFileChangedOnDiskAsksBeforeOverwriting()
    {
        QTemporaryDir dir;
        TextHost host;
        const QString path = dir.path() + QStringLiteral("/b.txt");
        host.stub()->text = QStringLiteral("mine");
        QVERIFY(host.stub()->save(path));
        // Another program rewrites the file.
        QTest::qWait(20);
        {
            QFile other(path);
            QVERIFY(other.open(QIODevice::WriteOnly));
            other.write("theirs, and longer");
        }
        host.stub()->edit(QStringLiteral("mine again"));
        QSignalSpy consent(&host, &DocumentHost::overwriteConsentRequested);
        QSignalSpy finished(&host, &DocumentHost::saveFinished);
        host.save();
        QCOMPARE(consent.count(), 1);
        host.resolveOverwrite(false);
        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().first().toBool());
        QVERIFY(host.isDirty());
        host.save();
        host.resolveOverwrite(true);
        QCOMPARE(finished.count(), 2);
        QVERIFY(finished.last().first().toBool());
    }

    void openGoesThroughTheDialogAndBack()
    {
        TextHost host;
        QSignalSpy dialog(&host, &DocumentHost::openDialogRequested);
        QSignalSpy requested(&host, &DocumentHost::openPathsRequested);
        QVERIFY(host.actions()->trigger(QString::fromLatin1(ActionId::kFileOpen)));
        QCOMPARE(dialog.count(), 1);
        host.openPathsChosen({QUrl::fromLocalFile(QStringLiteral("/tmp/x.txt")),
                              QUrl(QStringLiteral("https://example.invalid/y"))});
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.first().first().toStringList(), QStringList{QStringLiteral("/tmp/x.txt")});
    }

    void restoringAnEntryDoesNotCopyItAndDiscardRemovesIt()
    {
        // Regression: a restore used to journal the recovered document
        // under a fresh untitled key, so Discard left a copy that came back
        // on every launch.
        QTemporaryDir dir;
        JournalServices services(dir.path());
        services.journalStore.writeNow(QStringLiteral("untitled-1-1"), QString(), true,
                                       QByteArray("unsaved words"));
        QCOMPARE(services.journalStore.pendingKeys(), QStringList{QStringLiteral("untitled-1-1")});
        {
            TextHost host;
            host.setAppServices(&services);
            QVERIFY(host.restoreFromJournal(QString(), true, QByteArray("unsaved words"),
                                            QStringLiteral("untitled-1-1")));
            QCOMPARE(host.stub()->text, QStringLiteral("unsaved words"));
            QVERIFY(host.isDirty());
            // A later edit re-journals under the SAME key.
            host.stub()->edit(QStringLiteral("unsaved words, edited"));
            QTest::qWait(100); // past the journal's debounce
            QCOMPARE(services.journalStore.pendingKeys(),
                     QStringList{QStringLiteral("untitled-1-1")});
            QVERIFY(!host.requestClose());
            host.resolveClose(QStringLiteral("discard"));
            QVERIFY(host.closeAuthorized());
        }
        QVERIFY(services.journalStore.pendingKeys().isEmpty());
    }

    void loadFailureIsReportedNotSwallowed()
    {
        TextHost host;
        QSignalSpy message(&host, &DocumentHost::messageRequested);
        QVERIFY(!host.loadPath(QStringLiteral("/nonexistent/file.txt")));
        QCOMPARE(message.count(), 1);
        QCOMPARE(message.first().at(2).toString(), QStringLiteral("danger"));
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Keep the preferences store out of the user's home.
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_STATE_HOME", config.path().toUtf8());
    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaTest"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("QindaTest"));
    DocumentHostTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_document_host.moc"
