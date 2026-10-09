// SPDX-License-Identifier: GPL-3.0-or-later
// QindaMail's address book in mail (M64, D-072): the service's ContactStore
// lives beside the mail store, "Add sender to contacts" adds (once), the
// composer's recipient suggestions come from contacts first and then from
// addresses seen in mail (never your own), a vCard handed to the app lands
// in People, the People workspace lists, edits and saves through the real
// window, and the composer's To field offers a contact as you type.
// M76 (D-085): a contact group made in People (a contact and a plain
// address, through the same suggestions) expands in the composer's To field.
// Headless; fixtures written here.

#include "mail_test_support.h"

#include "qindamail/src/mail_host.h"
#include "qindamail/src/recipient_completer.h"

#include "contact_store.h"
#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <qindatk/local_productivity_store.h>

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>

using namespace QindaOffice::Apps::Mail;
using QindaOffice::Test::findNamed;

namespace {

const QByteArray kCards = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Grace Hopper\r\nN:Hopper;Grace;;;\r\n"
                          "EMAIL;TYPE=work:grace@example.org\r\nORG:Navy\r\nEND:VCARD\r\n"
                          "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Alan Turing\r\nN:Turing;Alan;;;\r\n"
                          "EMAIL:alan@example.org\r\nTEL;TYPE=cell:+44 20 7946 0000\r\nEND:VCARD\r\n";

QStringList values(const QVariantList &candidates)
{
    QStringList out;
    for (const QVariant &candidate : candidates) out.append(candidate.toMap().value(QStringLiteral("value")).toString());
    return out;
}

} // namespace

class TestMailPeople : public QObject {
    Q_OBJECT

private slots:
    void contactsLiveBesideTheMailStore()
    {
        MailTest::Env env;
        QindaOffice::Mail::ContactStore *contacts = env.service->contacts();
        QVERIFY(contacts != nullptr);
        QCOMPARE(QFileInfo(contacts->path()).absolutePath(), QFileInfo(env.dir.filePath(QStringLiteral("data/qindaoffice/mail/x"))).absolutePath());
        const QVariantMap result = contacts->importData(kCards, QStringLiteral("people.vcf"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("added")).toInt(), 2);
        auto reopened = env.reopen();
        QCOMPARE(reopened->contacts()->count(), 2);
    }

    void addSenderAddsOnce()
    {
        MailTest::Env env;
        env.service->store()->upsert(QStringLiteral("messages"),
                                     MailTest::message(QStringLiteral("m1"), QStringLiteral("Hello"), QDateTime::currentDateTimeUtc()));
        const QVariantMap first = env.service->addSenderToContacts(QStringLiteral("m1"));
        QVERIFY(first.value(QStringLiteral("ok")).toBool());
        QVERIFY(!first.value(QStringLiteral("existed")).toBool());
        const QVariantMap contact = env.service->contacts()->contactForAddress(QStringLiteral("GRACE@example.org"));
        QCOMPARE(contact.value(QStringLiteral("displayName")).toString(), QStringLiteral("Grace Hopper"));
        const QVariantMap second = env.service->addSenderToContacts(QStringLiteral("m1"));
        QVERIFY(second.value(QStringLiteral("ok")).toBool());
        QVERIFY(second.value(QStringLiteral("existed")).toBool());
        QCOMPARE(env.service->contacts()->count(), 1);
        QVERIFY(!env.service->addSenderToContacts(QStringLiteral("missing")).value(QStringLiteral("ok")).toBool());
    }

    void suggestionsComeFromContactsThenMail()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        QVERIFY(service->contacts()->importData(kCards, QStringLiteral("people.vcf")).value(QStringLiteral("ok")).toBool());
        // Seen in mail: a sender twice, a Cc once.
        for (int i = 0; i < 2; ++i) {
            service->store()->upsert(QStringLiteral("messages"), MailTest::message(
                QStringLiteral("m%1").arg(i), QStringLiteral("Note"), QDateTime::currentDateTimeUtc().addDays(-i),
                {{QStringLiteral("sender"), QStringLiteral("Gregor Mendel")},
                 {QStringLiteral("senderAddress"), QStringLiteral("gregor@peas.example")},
                 {QStringLiteral("cc"), QStringLiteral("Gertrude Bell <gertrude@example.org>")}}));
        }
        service->store()->upsert(QStringLiteral("messages"), MailTest::message(QStringLiteral("m9"), QStringLiteral("x"),
            QDateTime::currentDateTimeUtc(), {{QStringLiteral("to"), QStringLiteral("Grace H <grace@example.org>")}}));
        QTest::qWait(20);
        const QStringList gr = values(service->recipientSuggestions(QStringLiteral("gr")));
        QVERIFY2(!gr.isEmpty() && gr.first() == QStringLiteral("grace@example.org"), qPrintable(gr.join(QLatin1Char(','))));
        QVERIFY(gr.contains(QStringLiteral("gregor@peas.example")));
        QCOMPARE(gr.count(QStringLiteral("grace@example.org")), 1);         // contact and mail: one entry
        QCOMPARE(values(service->recipientSuggestions(QStringLiteral("bell"))), QStringList{QStringLiteral("gertrude@example.org")});
        QCOMPARE(values(service->recipientSuggestions(QStringLiteral("turing"))), QStringList{QStringLiteral("alan@example.org")});
        QVERIFY(service->recipientSuggestions(QStringLiteral("   ")).isEmpty());
        // Never the user's own address (ada@example.org is in every To).
        QVERIFY(!values(service->recipientSuggestions(QStringLiteral("ada"))).contains(QStringLiteral("ada@example.org"))
                || service->ownAddresses().isEmpty());
        // The TokenField contract: an id now, the answer on a later turn.
        RecipientCompleter completer;
        completer.setService(service);
        QSignalSpy finished(&completer, &RecipientCompleter::finished);
        const QString id = completer.request(QStringLiteral("alan"));
        QCOMPARE(finished.count(), 0);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toString(), id);
        const QVariantList candidates = finished.first().at(1).toMap().value(QStringLiteral("candidates")).toList();
        QCOMPARE(values(candidates), QStringList{QStringLiteral("alan@example.org")});
        QVERIFY(candidates.first().toMap().value(QStringLiteral("label")).toString().contains(QStringLiteral("Alan Turing")));
    }

    void vcardFromTheFileManagerLandsInPeople()
    {
        MailTest::Env env;
        MailHost host(env.service.get());
        const QString path = env.dir.filePath(QStringLiteral("card.vcf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(kCards);
        file.close();
        QVERIFY(host.loadPath(path));
        QCOMPARE(host.view(), QStringLiteral("people"));
        QCOMPARE(env.service->contacts()->count(), 2);
    }

    void peopleWorkspaceEditsThroughTheWindow()
    {
        MailTest::Env env;
        QVERIFY(env.service->contacts()->importData(kCards, QStringLiteral("people.vcf")).value(QStringLiteral("ok")).toBool());
        MailHost host(env.service.get());
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Mail/App/MailWindow.qml")));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{QStringLiteral("host"), QVariant::fromValue(&host)}}));
        QVERIFY2(object != nullptr, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(object.get());
        host.attachWindow(window);
        window->resize(1280, 800);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(host.actions()->trigger(QStringLiteral("mailViewPeopleAction")));
        QCOMPARE(host.view(), QStringLiteral("people"));
        QQuickItem *rows = findNamed(window->contentItem(), QStringLiteral("peopleRows"));
        QVERIFY(rows != nullptr);
        QTRY_COMPARE(rows->property("count").toInt(), 2);
        const QString graceId = env.service->contacts()->contactForAddress(QStringLiteral("grace@example.org")).value(QStringLiteral("id")).toString();
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findNamed(window->contentItem(), QStringLiteral("peopleRow_") + graceId)) != nullptr);
        QTest::qWait(100);
        QTest::mouseClick(window, Qt::LeftButton, {}, row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint());
        QTRY_COMPARE(host.selectedContactId(), graceId);
        QVERIFY(host.actions()->entry(QStringLiteral("contactDeleteAction"))->isEnabled());
        QQuickItem *company = findNamed(window->contentItem(), QStringLiteral("contactCompany"));
        QVERIFY(company != nullptr);
        QTRY_COMPARE(company->property("text").toString(), QStringLiteral("Navy"));
        QindaOffice::Test::saveGrab(window, QStringLiteral("qindamail_people_1280"));
        // Edit and save through the editor's own functions (what its buttons call).
        QObject *editor = findNamed(window->contentItem(), QStringLiteral("contactEditor"));
        QVERIFY(editor != nullptr);
        company->setProperty("text", QStringLiteral("US Navy"));
        editor->setProperty("dirty", true);
        QVariant saved;
        QVERIFY(QMetaObject::invokeMethod(editor, "save", Q_RETURN_ARG(QVariant, saved)));
        QVERIFY(saved.toBool());
        QCOMPARE(env.service->contacts()->contactById(graceId).value(QStringLiteral("organization")).toString(), QStringLiteral("US Navy"));
        // New Contact opens an empty card; the ribbon gate holds in People.
        QVERIFY(host.actions()->trigger(QStringLiteral("contactNewAction")));
        QTRY_COMPARE(host.selectedContactId(), QString());
        QTRY_COMPARE(company->property("text").toString(), QString());
        window->resize(520, 800);
        QTest::qWait(200);
        QindaOffice::Test::saveGrab(window, QStringLiteral("qindamail_people_520"));

        // The composer's To field suggests from People as you type.
        window->resize(1280, 800);
        QVERIFY(host.actions()->trigger(QStringLiteral("mailNewMessageAction")));
        QQuickWindow *composer = nullptr;
        QTRY_VERIFY([&] {
            for (QWindow *w : QGuiApplication::allWindows())
                if (w != window && w->isVisible() && w->title().contains(QStringLiteral("QindaMail"))) composer = qobject_cast<QQuickWindow *>(w);
            return composer != nullptr;
        }());
        QVERIFY(QTest::qWaitForWindowExposed(composer));
        QQuickItem *to = findNamed(composer->contentItem(), QStringLiteral("composerTo"));
        QVERIFY(to != nullptr);
        auto *input = to->property("inputItem").value<QQuickItem *>();
        QVERIFY(input != nullptr);
        composer->requestActivate();
        input->forceActiveFocus();
        QTRY_VERIFY(input->hasActiveFocus());
        for (const char key : {'t', 'u', 'r'}) QTest::keyClick(composer, key);
        QQuickItem *suggestion = nullptr;
        QTRY_VERIFY((suggestion = findNamed(to, QStringLiteral("tokenSuggestion"))) != nullptr && suggestion->isVisible());
        QVERIFY(suggestion->property("text").toString().contains(QStringLiteral("alan@example.org")));
        QTest::qWait(100);
        QindaOffice::Test::saveGrab(composer, QStringLiteral("qindamail_composer_suggestions"));
        QTest::mouseClick(composer, Qt::LeftButton, {}, suggestion->mapToScene(QPointF(suggestion->width() / 2, suggestion->height() / 2)).toPoint());
        QTRY_COMPARE(to->property("tokens").toList().size(), 1);
        QCOMPARE(to->property("tokens").toList().first().toMap().value(QStringLiteral("value")).toString(),
                 QStringLiteral("alan@example.org"));
        composer->close();
    }

    void groupsThroughTheWindow();
};

namespace {

void typeInto(QQuickWindow *window, QQuickItem *field, const QString &text)
{
    auto *input = field->property("inputItem").value<QQuickItem *>();
    QVERIFY(input != nullptr);
    window->requestActivate();
    input->forceActiveFocus();
    QTRY_VERIFY(input->hasActiveFocus());
    for (const QChar ch : text) QTest::keyClick(window, ch.toLatin1());   // no QWindow keyClicks()
}

QQuickItem *visibleSuggestion(QQuickItem *field)
{
    QQuickItem *suggestion = findNamed(field, QStringLiteral("tokenSuggestion"));
    return suggestion != nullptr && suggestion->isVisible() ? suggestion : nullptr;
}

void click(QQuickWindow *window, QQuickItem *item)
{
    QTest::mouseClick(window, Qt::LeftButton, {}, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
}

} // namespace

void TestMailPeople::groupsThroughTheWindow()
{
    MailTest::Env env;
    QVERIFY(env.service->contacts()->importData(kCards, QStringLiteral("people.vcf")).value(QStringLiteral("ok")).toBool());
    MailHost host(env.service.get());
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Mail/App/MailWindow.qml")));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{QStringLiteral("host"), QVariant::fromValue(&host)}}));
    QVERIFY2(object != nullptr, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(object.get());
    host.attachWindow(window);
    window->resize(1280, 800);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    // New Contact Group opens People with an empty group.
    QVERIFY(host.actions()->trigger(QStringLiteral("contactNewGroupAction")));
    QCOMPARE(host.view(), QStringLiteral("people"));
    QQuickItem *editor = nullptr;
    QTRY_VERIFY((editor = findNamed(window->contentItem(), QStringLiteral("contactGroupEditor"))) != nullptr && editor->isVisible());
    QQuickItem *name = findNamed(editor, QStringLiteral("contactGroupName"));
    QQuickItem *add = findNamed(editor, QStringLiteral("contactGroupAdd"));
    QVERIFY(name != nullptr && add != nullptr);
    name->setProperty("text", QStringLiteral("Team"));
    // A contact through the suggestions, then a plain address with Enter.
    typeInto(window, add, QStringLiteral("gra"));
    QQuickItem *suggestion = nullptr;
    QTRY_VERIFY((suggestion = visibleSuggestion(add)) != nullptr);
    QTest::qWait(100);
    click(window, suggestion);
    QTRY_COMPARE(editor->property("members").toList().size(), 1);
    typeInto(window, add, QStringLiteral("bob@example.net"));
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(editor->property("members").toList().size(), 2);
    const QVariantList members = editor->property("members").toList();
    QCOMPARE(members.at(0).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("contact"));
    QCOMPARE(members.at(1).toMap().value(QStringLiteral("address")).toString(), QStringLiteral("bob@example.net"));
    QVariant saved;
    QVERIFY(QMetaObject::invokeMethod(editor, "save", Q_RETURN_ARG(QVariant, saved)));
    QVERIFY(saved.toBool());
    QString groupId;
    for (const QVariant &value : env.service->contacts()->contacts())
        if (value.toMap().value(QStringLiteral("isGroup")).toBool()) groupId = value.toMap().value(QStringLiteral("id")).toString();
    QVERIFY(!groupId.isEmpty());
    const QVariantMap expanded = env.service->contacts()->expandGroup(groupId);
    QCOMPARE(expanded.value(QStringLiteral("recipients")).toStringList(),
             (QStringList{QStringLiteral("Grace Hopper <grace@example.org>"), QStringLiteral("bob@example.net")}));
    QQuickItem *rows = findNamed(window->contentItem(), QStringLiteral("peopleRows"));
    QTRY_COMPARE(rows->property("count").toInt(), 3);
    QTRY_VERIFY(findNamed(window->contentItem(), QStringLiteral("peopleRow_") + groupId) != nullptr);
    QTest::qWait(150);
    QindaOffice::Test::saveGrab(window, QStringLiteral("qindamail_people_group_1280"));
    window->resize(520, 800);
    QTest::qWait(250);
    QindaOffice::Test::saveGrab(window, QStringLiteral("qindamail_people_group_520"));
    // Everything stays inside the window at 520 px.
    QQuickItem *detail = findNamed(window->contentItem(), QStringLiteral("peopleDetail"));
    QVERIFY(detail->mapToScene(QPointF(detail->width(), 0)).x() <= window->width() + 0.5);
    window->resize(1280, 800);

    // In the composer the group is offered by name and becomes its members.
    QVERIFY(host.actions()->trigger(QStringLiteral("mailNewMessageAction")));
    QQuickWindow *composer = nullptr;
    QTRY_VERIFY([&] {
        for (QWindow *w : QGuiApplication::allWindows())
            if (w != window && w->isVisible() && w->title().contains(QStringLiteral("QindaMail"))) composer = qobject_cast<QQuickWindow *>(w);
        return composer != nullptr;
    }());
    QVERIFY(QTest::qWaitForWindowExposed(composer));
    QQuickItem *to = findNamed(composer->contentItem(), QStringLiteral("composerTo"));
    QVERIFY(to != nullptr);
    typeInto(composer, to, QStringLiteral("tea"));
    QTRY_VERIFY((suggestion = visibleSuggestion(to)) != nullptr);
    QVERIFY(suggestion->property("text").toString().contains(QStringLiteral("Team (group)")));
    QTest::qWait(100);
    QindaOffice::Test::saveGrab(composer, QStringLiteral("qindamail_composer_group"));
    click(composer, suggestion);
    QTRY_COMPARE(to->property("tokens").toList().size(), 2);
    const QVariantList tokens = to->property("tokens").toList();
    QCOMPARE(tokens.at(0).toMap().value(QStringLiteral("value")).toString(), QStringLiteral("grace@example.org"));
    QCOMPARE(tokens.at(1).toMap().value(QStringLiteral("value")).toString(), QStringLiteral("bob@example.net"));
    composer->close();
}

MAIL_TEST_MAIN(TestMailPeople)

#include "tst_qindamail_people.moc"
