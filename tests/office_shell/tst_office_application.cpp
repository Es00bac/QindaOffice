// SPDX-License-Identifier: GPL-3.0-or-later
#include "document_window.h"
#include "office_application.h"

#include <QApplication>
#include <QSignalSpy>
#include <QtTest>

using namespace QindaOffice::Shell;

// Verifies window ownership and bounds: New opens ordinary windows, the
// process bound is 32, windows prune themselves on destruction, and CLI
// paths open as windows.
class OfficeApplicationTest : public QObject {
    Q_OBJECT
private slots:
    void newWindowOpensOrdinaryWindows();
    void windowBoundIsThirtyTwo();
    void destroyedWindowsArePruned();
    void openPathsCreatesWindowsWithTitles();
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
    QCOMPARE(office.windows().size(), 2);
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
    OfficeApplication office;
    QVERIFY(office.openPaths({QStringLiteral("/tmp/a.html"),
                              QStringLiteral("/tmp/b.html")}));
    QCOMPARE(office.windowCount(), 2);
    const QList<DocumentWindow *> windows = office.windows();
    QVERIFY(windows.at(0)->windowTitle().contains(QStringLiteral("a.html")));
    QVERIFY(windows.at(1)->windowTitle().contains(QStringLiteral("b.html")));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaWrite"));
    OfficeApplicationTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_office_application.moc"
