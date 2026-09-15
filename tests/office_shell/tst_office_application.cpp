// SPDX-License-Identifier: GPL-3.0-or-later
#include "document_window.h"
#include "office_application.h"

#include "plain_text_document.h"
#include "recent_files.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Shell;

// Verifies window ownership and the M1 application behaviours: window
// bounds, pristine-untitled reuse, canonical-path uniqueness, recents
// bookkeeping. XDG config/state are redirected to a temp dir so the tests
// never touch the user's directories.
class OfficeApplicationTest : public QObject {
    Q_OBJECT
private slots:
    void newWindowOpensOrdinaryWindows();
    void windowBoundIsThirtyTwo();
    void destroyedWindowsArePruned();
    void openPathsCreatesWindowsWithTitles();
    void duplicatePathActivatesExistingWindow();
    void pristineUntitledWindowAbsorbsFirstOpen();
    void openedPathEntersRecentFiles();
};

void OfficeApplicationTest::newWindowOpensOrdinaryWindows()
{
    OfficeApplication office;
    DocumentWindow *first = office.newWindow();
    DocumentWindow *second = office.newWindow();
    QVERIFY(first != nullptr);
    QVERIFY(second != nullptr);
    QVERIFY(first != second);
    QCOMPARE(office.windowCount(), 2);
    QVERIFY(first->document() != nullptr); // M1: every window owns a stub document
}

void OfficeApplicationTest::windowBoundIsThirtyTwo()
{
    OfficeApplication office;
    QList<DocumentWindow *> created;
    QSignalSpy diagnostics(&office, &OfficeApplication::diagnosticReported);
    for (int i = 0; i < OfficeApplication::kMaxWindows; ++i) {
        created.append(office.newWindow());
    }
    QCOMPARE(office.windowCount(), OfficeApplication::kMaxWindows);
    QVERIFY(office.newWindow() == nullptr);
    QCOMPARE(diagnostics.count(), 1);
    for (QWidget *window : created) {
        window->deleteLater();
    }
    QCoreApplication::sendPostedEvents();
}

void OfficeApplicationTest::destroyedWindowsArePruned()
{
    OfficeApplication office;
    DocumentWindow *window = office.newWindow();
    QCOMPARE(office.windowCount(), 1);
    delete window;
    QCOMPARE(office.windowCount(), 0);
    QCOMPARE(office.windows().size(), 0);
}

void OfficeApplicationTest::openPathsCreatesWindowsWithTitles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = dir.path() + QStringLiteral("/a.txt");
    const QString secondPath = dir.path() + QStringLiteral("/b.txt");
    QVERIFY(QFile::exists(firstPath) == false);
    {
        QFile a(firstPath);
        QVERIFY(a.open(QIODevice::WriteOnly | QIODevice::Text));
        a.write("alpha");
        QFile b(secondPath);
        QVERIFY(b.open(QIODevice::WriteOnly | QIODevice::Text));
        b.write("beta");
    }
    OfficeApplication office;
    QVERIFY(office.openPaths({firstPath, secondPath}));
    QCOMPARE(office.windowCount(), 2);
    const QList<DocumentWindow *> windows = office.windows();
    QVERIFY(windows.at(0)->windowTitle().contains(QStringLiteral("a.txt")));
    QVERIFY(windows.at(1)->windowTitle().contains(QStringLiteral("b.txt")));
    QCOMPARE(qobject_cast<QindaOffice::Core::PlainTextDocument *>(windows.at(0)->document())->text(),
              QStringLiteral("alpha"));
}

void OfficeApplicationTest::duplicatePathActivatesExistingWindow()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/dup.txt");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("content");
    }
    OfficeApplication office;
    QVERIFY(office.openPaths({path}));
    QCOMPARE(office.windowCount(), 1);
    QVERIFY(office.openPaths({path})); // duplicate: activates, no new window
    QCOMPARE(office.windowCount(), 1);
}

void OfficeApplicationTest::pristineUntitledWindowAbsorbsFirstOpen()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/absorb.txt");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("payload");
    }
    OfficeApplication office;
    DocumentWindow *pristine = office.newWindow();
    QVERIFY(pristine->isPristineUntitled());
    QVERIFY(office.openPaths({path}));
    QCOMPARE(office.windowCount(), 1); // reused, not duplicated
    QCOMPARE(office.windows().first(), pristine);
    QCOMPARE(pristine->document()->canonicalPath(),
             QFileInfo(path).canonicalFilePath());
}

void OfficeApplicationTest::openedPathEntersRecentFiles()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/recent.txt");
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("recent");
    }
    OfficeApplication office;
    QVERIFY(office.openPaths({path}));
    QVERIFY(office.recentFiles() != nullptr);
    QCOMPARE(office.recentFiles()->paths().first(),
             QFileInfo(path).canonicalFilePath());
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Hermetic stores: redirect the suite's config/state roots before any
    // QStandardPaths use.
    static QTemporaryDir xdgTemp;
    qputenv("XDG_CONFIG_HOME", (xdgTemp.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_STATE_HOME", (xdgTemp.path() + QStringLiteral("/state")).toUtf8());
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaWrite"));
    OfficeApplicationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_office_application.moc"
