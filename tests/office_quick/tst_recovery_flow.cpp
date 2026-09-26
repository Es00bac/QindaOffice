// SPDX-License-Identifier: GPL-3.0-or-later
// Recovered documents (D-055): the journal's entries become ONE dialog's
// rows (duplicates merged), the answer recovers the chosen ones into
// windows and discards the rest for good — and closing the dialog is a
// dismissal. Replaces "one window per recovered document".

#include "document.h"
#include "document_host.h"
#include "office_application.h"
#include "paths.h"
#include "recovery_journal.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Quick;
using QindaOffice::Core::RecoveryJournal;

namespace {

class StubDocument final : public QindaOffice::Core::Document {
public:
    QString text;
    bool load(const QString &, QString * = nullptr) override { return false; }
    bool save(const QString &, QString * = nullptr) override { return false; }
    [[nodiscard]] QStringList mimeTypes() const override { return {QStringLiteral("text/plain")}; }
    [[nodiscard]] QByteArray recoverySnapshot() const override { return text.toUtf8(); }
    bool loadRecovery(const QByteArray &snapshot, QString * = nullptr) override
    {
        text = QString::fromUtf8(snapshot);
        setDirty(true);
        return true;
    }
};

class StubHost final : public DocumentHost {
public:
    StubHost()
    {
        setDocument(new StubDocument);
        finalizeHost();
    }
};

// Stands in for OfficeApplication in the dialog-only cases.
class OfficeProbe final : public QObject {
    Q_OBJECT
public:
    QStringList recovered;
    QStringList discarded;
    int calls = 0;
    Q_INVOKABLE void resolveRecovery(const QStringList &recover, const QStringList &discard)
    {
        ++calls;
        recovered = recover;
        discarded = discard;
    }
};

QQuickItem *findNamed(QQuickItem *root, const QString &name)
{
    if (root == nullptr) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *hit = findNamed(child, name)) return hit;
    }
    return nullptr;
}

QVariantList rows()
{
    return {QVariantMap{{QStringLiteral("key"), QStringLiteral("a")},
                        {QStringLiteral("title"), QStringLiteral("Untitled document")},
                        {QStringLiteral("detail"), QStringLiteral("Never saved")},
                        {QStringLiteral("preview"), QStringLiteral("groceries")}},
            QVariantMap{{QStringLiteral("key"), QStringLiteral("b")},
                        {QStringLiteral("title"), QStringLiteral("letter.odt")},
                        {QStringLiteral("detail"), QStringLiteral("/home/x/letter.odt")},
                        {QStringLiteral("preview"), QString()}}};
}

} // namespace

class RecoveryFlowTest final : public QObject {
    Q_OBJECT

    QQuickWindow *makeDialog(QQmlEngine &engine, OfficeProbe *probe)
    {
        engine.addImportPath(QStringLiteral(QQO_QINDATK_QML_ROOT));
        QQmlComponent component(&engine,
                                QUrl(QStringLiteral("qrc:/qt/qml/QindaOffice/Shell/RecoveryDialog.qml")));
        QObject *object = component.createWithInitialProperties(
            {{QStringLiteral("entries"), rows()},
             {QStringLiteral("office"), QVariant::fromValue(static_cast<QObject *>(probe))}});
        if (object == nullptr) {
            qWarning().noquote() << component.errorString();
        }
        auto *window = qobject_cast<QQuickWindow *>(object);
        if (window != nullptr) {
            window->show();
        }
        return window;
    }

    static void click(QQuickWindow *window, QQuickItem *item)
    {
        QTest::mouseClick(window, Qt::LeftButton, {},
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    }

private slots:
    void journalEntriesBecomeRowsAndTheAnswerIsApplied()
    {
        OfficeApplication office;
        office.setHostFactory([] { return new StubHost; });
        RecoveryJournal journal(QindaOffice::Core::stateDirFor(QindaOffice::Core::appLeaf())
                                + QStringLiteral("/recovery"));
        journal.writeNow(QStringLiteral("untitled-9-1"), QString(), true,
                         QByteArray("<html><body><p>Shopping: eggs, flour</p></body></html>"));
        journal.writeNow(QStringLiteral("untitled-9-2"), QString(), true,
                         QByteArray("<html><body><p>Shopping: eggs, flour</p></body></html>"));
        journal.writeNow(QStringLiteral("untitled-9-3"), QString(), true, QByteArray("plain notes"));

        const QVariantList pending = office.pendingRecovery();
        QCOMPARE(pending.size(), 2); // the byte-identical copy is merged away
        QCOMPARE(pending.first().toMap().value(QStringLiteral("preview")).toString(),
                 QStringLiteral("Shopping: eggs, flour"));
        QCOMPARE(pending.first().toMap().value(QStringLiteral("title")).toString(),
                 QStringLiteral("Untitled document"));

        QSignalSpy resolved(&office, &OfficeApplication::recoveryResolved);
        office.resolveRecovery({QStringLiteral("untitled-9-1")}, {QStringLiteral("untitled-9-3")});
        QCOMPARE(resolved.count(), 1);
        QVERIFY(resolved.first().first().toBool());
        QCOMPARE(office.windowCount(), 1);
        // The recovered entry stays until that window saves or discards;
        // the dismissed one is gone for good (moved aside).
        QCOMPARE(journal.pendingKeys(), QStringList{QStringLiteral("untitled-9-1")});
        QVERIFY(QFile::exists(journal.discardedDir() + QStringLiteral("/untitled-9-3.json")));
        QVERIFY(office.pendingRecovery().size() == 1);
    }

    void recoverSelectedOpensTheTickedAndDiscardsTheRest()
    {
        QQmlEngine engine;
        OfficeProbe probe;
        QQuickWindow *window = makeDialog(engine, &probe);
        QVERIFY(window != nullptr);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QQuickItem *checkB = findNamed(window->contentItem(), QStringLiteral("recoveryCheck_b"));
        QQuickItem *recover = findNamed(window->contentItem(), QStringLiteral("recoveryRecover"));
        QVERIFY(checkB != nullptr && recover != nullptr);
        click(window, checkB); // untick letter.odt
        QTRY_COMPARE(recover->property("text").toString(), QStringLiteral("Recover Selected"));
        click(window, recover);
        QTRY_COMPARE(probe.calls, 1);
        QCOMPARE(probe.recovered, QStringList{QStringLiteral("a")});
        QCOMPARE(probe.discarded, QStringList{QStringLiteral("b")});
        QTRY_VERIFY(!window->isVisible());
        delete window;
    }

    void closingTheDialogDiscardsEverything()
    {
        QQmlEngine engine;
        OfficeProbe probe;
        QQuickWindow *window = makeDialog(engine, &probe);
        QVERIFY(window != nullptr);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->close();
        QTRY_COMPARE(probe.calls, 1);
        QVERIFY(probe.recovered.isEmpty());
        QCOMPARE(probe.discarded, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        delete window;
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QTemporaryDir home;
    qputenv("XDG_CONFIG_HOME", (home.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_STATE_HOME", (home.path() + QStringLiteral("/state")).toUtf8());
    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaRecoveryTest"));
    RecoveryFlowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_recovery_flow.moc"
