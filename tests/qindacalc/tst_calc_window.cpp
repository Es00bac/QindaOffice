// SPDX-License-Identifier: GPL-3.0-or-later
// QindaCalc's document and host: a workbook edits, saves and reloads
// through the document; undo back to the saved state clears dirty; the
// recovery payload round-trips the whole workbook; ods goes through the
// LibreOffice bridge; a fresh host is pristine so Open can reuse it.

#include "calc_test_support.h"

#include "libre_office_converter.h"
#include "standard_actions.h"

#include "sheet.h"


#include <QFileInfo>
#include <QUndoStack>

using namespace CalcTest;
using namespace QindaOffice::Apps::QindaCalc;
namespace ActionId = QindaOffice::Quick::ActionId;
namespace Calc = QindaOffice::Calc;

class CalcWindowTest final : public QObject {
    Q_OBJECT
private slots:
    void constructAndEdit()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        SpreadsheetDocument *doc = scene.doc;
        doc->applyInput({0, 0}, QStringLiteral("41"));
        doc->applyInput({1, 0}, QStringLiteral("=A1*2"));
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 1, 0)), QStringLiteral("82"));
        QVERIFY(doc->isDirty());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/t.xlsx");
        QString error;
        QVERIFY2(doc->save(path, &error), qPrintable(error));
        SpreadsheetDocument reloaded;
        QVERIFY2(reloaded.load(path, &error), qPrintable(error));
        QCOMPARE(Calc::toText(reloaded.workbook()->cellValue(0, 1, 0)), QStringLiteral("82"));
    }

    // Dirty tracks the undo stack's clean index: Ctrl+Z back to the saved
    // state must not ask "Save changes?" on close.
    void undoBackToTheSavedStateClearsDirty()
    {
        CalcHost host;
        SpreadsheetDocument *doc = host.spreadsheetDocument();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/clean.xlsx");
        doc->applyInput({0, 0}, QStringLiteral("1"));
        QVERIFY(doc->save(path));
        QVERIFY(!doc->isDirty());
        doc->applyInput({0, 0}, QStringLiteral("2"));
        QVERIFY(doc->isDirty());
        doc->undoStack()->undo();
        QVERIFY(!doc->isDirty());
        doc->undoStack()->redo();
        QVERIFY(doc->isDirty());
    }

    void saveCommandsAreLive()
    {
        CalcHost host;
        for (const char *id : {ActionId::kFileSave, ActionId::kFileSaveAs}) {
            QVERIFY2(host.actions()->entry(QString::fromLatin1(id))->isEnabled(), id);
        }
        QCOMPARE(host.actions()->entry(QString::fromLatin1(ActionId::kFileSave))->shortcut(),
                 QKeySequence(QKeySequence::Save));
        QVERIFY(host.actions()->entry(QString::fromLatin1(ActionId::kFilePrint))->isEnabled());
    }

    void recoverySnapshotRoundTripsTheWholeWorkbook()
    {
        SpreadsheetDocument doc;
        doc.applyInput({0, 0}, QStringLiteral("10"));
        doc.insertSheet(QStringLiteral("Second"));
        doc.setActiveSheet(1);
        doc.applyInput({0, 0}, QStringLiteral("=Sheet1!A1*3"));
        StylePatch patch;
        patch.bold = true;
        doc.restyleRange({{0, 0}, {0, 0}}, patch, QStringLiteral("Bold"));
        Calc::RangeRef total;
        total.sheet = QStringLiteral("Sheet1");
        total.range = {{0, 0}, {0, 0}};
        doc.defineName(QStringLiteral("TOTAL"), total);
        Calc::ChartSpec chart;
        chart.title = QStringLiteral("t");
        chart.rangeText = QStringLiteral("A1:A1");
        doc.addChart(chart);
        const QByteArray payload = doc.recoverySnapshot();
        SpreadsheetDocument restored;
        QString error;
        QVERIFY2(restored.loadRecovery(payload, &error), qPrintable(error));
        QCOMPARE(restored.workbook()->sheetCount(), 2);
        QCOMPARE(Calc::toText(restored.workbook()->cellValue(1, 0, 0)), QStringLiteral("30"));
        QVERIFY(restored.workbook()->style(restored.workbook()->sheetAt(1).styleIdAt({0, 0}))->bold);
        QVERIFY(restored.workbook()->namedRange(QStringLiteral("TOTAL")).has_value());
        QCOMPARE(restored.workbook()->charts.size(), 1);
    }

    void odsGoesThroughTheLibreOfficeBridge()
    {
        if (!QindaOffice::Core::LibreOfficeConverter::probe()) {
            QSKIP("LibreOffice is not installed");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SpreadsheetDocument doc;
        doc.applyInput({0, 0}, QStringLiteral("7"));
        const QString path = dir.path() + QStringLiteral("/bridge.ods");
        QString error;
        QVERIFY2(doc.save(path, &error), qPrintable(error));
        SpreadsheetDocument back;
        QVERIFY2(back.load(path, &error), qPrintable(error));
        QCOMPARE(Calc::toText(back.workbook()->cellValue(0, 0, 0)), QStringLiteral("7"));
    }

    // A real-world Excel workbook (absolute part names, no <dimension>,
    // tables, drawings, charts): set QQO_SAMPLE_XLSX to a path and the test
    // loads it, prints one line per sheet (cell count, error cells with up
    // to six samples) and requires every sheet to arrive.
    void opensARealExcelWorkbook()
    {
        const QString path = qEnvironmentVariable("QQO_SAMPLE_XLSX");
        if (path.isEmpty()) {
            QSKIP("QQO_SAMPLE_XLSX not set");
        }
        SpreadsheetDocument doc;
        QString error;
        const bool ok = doc.load(path, &error);
        QVERIFY2(ok, qPrintable(error));
        for (qint32 s = 0; s < doc.workbook()->sheetCount(); ++s) {
            const Calc::Sheet &sheet = doc.workbook()->sheetAt(s);
            int errors = 0;
            QStringList samples;
            for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
                const Calc::CellAddr addr{qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)};
                const QString shown = doc.displayTextAt(s, addr);
                if (!shown.startsWith(QLatin1Char('#'))) {
                    continue;
                }
                ++errors;
                if (samples.size() < 6) {
                    samples.append(Calc::addrText(addr) + QLatin1Char('=') + doc.inputTextAt(addr).left(60)
                                   + QStringLiteral(" -> ") + shown);
                }
            }
            qInfo() << "sheet" << s << doc.workbook()->sheetName(s) << "cells" << sheet.cells().size()
                    << "errors" << errors;
            for (const QString &sample : samples) {
                qInfo() << "   " << sample;
            }
        }
        QVERIFY(doc.workbook()->sheetCount() >= 2);
    }

    // AGENT-CONTRACT (ADR-0110): a freshly created host is PRISTINE, which
    // lets File ▸ Open replace its blank workbook instead of opening a
    // second window and leaving the blank one behind.
    void freshHostIsPristineSoOpenCanReuseIt()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(!scene.doc->isDirty());
        QVERIFY(scene.host.isPristineUntitled());
        QCOMPARE(scene.host.sheetNames(), QStringList{QStringLiteral("Sheet1")});
    }
};

CALC_TEST_MAIN(CalcWindowTest)

#include "tst_calc_window.moc"
