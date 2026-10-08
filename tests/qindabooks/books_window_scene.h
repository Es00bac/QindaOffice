// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The real QindaBooks window over a small company (M63 dialog tests): two
// customers, a vendor, a service item, an open invoice and a bill in Q1
// 2026, "today" pinned to 2026-03-31. Dialogs are reached by objectName and
// driven through their QML functions — never through a native file dialog.

#include "books_app_test_support.h"

#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>

namespace BooksAppTest {

struct WindowScene {
    QTemporaryDir dir;
    QQmlApplicationEngine engine;
    BooksHost host;
    QQuickWindow *window = nullptr;
    QString error;
    qint64 harbor = 0;
    qint64 riverbend = 0;
    qint64 printer = 0;
    qint64 invoiceId = 0;

    explicit WindowScene(const QSize &size = QSize(1320, 840))
    {
        BooksService *books = host.books();
        books->setToday(kToday);
        const QVariantMap created = books->createCompany(
            url(dir, QStringLiteral("Lantern.qbooks")),
            {{QStringLiteral("name"), QStringLiteral("Lantern Design Co")},
             {QStringLiteral("email"), QStringLiteral("hello@lantern.test")},
             {QStringLiteral("address"), QStringLiteral("12 Ember Lane\nSpringfield, OR 97477")}},
            QStringLiteral("general"));
        if (!ok(created)) {
            error = created.value(QStringLiteral("error")).toString();
            return;
        }
        seed(books);
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Books/App/BooksWindow.qml")));
        QObject *object = component.createWithInitialProperties({{QStringLiteral("host"), QVariant::fromValue(&host)}});
        if (object == nullptr) {
            error = component.errorString();
            return;
        }
        window = qobject_cast<QQuickWindow *>(object);
        host.attachWindow(window);
        window->resize(size);
        window->show();
    }
    ~WindowScene() { delete window; }
    WindowScene(const WindowScene &) = delete;
    WindowScene &operator=(const WindowScene &) = delete;

    qint64 account(const QString &number) const
    {
        return find(host.books()->accounts(true), QStringLiteral("number"), number).value(QStringLiteral("id")).toLongLong();
    }
    void seed(BooksService *books)
    {
        const auto party = [books](const char *kind, const char *name, const char *email) {
            return idOf(books->saveParty({{QStringLiteral("kind"), QString::fromLatin1(kind)},
                                          {QStringLiteral("name"), QString::fromUtf8(name)},
                                          {QStringLiteral("email"), QString::fromUtf8(email)}}));
        };
        harbor = party("customer", "Harbor & Pine Coffee", "ap@harbor.test");
        riverbend = party("customer", "Riverbend Books", "");
        printer = party("vendor", "Pioneer Print Shop", "");
        const QVariantMap line{{QStringLiteral("description"), QStringLiteral("Brand refresh")},
                               {QStringLiteral("accountId"), account(QStringLiteral("4100"))},
                               {QStringLiteral("amount"), QStringLiteral("1200.00")}};
        invoiceId = idOf(books->saveDocument({{QStringLiteral("kind"), QStringLiteral("invoice")},
                                              {QStringLiteral("partyId"), harbor},
                                              {QStringLiteral("date"), QStringLiteral("2026-02-02")},
                                              {QStringLiteral("lines"), QVariantList{line}}}));
        books->saveDocument({{QStringLiteral("kind"), QStringLiteral("bill")},
                             {QStringLiteral("partyId"), printer},
                             {QStringLiteral("date"), QStringLiteral("2026-03-03")},
                             {QStringLiteral("lines"),
                              QVariantList{QVariantMap{{QStringLiteral("description"), QStringLiteral("Proofs")},
                                                       {QStringLiteral("accountId"), account(QStringLiteral("6500"))},
                                                       {QStringLiteral("amount"), QStringLiteral("80.00")}}}}});
    }
    [[nodiscard]] QObject *object(const QString &name) const { return window->findChild<QObject *>(name); }
    [[nodiscard]] QQuickItem *item(const QString &name) const
    {
        return QindaOffice::Test::findNamed(window->contentItem(), name);
    }
    // Calls a QML function of `target` with up to three arguments.
    static QVariant call(QObject *target, const char *method, const QVariant &a = {}, const QVariant &b = {},
                         const QVariant &c = {})
    {
        QVariant out;
        const QMetaMethod m = target->metaObject()->method(target->metaObject()->indexOfMethod(
            QByteArray(method) + (c.isValid() ? "(QVariant,QVariant,QVariant)" : b.isValid() ? "(QVariant,QVariant)"
                                                                       : a.isValid() ? "(QVariant)" : "()")));
        if (c.isValid()) {
            m.invoke(target, Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, a), Q_ARG(QVariant, b), Q_ARG(QVariant, c));
        } else if (b.isValid()) {
            m.invoke(target, Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, a), Q_ARG(QVariant, b));
        } else if (a.isValid()) {
            m.invoke(target, Q_RETURN_ARG(QVariant, out), Q_ARG(QVariant, a));
        } else {
            m.invoke(target, Q_RETURN_ARG(QVariant, out));
        }
        return out;
    }
    // Opens a dialog through its command and waits until it is shown.
    QObject *openThrough(const QString &command, const QString &dialogName)
    {
        if (!host.actions()->trigger(command)) {
            return nullptr;
        }
        QObject *dialog = object(dialogName);
        if (dialog == nullptr || !QTest::qWaitFor([dialog] { return dialog->property("opened").toBool(); }, 3000)) {
            return nullptr;
        }
        QTest::qWait(120);
        return dialog;
    }
};

} // namespace BooksAppTest
