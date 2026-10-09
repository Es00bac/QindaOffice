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
#include "qindamail/src/account_runtime.h"
#include "oauth2_session.h"

#include <qindatk/local_productivity_store.h>
#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <QBuffer>
#include <QImage>
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

    void htmlMailReadsFormattedWithPicturesOnRequest()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 800), false);
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QStringList fetched;
            scene.env.service->setRemoteFetcher([&fetched](const QUrl &url, MailTest::MailService::FetchDone done) {
                fetched.append(url.toString());
                QImage banner(320, 60, QImage::Format_ARGB32);
                banner.fill(0xffcc8844);
                QByteArray bytes;
                QBuffer buffer(&bytes);
                buffer.open(QIODevice::WriteOnly);
                banner.save(&buffer, "PNG");
                QTimer::singleShot(0, [done, bytes] { done(bytes, QString()); });
            });
            QImage dot(24, 24, QImage::Format_ARGB32);
            dot.fill(0xff2a7a3a);
            QByteArray dotBytes;
            QBuffer dotBuffer(&dotBytes);
            dotBuffer.open(QIODevice::WriteOnly);
            dot.save(&dotBuffer, "PNG");
            const QByteArray raw = QByteArray(
                "From: Maya Okafor <maya@example.org>\r\nTo: ada@example.org\r\nSubject: Spring menu (formatted)\r\n"
                "Date: Tue, 6 Oct 2026 09:30:00 +0000\r\nMIME-Version: 1.0\r\n"
                "Content-Type: multipart/related; boundary=\"r\"\r\n\r\n--r\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
                "<h2>Spring menu</h2><p>Our <b>soup</b> of the day is <i>pea</i> <img src=\"cid:dot@x\" width=24 height=24>.</p>"
                "<p><img src=\"https://news.example/banner.png\" alt=\"Banner\"></p>"
                "<ul><li>Bread</li><li>Cheese</li></ul><p><a href=\"https://example.org/menu\">See the full menu</a></p>\r\n"
                "--r\r\nContent-Type: image/png\r\nContent-ID: <dot@x>\r\nContent-Transfer-Encoding: base64\r\n\r\n")
                + dotBytes.toBase64() + "\r\n--r--\r\n";
            const QString path = scene.env.dir.filePath(QStringLiteral("formatted.eml"));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(raw);
            file.close();
            const QString id = scene.env.service->importMessageFile(path);
            QVERIFY(!id.isEmpty());
            QObject *view = scene.window->findChild<QObject *>(QStringLiteral("readingBodyView"));
            QVERIFY(view != nullptr);
            QTRY_COMPARE(view->property("messageId").toString(), id);
            QTRY_VERIFY(view->property("formatted").toBool());
            QVERIFY(view->property("remoteBlocked").toBool());
            QCOMPARE(view->property("pictureCount").toInt(), 1);                 // the cid picture
            QString text;
            QMetaObject::invokeMethod(view, "plainText", Q_RETURN_ARG(QString, text));
            QVERIFY2(text.contains(QStringLiteral("soup of the day")), qPrintable(text));
            QVERIFY(text.contains(QStringLiteral("Banner")));                     // alt text instead of the picture
            QQuickItem *bar = scene.item(QStringLiteral("readingPicturesBar"));
            QVERIFY(bar != nullptr);
            QTRY_VERIFY(bar->isVisible());
            QTest::qWait(150);
            QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindamail_html_blocked_%1").arg(width));
            QVERIFY(fetched.isEmpty());
            QQuickItem *show = scene.item(QStringLiteral("readingShowPictures"));
            QVERIFY(show != nullptr && show->isVisible());
            const QPoint at = show->mapToScene(QPointF(show->width() / 2, show->height() / 2)).toPoint();
            QVERIFY2(QRect(QPoint(0, 0), scene.window->size()).contains(at), "Show pictures is outside the window");
            QTest::mouseClick(scene.window, Qt::LeftButton, {}, at);
            QTRY_COMPARE(fetched, QStringList{QStringLiteral("https://news.example/banner.png")});
            QTRY_VERIFY(!view->property("remoteBlocked").toBool());
            QTRY_COMPARE(view->property("pictureCount").toInt(), 2);
            QTRY_VERIFY(!bar->isVisible());
            QTest::qWait(150);
            QQuickItem *paper = scene.item(QStringLiteral("readingPaper"));
            QQuickItem *body = scene.item(QStringLiteral("readingBody"));
            QVERIFY(paper != nullptr && body != nullptr);
            // The paper keeps wrapping the body after the re-render (Flex relayout).
            QCOMPARE(paper->height(), body->height() + 2 * body->y());
            QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindamail_html_pictures_%1").arg(width));
        }
    }

    void meetingCardAnswersAnInvitation()
    {
        Scene scene(QSize(1280, 800), false);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        const QByteArray ics = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nMETHOD:REQUEST\r\nBEGIN:VEVENT\r\nUID:card-1@example.org\r\n"
                               "DTSTAMP:20261001T080000Z\r\nDTSTART:20261015T080000Z\r\nDTEND:20261015T090000Z\r\n"
                               "SUMMARY:Budget review\r\nLOCATION:Room 2\r\nORGANIZER;CN=Grace Hopper:mailto:grace@example.org\r\n"
                               "ATTENDEE;PARTSTAT=NEEDS-ACTION:mailto:ada@example.org\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
        const QByteArray raw = "From: Grace Hopper <grace@example.org>\r\nTo: ada@example.org\r\nSubject: Invitation: Budget review\r\n"
                               "Date: Thu, 1 Oct 2026 08:00:00 +0000\r\nMIME-Version: 1.0\r\n"
                               "Content-Type: multipart/alternative; boundary=\"a\"\r\n\r\n--a\r\nContent-Type: text/plain\r\n\r\n"
                               "Please come.\r\n--a\r\nContent-Type: text/calendar; method=REQUEST\r\n\r\n" + ics + "--a--\r\n";
        const QString path = scene.env.dir.filePath(QStringLiteral("invite.eml"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(raw);
        file.close();
        QVERIFY(!scene.env.service->importMessageFile(path).isEmpty());
        QQuickItem *card = nullptr;
        QTRY_VERIFY((card = scene.item(QStringLiteral("readingMeeting"))) != nullptr && card->isVisible());
        QQuickItem *title = scene.item(QStringLiteral("readingMeetingTitle"));
        QVERIFY(title != nullptr);
        QCOMPARE(title->property("text").toString(), QStringLiteral("Budget review"));
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindamail_meeting_1280"));
        // The calendar part is the card, not an attachment chip.
        QQuickItem *attachments = scene.item(QStringLiteral("readingAttachments"));
        QVERIFY(attachments != nullptr && !attachments->isVisible());
        QQuickItem *accept = scene.item(QStringLiteral("readingMeeting_accepted"));
        QVERIFY(accept != nullptr && accept->isVisible());
        QTest::mouseClick(scene.window, Qt::LeftButton, {}, accept->mapToScene(QPointF(accept->width() / 2, accept->height() / 2)).toPoint());
        QQuickItem *status = scene.item(QStringLiteral("readingMeetingStatus"));
        QVERIFY(status != nullptr);
        QTRY_COMPARE(status->property("text").toString(), QStringLiteral("You accepted this meeting."));
        bool inCalendar = false;
        for (const QVariant &value : scene.env.service->store()->events())
            inCalendar = inCalendar || value.toMap().value(QStringLiteral("meetingUid")).toString() == QStringLiteral("card-1@example.org");
        QVERIFY(inCalendar);
    }

    void ribbonGateHoldsInEveryWorkspace()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 800), true);
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            // A command is covered when some workspace's ribbon reaches it.
            QStringList reachable;
            for (const char *view : {"mail", "calendar", "tasks", "people"}) {
                scene.host->setView(QString::fromLatin1(view));
                QTest::qWait(120);
                reachable += ribbonIds(scene.window);
            }
            for (const char *view : {"mail", "calendar", "tasks", "people"}) {
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
        QMetaObject::invokeMethod(dialog, "close");
        // M64: the event dialog invites people (suggestions from People and mail).
        QVERIFY(scene.host->actions()->trigger(QStringLiteral("mailNewEventAction")));
        QObject *eventDialog = scene.window->findChild<QObject *>(QStringLiteral("eventDialog"));
        QVERIFY(eventDialog != nullptr);
        QTRY_VERIFY(eventDialog->property("opened").toBool());
        auto *eventBody = eventDialog->property("contentItem").value<QQuickItem *>();
        QVERIFY(eventBody != nullptr && findNamed(eventBody, QStringLiteral("eventAttendees")) != nullptr);
        QTest::qWait(150);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindamail_event_dialog"));
    }

    void accountSignInUsesCredentialDialog_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<bool>("appPassword");
        QTest::addColumn<bool>("configuredOAuth");
        QTest::addColumn<int>("width");
        for (const int width : {1280, 520}) {
            const QByteArray suffix = QByteArray::number(width);
            QTest::newRow(("password-" + suffix).constData()) << QStringLiteral("imap") << false << false << width;
            QTest::newRow(("gmail-app-password-" + suffix).constData()) << QStringLiteral("gmail") << true << false << width;
            QTest::newRow(("google-missing-client-" + suffix).constData()) << QStringLiteral("gmail") << false << false << width;
            QTest::newRow(("google-configured-" + suffix).constData()) << QStringLiteral("gmail") << false << true << width;
        }
    }

    void accountSignInUsesCredentialDialog()
    {
        QFETCH(QString, kind);
        QFETCH(bool, appPassword);
        QFETCH(bool, configuredOAuth);
        QFETCH(int, width);
        Scene scene(QSize(width, 800), false);
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        auto *service = scene.env.service.get();
        QVariantMap account = service->accountStore()->draftFor(kind, QStringLiteral("ada@example.org"));
        QVariantMap incoming = account.value(QStringLiteral("incoming")).toMap();
        incoming.insert(QStringLiteral("host"), QStringLiteral("imap.example.invalid"));
        incoming.insert(QStringLiteral("port"), 993);
        incoming.insert(QStringLiteral("security"), QStringLiteral("tls"));
        QVariantMap outgoing = account.value(QStringLiteral("outgoing")).toMap();
        outgoing.insert(QStringLiteral("host"), QStringLiteral("smtp.example.invalid"));
        outgoing.insert(QStringLiteral("port"), 465);
        outgoing.insert(QStringLiteral("security"), QStringLiteral("tls"));
        if (appPassword) {
            incoming.insert(QStringLiteral("authMechanism"), QStringLiteral("plain"));
            outgoing.insert(QStringLiteral("authMechanism"), QStringLiteral("plain"));
        }
        account.insert(QStringLiteral("incoming"), incoming);
        account.insert(QStringLiteral("outgoing"), outgoing);
        account.insert(QStringLiteral("calendar"), QVariantMap{{QStringLiteral("protocol"), QStringLiteral("none")}});
        if (configuredOAuth) {
            QVariantMap oauth = account.value(QStringLiteral("oauth")).toMap();
            oauth.insert(QStringLiteral("clientId"), QStringLiteral("synthetic-client.apps.googleusercontent.com"));
            account.insert(QStringLiteral("oauth"), oauth);
        }
        const QVariantMap added = service->addAccount(account);
        QVERIFY2(added.value(QStringLiteral("ok")).toBool(), qPrintable(added.value(QStringLiteral("error")).toString()));
        const QString id = added.value(QStringLiteral("id")).toString();
        auto *runtime = service->runtime(id);
        QVERIFY(runtime != nullptr);
        if (kind == QLatin1String("imap") || appPassword || configuredOAuth)
            runtime->setStatus(QStringLiteral("needsSignIn"), QStringLiteral("Synthetic credential needs renewal"));
        const QString problem = service->accountById(id).value(QStringLiteral("lastError")).toString();
        if (kind == QLatin1String("gmail") && !appPassword && !configuredOAuth)
            QVERIFY2(problem.contains(QStringLiteral("No OAuth client ID")), qPrintable(problem));

        // A second real window shares the service but must not receive this
        // window's dialog intent or its correlated sign-in error.
        MailHost otherHost(service);
        QQmlComponent component(&scene.engine, QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Mail/App/MailWindow.qml")));
        std::unique_ptr<QObject> otherWindow(component.createWithInitialProperties(
            {{QStringLiteral("host"), QVariant::fromValue(&otherHost)}}));
        QVERIFY2(otherWindow != nullptr, qPrintable(component.errorString()));
        QObject *otherDialog = otherWindow->findChild<QObject *>(QStringLiteral("accountDialog"));
        QVERIFY(otherDialog != nullptr);
        scene.window->requestActivate();
        QTRY_VERIFY(scene.window->isActive());
        QSignalSpy requested(scene.host.get(), &MailHost::accountDialogRequested);
        QSignalSpy otherRequested(&otherHost, &MailHost::accountDialogRequested);
        QSignalSpy operations(service, &MailTest::MailService::operationFinished);
        QQuickItem *button = nullptr;
        QTRY_VERIFY((button = scene.item(QStringLiteral("folderSignIn_") + id)) != nullptr && button->isVisible() && button->width() > 0 && button->height() > 0);
        QTest::qWait(150); // the folder delegate's Flex layout settles on the next frame
        QSignalSpy clicked(button, SIGNAL(clicked()));
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindamail_sign_in_before_%1").arg(QString::fromLatin1(QTest::currentDataTag())));
        QTest::mouseClick(scene.window, Qt::LeftButton, {}, button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QCOMPARE(clicked.count(), 1);
        QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("accountDialog"));
        QVERIFY(dialog != nullptr);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.first().first().toString(), id);
        QCOMPARE(otherRequested.count(), 0);
        QVERIFY(!otherDialog->property("opened").toBool());
        QCOMPARE(operations.count(), 0); // opening an editor never authorizes or syncs
        QCOMPARE(dialog->property("editingId").toString(), id);
        QCOMPARE(dialog->property("kind").toString(), kind);
        QCOMPARE(dialog->property("step").toString(), QStringLiteral("details"));
        auto *body = dialog->property("contentItem").value<QQuickItem *>();
        QVERIFY(body != nullptr);
        QQuickItem *notice = findNamed(body, QStringLiteral("accountProblem"));
        QVERIFY(notice != nullptr);
        QTRY_VERIFY(notice->isVisible());
        QCOMPARE(notice->property("text").toString(), problem);
        QQuickItem *password = findNamed(body, QStringLiteral("accountPassword"));
        QVERIFY(password != nullptr);
        QCOMPARE(password->isVisible(), kind == QLatin1String("imap") || appPassword);

        if (password->isVisible()) {
            password->forceActiveFocus();
            QTRY_VERIFY(password->hasActiveFocus());
            QTest::keyClick(scene.window, Qt::Key_P);
            QTRY_COMPARE(password->property("text").toString(), QStringLiteral("p"));
        } else {
            const auto browserRequests = std::make_shared<int>(0);
            // updateAccount rebuilds its runtime. Intercept each new OAuth
            // object's browser adapter; the actual signIn path still runs,
            // but no desktop browser or remote request can be started.
            connect(service, &MailTest::MailService::accountsChanged, scene.host.get(), [service, id, browserRequests] {
                auto *current = service->runtime(id);
                if (current != nullptr && current->oauth() != nullptr)
                    current->oauth()->setBrowserOpener([browserRequests](const QUrl &) { ++*browserRequests; return false; });
            });
            auto *footer = dialog->property("footer").value<QQuickItem *>();
            QVERIFY(footer != nullptr);
            QQuickItem *primary = findNamed(footer, QStringLiteral("dialogPrimary"));
            QVERIFY(primary != nullptr && primary->isVisible());
            QTest::qWait(150);
            QSignalSpy acceptedClick(primary, SIGNAL(clicked()));
            QTest::mouseClick(scene.window, Qt::LeftButton, {}, primary->mapToScene(QPointF(primary->width() / 2, primary->height() / 2)).toPoint());
            QCOMPARE(acceptedClick.count(), 1);
            if (configuredOAuth) {
                // Browser-open failure intentionally keeps OAuth waiting for
                // a manual redirect. End this private synthetic flow through
                // its public test API to exercise a real correlated rejection.
                QTRY_COMPARE(*browserRequests, 1);
                auto *current = service->runtime(id);
                QVERIFY(current != nullptr && current->oauth() != nullptr);
                QVERIFY(current->oauth()->authorizing());
                QCOMPARE(operations.count(), 0);
                QTRY_VERIFY(dialog->property("opened").toBool());
                QVERIFY(!dialog->property("primaryEnabled").toBool());
                current->oauth()->cancel();
            }
            QTRY_COMPARE(operations.count(), 1);
            QCOMPARE(operations.first().at(1).toString(), QStringLiteral("rejected"));
            QTRY_VERIFY(dialog->property("opened").toBool());
            QTRY_COMPARE(notice->property("text").toString(), operations.first().at(2).toString());
            QVERIFY(notice->isVisible());
            QCOMPARE(dialog->property("editingId").toString(), id);
            QCOMPARE(dialog->property("kind").toString(), kind);
            QCOMPARE(dialog->property("step").toString(), QStringLiteral("details"));
            QCOMPARE(*browserRequests, configuredOAuth ? 1 : 0);
            QCOMPARE(otherDialog->property("accountError").toString(), QString());
            QVERIFY(!otherDialog->property("opened").toBool());
        }
        QTest::qWait(200);
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindamail_sign_in_%1").arg(QString::fromLatin1(QTest::currentDataTag())));
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
