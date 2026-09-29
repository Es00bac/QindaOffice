// SPDX-License-Identifier: GPL-3.0-or-later
#include "agent_host.h"
#include "agent_server.h"
#include "calc_test_support.h"
#include "assistant_controller.h"
#include "document_tools.h"
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
using namespace CalcTest;
class CalcAgentTest : public QObject {
  Q_OBJECT
private slots:
  void visibleStateAndNativeGrantControls() {
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(QTest::qWaitForWindowExposed(scene.window));
    QTRY_VERIFY(scene.grid->width() > 0 && scene.grid->height() > 0);
    auto *provider = scene.host.agentProvider();
    QCOMPARE(scene.host.agentAccess(), 0);
    auto *read = scene.host.action(QStringLiteral("agentRead"));
    auto *edit = scene.host.action(QStringLiteral("agentEdit"));
    auto *off = scene.host.action(QStringLiteral("agentDisconnect"));
    QVERIFY(off->isChecked() && read->isEnabled() && edit->isEnabled());
    scene.host.actions()->trigger(QStringLiteral("agentRead"));
    QCOMPARE(scene.host.agentAccess(), 1);
    QVERIFY(read->isChecked() && !off->isChecked() && !edit->isChecked());
    scene.host.actions()->trigger(QStringLiteral("agentEdit"));
    QCOMPARE(scene.host.agentAccess(), 2);
    scene.host.actions()->trigger(QStringLiteral("agentDisconnect"));
    QCOMPARE(scene.host.agentAccess(), 0);
    QList<QPair<CalcEng::CellAddr, QString>> inputs;
    for (int row = 0; row < 4001; ++row) inputs.append({{row, 0}, QStringLiteral("row %1").arg(row + 1)});
    scene.doc->applyInputs(inputs, QStringLiteral("Large sheet"));
    scene.grid->refreshAll();
    auto state = provider->state();
    QVERIFY(state[QStringLiteral("truncated")].toBool());
    QCOMPARE(state[QStringLiteral("omitted_cells")].toInt(), 1001);
    const auto revision = provider->revision();
    const auto generation = provider->generation();
    scene.grid->setScrollY(75000);
    state = provider->state();
    const auto rows = state[QStringLiteral("view")].toObject()[QStringLiteral("visible_rows")].toArray();
    QVERIFY(!rows.isEmpty());
    const auto address = QStringLiteral("A%1").arg(rows.first().toInt());
    QVERIFY(state[QStringLiteral("cells")].toObject()[address].toObject()[QStringLiteral("on_screen")].toBool());
    QVERIFY(provider->revision() != revision);
    QCOMPARE(provider->generation(), generation);
  }
  void budgetOverSocketEditRecalculateAndUndo() {
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    // Revision includes the actual painted viewport. Let the initial layout
    // and focus settle before taking a snapshot for an external client.
    QVERIFY(QTest::qWaitForWindowExposed(scene.window));
    QTRY_VERIFY(scene.grid->width() > 0 && scene.grid->height() > 0);
    QTest::qWait(50);
    scene.doc->applyInputs({{{4, 1}, QStringLiteral("1")},
                            {{4, 2}, QStringLiteral("2250")},
                            {{4, 3}, QStringLiteral("=B5*C5")},
                            {{5, 3}, QStringLiteral("5382")},
                            {{10, 3}, QStringLiteral("=SUM(D5:D10)")}},
                           QStringLiteral("Sample budget"));
    auto *provider = scene.host.agentProvider();
    QVERIFY(provider);
    QindaTK::AgentHost host;
    QVERIFY(host.addProvider(provider, QindaTK::AgentHost::Grant::Edit));
    QTemporaryDir dir;
    QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::WriteOwner |
                                          QFile::ExeOwner);
    QindaTK::AgentServer server(&host);
    QString error;
    const auto endpoint = dir.filePath(QStringLiteral("calc.sock"));
    QVERIFY2(server.listen(endpoint, &error), qPrintable(error));
    const auto invoke = [&](const QString &action, const QByteArray &args,
                            const QString &revision, const QString &id) {
      QProcess process;
      process.start(QStringLiteral("python3"),
                    {QStringLiteral(QQO_AGENT_CLI), QStringLiteral("--socket"),
                     endpoint, QStringLiteral("invoke"), provider->id(), action,
                     QStringLiteral("--args"), QString::fromUtf8(args),
                     QStringLiteral("--if-revision"), revision,
                     QStringLiteral("--request-id"), id});
      QElapsedTimer timer;
      timer.start();
      while (process.state() != QProcess::NotRunning &&
             timer.elapsed() < 20000) {
        QCoreApplication::processEvents();
        QTest::qWait(5);
      }
      return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    };
    const int before = scene.doc->undoStack()->index();
    const auto revision = provider->revision();
    const auto receipt =
        invoke(QStringLiteral("calc.set_cells"), R"({"cells":{"B5":"2"}})",
               revision, QStringLiteral("lighting-42"));
    QCOMPARE(receipt.value(QStringLiteral("status")).toString(),
             QStringLiteral("completed"));
    QCOMPARE(scene.doc->displayTextAt({10, 3}), QStringLiteral("9882"));
    QCOMPARE(scene.doc->undoStack()->index(), before + 1);
    QCOMPARE(receipt.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("cells"))
                 .toObject()
                 .value(QStringLiteral("D11"))
                 .toObject()
                 .value(QStringLiteral("value"))
                 .toDouble(),
             9882.0);
    const auto replay =
        invoke(QStringLiteral("calc.set_cells"), R"({"cells":{"B5":"2"}})",
               revision, QStringLiteral("lighting-42"));
    QVERIFY(replay.value(QStringLiteral("replayed")).toBool());
    QCOMPARE(scene.doc->undoStack()->index(), before + 1);
    const auto undoArgs =
        QJsonDocument(QJsonObject{{QStringLiteral("undo_ref"),
                                   receipt.value(QStringLiteral("undo_ref"))}})
            .toJson(QJsonDocument::Compact);
    const auto undone =
        invoke(QStringLiteral("calc.undo"), undoArgs, provider->revision(),
               QStringLiteral("undo-lighting"));
    QCOMPARE(undone.value(QStringLiteral("status")).toString(),
             QStringLiteral("undone"));
    QCOMPARE(scene.doc->displayTextAt({10, 3}), QStringLiteral("7632"));
    QCOMPARE(scene.doc->undoStack()->index(), before);
  }
  void humanRevisionValidationAndDraft() {
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    auto *provider = scene.host.agentProvider();
    QindaTK::AgentHost host;
    host.addProvider(provider, QindaTK::AgentHost::Grant::Edit);
    auto request = QJsonObject{
        {QStringLiteral("document_id"), provider->id()},
        {QStringLiteral("action"), QStringLiteral("calc.set_cells")},
        {QStringLiteral("request_id"), QStringLiteral("old")},
        {QStringLiteral("if_revision"), provider->revision()},
        {QStringLiteral("arguments"),
         QJsonObject{
             {QStringLiteral("cells"),
              QJsonObject{{QStringLiteral("B5"), QStringLiteral("2")}}}}}};
    scene.doc->applyInput({4, 1}, QStringLiteral("3"));
    QCOMPARE(host.request(QStringLiteral("action.invoke"), request)
                 .value(QStringLiteral("status"))
                 .toString(),
             QStringLiteral("stale_revision"));
    QCOMPARE(scene.doc->inputTextAt({4, 1}), QStringLiteral("3"));
    CalcEng::ValidationRule rule;
    rule.range = {{4, 1}, {4, 1}};
    rule.kind = CalcEng::ValidationRule::Kind::WholeNumber;
    rule.min = 1;
    rule.max = 10;
    scene.doc->setValidations({rule});
    request[QStringLiteral("if_revision")] = provider->revision();
    request[QStringLiteral("request_id")] = QStringLiteral("bad");
    request[QStringLiteral("arguments")] = QJsonObject{
        {QStringLiteral("cells"),
         QJsonObject{{QStringLiteral("B5"), QStringLiteral("-2")},
                     {QStringLiteral("C5"), QStringLiteral("999")}}}};
    const int before = scene.doc->undoStack()->index();
    QCOMPARE(host.request(QStringLiteral("action.invoke"), request)
                 .value(QStringLiteral("status"))
                 .toString(),
             QStringLiteral("validation_failed"));
    QVERIFY(scene.doc->inputTextAt({4, 2}).isEmpty());
    QCOMPARE(scene.doc->undoStack()->index(), before);
    scene.grid->startEditor();
    request[QStringLiteral("if_revision")] = provider->revision();
    request[QStringLiteral("request_id")] = QStringLiteral("draft");
    request[QStringLiteral("arguments")] =
        QJsonObject{{QStringLiteral("cells"),
                     QJsonObject{{QStringLiteral("B5"), QStringLiteral("2")}}}};
    QCOMPARE(host.request(QStringLiteral("action.invoke"), request)
                 .value(QStringLiteral("status"))
                 .toString(),
             QStringLiteral("disabled"));
    scene.grid->cancelEditor();
    auto *formula = scene.item(QStringLiteral("formulaField"));
    QVERIFY(formula);
    formula->setProperty("text", QStringLiteral("unfinished formula draft"));
    scene.grid->forceActiveFocus();
    QVERIFY(!provider->state()[QStringLiteral("draft")].isNull());
    const auto outcome = scene.host.assistant()->documentTools()->apply(
        QJsonArray{QJsonObject{{QStringLiteral("op"),QStringLiteral("set_cells")},
            {QStringLiteral("cells"),QJsonObject{{QStringLiteral("B5"),QStringLiteral("2")}}}}});
    QCOMPARE(outcome.applied, 0);
    QCOMPARE(scene.doc->inputTextAt({4, 1}), QStringLiteral("3"));
  }
};
CALC_TEST_MAIN(CalcAgentTest)
#include "tst_calc_agent.moc"
