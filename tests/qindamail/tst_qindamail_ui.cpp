// SPDX-License-Identifier: GPL-3.0-or-later
// QindaMail through the real window (M44): the three workspaces render over
// a seeded store, selecting a message fills the reading pane, the ribbon
// gate holds in every workspace (commands reachable from the ribbon of at
// least one workspace, icons real, nothing hidden at 520 px), the composer
// and the dialogs open, and the structural fixtures. QQO_GRAB_DIR saves
// PNGs of each workspace for review (docs/verification.md).

#include "qindamail/src/message_list_model.h"
#include "mail_test_support.h"

#include "qindamail/src/mail_host.h"
#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>

using QindaOffice::Apps::Mail::MailHost;
using QindaOffice::Test::findNamed;

namespace {

struct Scene {
    MailTest::Env env;
    QQmlApplicationEngine engine;
    std::unique_ptr<MailHost> host;
    QQuickWindow *window = nullptr;
    QString error;

    explicit Scene(const QSize &size, bool withCalendar)
    {
        seedMail();
        if (withCalendar) seedCalendar();
        host = std::make_unique<MailHost>(env.service.get());
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Mail/App/MailWindow.qml")));
        QObject *object = component.createWithInitialProperties({{QStringLiteral("host"), QVariant::fromValue(host.get())}});
        if (object == nullptr) {
            error = component.errorString();
            return;
        }
        window = qobject_cast<QQuickWindow *>(object);
        host->attachWindow(window);
        window->resize(size);
        window->show();
    }
    ~Scene() { delete window; }

    void seedMail()
    {
        auto *store = env.service->store();
        // A fixed "now": the list's day headings (Today, Yesterday, This
        // week, Earlier) depend on the weekday and the hour, so seeding from
        // the wall clock made the structural fixtures fail on some days.
        const QDateTime now(QDate(2026, 9, 24), QTime(12, 0));
        env.service->messageModel()->setReferenceTime(now);
        struct Seed { const char *id; const char *sender; const char *subject; int daysAgo; bool unread; bool flagged; bool attachment; };
        const Seed seeds[] = {
            {"m1", "Maya Okafor", "Spring menu proofs are ready", 0, true, false, true},
            {"m2", "Fresh Start Center", "Volunteer schedule for October", 0, true, true, false},
            {"m3", "GitHub", "[QindaQt] New issue: panel menu on second screen", 1, false, false, false},
            {"m4", "Jordan Lee", "Invoice INV-1001 received — thank you", 2, false, false, false},
            {"m5", "Northwind Creative", "Brand book v3 (final)", 3, true, false, true},
            {"m6", "Portage news", "Qt 6.11.2 stabilised on amd64", 6, false, false, false},
            {"m7", "Sam Rivera", "Coffee next week?", 12, false, true, false},
        };
        for (const Seed &seed : seeds) {
            QVariantMap extra{{QStringLiteral("sender"), QString::fromUtf8(seed.sender)},
                              {QStringLiteral("unread"), seed.unread},
                              {QStringLiteral("flagged"), seed.flagged}};
            if (seed.attachment) {
                extra.insert(QStringLiteral("attachments"), QVariantList{QVariantMap{
                    {QStringLiteral("id"), QString::fromLatin1(seed.id) + QStringLiteral("/2")},
                    {QStringLiteral("name"), QStringLiteral("proofs.pdf")},
                    {QStringLiteral("mimeType"), QStringLiteral("application/pdf")},
                    {QStringLiteral("size"), 482133},
                    {QStringLiteral("state"), QStringLiteral("remote")}}});
            }
            store->upsert(QStringLiteral("messages"),
                          MailTest::message(QString::fromLatin1(seed.id), QString::fromUtf8(seed.subject),
                                            now.addDays(-seed.daysAgo).addSecs(-3600 * (1 + seed.daysAgo)), extra));
        }
    }
    void seedCalendar()
    {
        const QDate monday = QDate::currentDate().addDays(1 - QDate::currentDate().dayOfWeek());
        const auto at = [&](int day, const char *time) { return monday.addDays(day).toString(Qt::ISODate) + QLatin1Char('T') + QLatin1String(time); };
        const QList<QVariantMap> events = {
            {{QStringLiteral("title"), QStringLiteral("Standup")}, {QStringLiteral("startLocal"), at(0, "09:00")}, {QStringLiteral("endLocal"), at(0, "09:15")}},
            {{QStringLiteral("title"), QStringLiteral("Client call — Harbor & Pine")}, {QStringLiteral("startLocal"), at(1, "11:00")}, {QStringLiteral("endLocal"), at(1, "12:00")}, {QStringLiteral("location"), QStringLiteral("Video")}},
            {{QStringLiteral("title"), QStringLiteral("Design review")}, {QStringLiteral("startLocal"), at(2, "14:00")}, {QStringLiteral("endLocal"), at(2, "15:30")}},
            {{QStringLiteral("title"), QStringLiteral("Volunteer shift")}, {QStringLiteral("startLocal"), at(3, "10:00")}, {QStringLiteral("endLocal"), at(3, "13:00")}},
            {{QStringLiteral("title"), QStringLiteral("Quarterly taxes due")}, {QStringLiteral("startLocal"), monday.addDays(4).toString(Qt::ISODate)}, {QStringLiteral("endLocal"), monday.addDays(5).toString(Qt::ISODate)}, {QStringLiteral("allDay"), true}},
        };
        for (const QVariantMap &event : events) env.service->createEvent(event);
        for (const char *task : {"Send brand book", "Reconcile September", "Book venue for fundraiser"})
            env.service->addTask(QString::fromLatin1(task));
        QTest::qWait(100);
    }
    [[nodiscard]] QQuickItem *item(const QString &name) const { return findNamed(window->contentItem(), name); }
};

// A document-less window: Save and Save As stay disabled (D-042), and the
// edit commands act on whichever text field has focus — the menus and the
// command search reach them, the ribbon does not repeat them.
QStringList menuOnly()
{
    return {QStringLiteral("fileSaveAction"), QStringLiteral("fileSaveAsAction"), QStringLiteral("editUndoAction"),
            QStringLiteral("editRedoAction"), QStringLiteral("editCutAction"), QStringLiteral("editCopyAction"),
            QStringLiteral("editPasteAction"), QStringLiteral("editSelectAllAction"), QStringLiteral("editFindAction"),
            QStringLiteral("editReplaceAction")};
}

QStringList ribbonIds(QQuickWindow *window)
{
    QQuickItem *ribbon = findNamed(window->contentItem(), QStringLiteral("officeRibbon"));
    return ribbon != nullptr ? QindaOffice::Test::callStrings(ribbon, "commandIds") : QStringList();
}

} // namespace

class MailUiTest final : public QObject {
    Q_OBJECT
private slots:
    void mailWorkspaceShowsTheInboxAndReads()
    {
        Scene scene(QSize(1280, 800), false);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTest::qWait(200);
        QVERIFY(scene.item(QStringLiteral("folderList")) != nullptr);
        QQuickItem *rows = scene.item(QStringLiteral("messageRows"));
        QVERIFY(rows != nullptr);
        QTRY_VERIFY(rows->property("count").toInt() >= 7);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindamail_inbox_1280"));
        QQuickItem *row = findNamed(rows, QStringLiteral("messageRow_m1"));
        QVERIFY(row != nullptr);
        QTest::mouseClick(scene.window, Qt::LeftButton, {}, row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint());
        QTRY_COMPARE(scene.env.service->selectedMessageId(), QStringLiteral("m1"));
        QQuickItem *subject = scene.item(QStringLiteral("readingSubject"));
        QVERIFY(subject != nullptr);
        QTRY_COMPARE(subject->property("text").toString(), QStringLiteral("Spring menu proofs are ready"));
        QVERIFY(scene.item(QStringLiteral("readingAttachment_m1/2")) != nullptr);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindamail_mail_1280"));
    }

    void ribbonGateHoldsInEveryWorkspace()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 800), true);
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            // A command is covered when some workspace's ribbon reaches it.
            QStringList reachable;
            for (const char *view : {"mail", "calendar", "tasks"}) {
                scene.host->setView(QString::fromLatin1(view));
                QTest::qWait(120);
                reachable += ribbonIds(scene.window);
            }
            for (const char *view : {"mail", "calendar", "tasks"}) {
                scene.host->setView(QString::fromLatin1(view));
                QTest::qWait(150);
                QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindamail_%1_%2").arg(QString::fromLatin1(view)).arg(width));
                const QStringList problems = QindaOffice::Test::ribbonProblems(scene.window, scene.host->menuTree(), reachable + menuOnly());
                QVERIFY2(problems.isEmpty(), qPrintable(QString::fromLatin1(view) + QStringLiteral(": ") + problems.join(QLatin1Char('\n'))));
            }
        }
    }

    void composerAndDialogsOpen()
    {
        Scene scene(QSize(1280, 800), false);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host->actions()->trigger(QStringLiteral("mailNewMessageAction")));
        QQuickWindow *composer = nullptr;
        QTRY_VERIFY([&] {
            for (QWindow *w : QGuiApplication::allWindows())
                if (w != scene.window && w->isVisible() && w->title().contains(QStringLiteral("QindaMail"))) composer = qobject_cast<QQuickWindow *>(w);
            return composer != nullptr;
        }());
        QTest::qWait(150);
        QVERIFY(findNamed(composer->contentItem(), QStringLiteral("mailComposer")) != nullptr);
        QindaOffice::Test::saveGrab(composer, QStringLiteral("qindamail_composer"));
        composer->close();
        QVERIFY(scene.host->actions()->trigger(QStringLiteral("mailAddAccountAction")));
        QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("accountDialog"));
        QVERIFY(dialog != nullptr);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindamail_account_dialog"));
    }

    void structuralFixtures()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, width == 1280 ? 800 : 700), false);
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QTest::qWait(150);
            QString diff;
            const QString path = QStringLiteral(QQO_FIXTURE_DIR "/qindamail_%1.txt").arg(width);
            QVERIFY2(QindaOffice::Test::matchesFixture(scene.window, path, &diff), qPrintable(diff));
        }
    }
};

MAIL_TEST_MAIN(MailUiTest)

#include "tst_qindamail_ui.moc"
