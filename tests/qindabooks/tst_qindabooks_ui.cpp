// SPDX-License-Identifier: GPL-3.0-or-later
// QindaBooks through the real window (M47): a seeded company (customers,
// products, invoices — one overdue, one paid — bills, an expense and an
// imported bank statement) renders in every workspace; the dashboard shows
// its numbers; the invoice editor, the reports and the drill path open;
// the ribbon gate holds at 1280 and 520 px; structural fixtures.
// QQO_GRAB_DIR saves PNGs of each workspace for review.

#include "books_app_test_support.h"

#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QList>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>

using namespace BooksAppTest;
using QindaOffice::Test::findNamed;

namespace {

QString str(const char *text) { return QString::fromUtf8(text); }

struct Scene {
    QTemporaryDir dir;
    QQmlApplicationEngine engine;
    BooksHost host;
    QQuickWindow *window = nullptr;
    QString error;
    qint64 invoiceId = 0;

    explicit Scene(const QSize &size)
    {
        BooksService *books = host.books();
        books->setToday(kToday);
        const QVariantMap created = books->createCompany(
            url(dir, str("Northwind.qbooks")),
            {{str("name"), str("Northwind Creative")}, {str("email"), str("hello@northwind.studio")},
             {str("address"), str("418 Alder Street, Suite 3\nPortland, OR 97204")}},
            str("services"));
        if (!ok(created)) {
            error = created.value(str("error")).toString();
            return;
        }
        seed(books);
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine, QUrl(str("qrc:/qt/qml/QindaOffice/Books/App/BooksWindow.qml")));
        QObject *object = component.createWithInitialProperties({{str("host"), QVariant::fromValue(&host)}});
        if (object == nullptr) {
            error = component.errorString();
            return;
        }
        window = qobject_cast<QQuickWindow *>(object);
        host.attachWindow(window);
        window->resize(size);
        window->show();
    }
    ~Scene() { delete window; }

    static qint64 accountByName(BooksService *books, const QString &fragment)
    {
        for (const QVariant &v : books->accounts(false)) {
            const QVariantMap a = v.toMap();
            if (a.value(str("name")).toString().contains(fragment, Qt::CaseInsensitive)) return a.value(str("id")).toLongLong();
        }
        return 0;
    }
    void seed(BooksService *books)
    {
        const qint64 income = books->incomeAccounts().value(0).toMap().value(str("id")).toLongLong();
        const auto customer = [&](const char *name, const char *email) {
            return idOf(books->saveParty({{str("kind"), str("customer")}, {str("name"), str(name)}, {str("email"), str(email)}}));
        };
        const qint64 harbor = customer("Harbor & Pine Coffee Co.", "accounts@harborpine.coffee");
        const qint64 fscc = customer("Fresh Start Center", "office@freshstart.org");
        const qint64 river = customer("Riverbend Books", "hello@riverbend.shop");
        const qint64 design = idOf(books->saveItem({{str("name"), str("Brand identity")}, {str("type"), str("service")},
                                                    {str("price"), str("2400.00")}, {str("incomeAccountId"), income},
                                                    {str("description"), str("Logo suite, colour and type guidelines")}}));
        const qint64 hours = idOf(books->saveItem({{str("name"), str("Illustration (hourly)")}, {str("type"), str("service")},
                                                   {str("price"), str("95.00")}, {str("incomeAccountId"), income}}));
        const auto invoice = [&](qint64 who, const char *date, QVariantList lines) {
            return idOf(books->saveDocument({{str("kind"), str("invoice")}, {str("partyId"), who}, {str("date"), str(date)},
                                             {str("state"), str("open")}, {str("lines"), lines}}));
        };
        const auto line = [&](qint64 item, const char *qty, const char *rate) {
            return QVariantMap{{str("itemId"), item}, {str("quantity"), str(qty)}, {str("rate"), str(rate)},
                               {str("description"), QString()}};
        };
        invoiceId = invoice(harbor, "2026-03-02", {line(design, "1", "2400.00"), line(hours, "12.5", "95.00")});
        invoice(fscc, "2026-01-20", {line(hours, "8", "95.00")});
        const qint64 paid = invoice(river, "2026-02-10", {line(hours, "20", "95.00")});
        invoice(river, "2026-03-25", {line(design, "1", "1800.00")});
        const QVariantList payAccounts = books->paymentAccounts();
        const qint64 bank = payAccounts.value(0).toMap().value(str("id")).toLongLong();
        books->receivePayment({{str("customerId"), river}, {str("date"), str("2026-03-05")}, {str("amount"), str("1900.00")},
                               {str("depositAccountId"), bank}, {str("method"), str("ACH")},
                               {str("applications"), QVariantList{QVariantMap{{str("invoiceId"), paid}, {str("amount"), str("1900.00")}}}}});
        const QVariantList expenseAccounts = books->expenseAccounts();
        const auto expenseAccount = [&](int i) { return expenseAccounts.value(i % qMax(1, int(expenseAccounts.size()))).toMap().value(str("id")).toLongLong(); };
        const qint64 adobe = idOf(books->saveParty({{str("kind"), str("vendor")}, {str("name"), str("Adobe")}}));
        const qint64 printer = idOf(books->saveParty({{str("kind"), str("vendor")}, {str("name"), str("Pioneer Print Shop")}}));
        books->saveDocument({{str("kind"), str("bill")}, {str("partyId"), printer}, {str("date"), str("2026-03-10")},
                             {str("lines"), QVariantList{QVariantMap{{str("accountId"), expenseAccount(0)}, {str("description"), str("Menu cards")},
                                                                     {str("quantity"), str("1")}, {str("rate"), str("360.00")}}}}});
        books->saveDocument({{str("kind"), str("expense")}, {str("partyId"), adobe}, {str("date"), str("2026-03-03")},
                             {str("paymentAccountId"), bank},
                             {str("lines"), QVariantList{QVariantMap{{str("accountId"), expenseAccount(1)}, {str("description"), str("Creative Cloud")},
                                                                     {str("quantity"), str("1")}, {str("rate"), str("89.99")}}}}});
        const QString csv = dir.filePath(str("statement.csv"));
        writeFile(csv, "Date,Description,Amount\n2026-03-05,RIVERBEND BOOKS ACH,1900.00\n2026-03-03,ADOBE *CREATIVE CLD,-89.99\n"
                       "2026-03-18,PORTLAND GENERAL ELECTRIC,-142.30\n2026-03-28,STRIPE PAYOUT,612.45\n");
        books->importStatement(bank, QUrl::fromLocalFile(csv), {});
    }
    [[nodiscard]] QQuickItem *item(const QString &name) const { return findNamed(window->contentItem(), name); }
};

} // namespace

class BooksUiTest final : public QObject {
    Q_OBJECT
private slots:
    void workspacesRenderTheCompany()
    {
        Scene scene(QSize(1320, 840));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTest::qWait(250);
        QQuickItem *income = scene.item(str("kpiIncome"));
        QVERIFY(income != nullptr);
        QindaOffice::Test::saveGrab(scene.window, str("qindabooks_dashboard_1320"));
        QVERIFY(!findNamed(income, str("kpiValue"))->property("text").toString().isEmpty());
        for (const char *view : {"sales", "expenses", "banking", "accounting", "reports"}) {
            scene.host.setCurrentView(str(view));
            QTest::qWait(250);
            QindaOffice::Test::saveGrab(scene.window, str("qindabooks_%1_1320").arg(str(view)));
        }
        QQuickItem *rows = scene.item(str("reportRows"));
        QVERIFY(rows != nullptr);
        QTRY_VERIFY(rows->property("count").toInt() > 3);
    }

    void invoiceEditorShowsTotals()
    {
        Scene scene(QSize(1320, 840));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        emit scene.host.documentEditorRequested(str("invoice"), scene.invoiceId);
        QObject *editor = scene.window->findChild<QObject *>(str("documentEditor"));
        QVERIFY(editor != nullptr);
        QTRY_VERIFY(editor->property("opened").toBool());
        QTest::qWait(200);
        QindaOffice::Test::saveGrab(scene.window, str("qindabooks_invoice_editor"));
        const QVariantMap totals = editor->property("totals").toMap();
        QVERIFY2(!totals.value(str("totalText")).toString().isEmpty(), "the service computed the invoice's totals");
    }

    void connectionsDialogOpens()
    {
        Scene scene(QSize(1320, 840));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host.actions()->trigger(str("booksConnectionsAction")));
        QObject *dialog = scene.window->findChild<QObject *>(str("connectionsDialog"));
        QVERIFY(dialog != nullptr);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::qWait(200);
        QindaOffice::Test::saveGrab(scene.window, str("qindabooks_connections"));
        // Nothing is connected in a fresh company: both cards offer Connect.
        QVERIFY(scene.window->findChild<QObject *>(str("mercuryConnect")) != nullptr);
        QVERIFY(scene.window->findChild<QObject *>(str("stripeConnect")) != nullptr);
    }

    void ribbonGateHolds()
    {
        for (const int width : {1320, 520}) {
            Scene scene(QSize(width, 800));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QTest::qWait(150);
            QindaOffice::Test::saveGrab(scene.window, str("qindabooks_ribbon_%1").arg(width));
            const QStringList problems = QindaOffice::Test::ribbonProblems(
                scene.window, scene.host.menuTree(),
                // Edit commands act on the focused field; Save is a no-op on an
                // open company (D-047) — menus and the command search reach them.
                {str("editCutAction"), str("editCopyAction"), str("editPasteAction"), str("editSelectAllAction"),
                 str("editFindAction"), str("editReplaceAction"), str("fileSaveAsAction")});
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        }
    }

    void structuralFixtures()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, width == 1280 ? 800 : 700));
            QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QTest::qWait(200);
            QQuickItem *header = scene.item(str("dashHeader"));
            QVERIFY(header != nullptr);
            QList<QRectF> occupied;
            for (const char *name : {"dashCompanyHeading", "dashNewInvoice", "dashReceivePayment", "dashNewExpense"}) {
                QQuickItem *item = scene.item(str(name));
                QVERIFY2(item != nullptr, name);
                const QRectF bounds = item->mapRectToItem(header, item->boundingRect());
                QVERIFY2(bounds.width() > 0 && bounds.height() > 0, name);
                QVERIFY2(bounds.left() >= -1 && bounds.top() >= -1
                             && bounds.right() <= header->width() + 1 && bounds.bottom() <= header->height() + 1, name);
                for (const QRectF &other : occupied) QVERIFY2(!bounds.intersects(other), name);
                occupied.append(bounds);
            }
            QindaOffice::Test::saveGrab(scene.window, str("qindabooks_dashboard_header_%1").arg(width));
            QString diff;
            const QString path = QStringLiteral(QQO_FIXTURE_DIR "/qindabooks_%1.txt").arg(width);
            QVERIFY2(QindaOffice::Test::matchesFixture(scene.window, path, &diff), qPrintable(diff));
        }
    }
};

BOOKS_APP_TEST_MAIN(BooksUiTest)

#include "tst_qindabooks_ui.moc"
