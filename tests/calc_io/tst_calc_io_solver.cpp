// SPDX-License-Identifier: GPL-3.0-or-later
// M90 (D-098): Excel's Solver model through a save. Excel's Solver add-in
// keeps its model as hidden names scoped to the model's sheet (solver_opt,
// solver_typ, solver_adj, solver_lhsN / relN / rhsN, solver_eng, the
// options). A package built the way Excel 365 writes them opens with the
// model in QindaCalc's hands (and it solves); a save gives every name back
// hidden and scoped, schema-valid, and the model re-reads equal; a model
// made in QindaCalc is written as Excel's complete set.

#include "xlsx_bridge.h"

#include "calc_io_schema.h"
#include "carry_inspect.h"
#include "excel_inspect.h"
#include "excel_package.h"

#include "solver.h"
#include "workbook.h"

#include <QFile>
#include <QTemporaryDir>

#include <QtTest>

#include <algorithm>

using namespace QindaOffice::Calc;
using namespace QindaOffice::Test;
using QindaOffice::Ooxml::Package;

namespace {

QByteArray cell(const char *ref, const char *value)
{
    return QByteArrayLiteral("<c r=\"") + ref + QByteArrayLiteral("\"><v>") + value + QByteArrayLiteral("</v></c>");
}

QByteArray formula(const char *ref, const char *f, const char *value)
{
    return QByteArrayLiteral("<c r=\"") + ref + QByteArrayLiteral("\"><f>") + f + QByteArrayLiteral("</f><v>") + value
        + QByteArrayLiteral("</v></c>");
}

// The product mix of tst_calc_solver: maximize E2 = 3·B2 + 5·C2 with
// D4:D6 <= E4:E6, B2:C2 integers, as Excel 365 saves it after Solve.
ExcelWorkbook excelModel()
{
    ExcelWorkbook book;
    const QByteArray data = QByteArrayLiteral("<row r=\"2\">") + cell("B2", "0") + cell("C2", "0")
        + formula("E2", "3*B2+5*C2", "0") + QByteArrayLiteral("</row><row r=\"4\">") + formula("D4", "B2", "0")
        + cell("E4", "4") + QByteArrayLiteral("</row><row r=\"5\">") + formula("D5", "2*C2", "0") + cell("E5", "12")
        + QByteArrayLiteral("</row><row r=\"6\">") + formula("D6", "3*B2+2*C2", "0") + cell("E6", "18")
        + QByteArrayLiteral("</row>");
    book.sheets = {ExcelSheet(QStringLiteral("Model"), data), ExcelSheet(QStringLiteral("Notes"), QByteArray())};
    const char *names[][2] = {
        {"solver_adj", "Model!$B$2:$C$2"}, {"solver_cvg", "0.0001"}, {"solver_drv", "1"}, {"solver_eng", "2"},
        {"solver_est", "1"}, {"solver_itr", "2147483647"}, {"solver_lhs1", "Model!$B$2:$C$2"},
        {"solver_lhs2", "Model!$D$4:$D$6"}, {"solver_mip", "2147483647"}, {"solver_mni", "30"}, {"solver_mrt", "0.075"},
        {"solver_msl", "2"}, {"solver_neg", "1"}, {"solver_nod", "2147483647"}, {"solver_num", "2"}, {"solver_nwt", "1"},
        {"solver_opt", "Model!$E$2"}, {"solver_pre", "0.000001"}, {"solver_rbv", "1"}, {"solver_rel1", "4"},
        {"solver_rel2", "1"}, {"solver_rhs1", "integer"}, {"solver_rhs2", "Model!$E$4:$E$6"}, {"solver_rlx", "2"},
        {"solver_rsd", "0"}, {"solver_scl", "1"}, {"solver_sho", "2"}, {"solver_ssz", "100"}, {"solver_tim", "2147483647"},
        {"solver_tol", "0.01"}, {"solver_typ", "1"}, {"solver_val", "0"}, {"solver_ver", "3"}};
    for (const auto &[name, body] : names) {
        book.definedNames += QByteArrayLiteral("<definedName name=\"") + name
            + QByteArrayLiteral("\" localSheetId=\"0\" hidden=\"1\">") + body + QByteArrayLiteral("</definedName>");
    }
    // A workbook-wide name of the user's own, to show the two kinds apart.
    book.definedNames += QByteArrayLiteral("<definedName name=\"Hours\">Model!$D$4:$D$6</definedName>");
    return book;
}

SolverModel expectedModel()
{
    SolverModel model;
    model.objective = QStringLiteral("Model!$E$2");
    model.goal = SolverGoal::Max;
    model.variables = QStringLiteral("Model!$B$2:$C$2");
    model.constraints = {{QStringLiteral("Model!$B$2:$C$2"), SolverRelation::Integer, QString()},
                         {QStringLiteral("Model!$D$4:$D$6"), SolverRelation::LessEqual, QStringLiteral("Model!$E$4:$E$6")}};
    model.method = SolverMethod::SimplexLp;
    return model;
}

QHash<QString, SavedName> solverNames(const Package &package)
{
    QHash<QString, SavedName> out;
    for (const SavedName &name : savedDefinedNames(package)) {
        if (name.name.startsWith(QLatin1String("solver_"))) {
            out.insert(name.name, name);
        }
    }
    return out;
}

} // namespace

class CalcIoSolverTest : public QObject {
    Q_OBJECT
private slots:
    void excelModelOpensWithItsSettings();
    void excelModelSurvivesASave();
    void modelMadeHereIsExcels();

private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }
    bool open(const QByteArray &bytes, const QString &name, Workbook *wb)
    {
        QFile file(path(name));
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write(bytes);
        file.close();
        const QString schema = xlsxSchemaErrors(path(name));
        if (!schema.isEmpty()) {
            qWarning("package %s is not schema-valid: %s", qPrintable(name), qPrintable(schema));
            return false;
        }
        QString error;
        return IO::readXlsxFile(path(name), wb, &error);
    }
    std::shared_ptr<const Package> save(const Workbook &wb, const QString &name)
    {
        QString error;
        if (!IO::writeXlsxFile(wb, path(name), &error)) {
            qWarning("write failed: %s", qPrintable(error));
            return nullptr;
        }
        const QString schema = xlsxSchemaErrors(path(name));
        if (!schema.isEmpty()) {
            qWarning("saved package is not schema-valid: %s", qPrintable(schema));
            return nullptr;
        }
        keepForInspection(path(name));
        return Package::load(path(name), &error);
    }
};

void CalcIoSolverTest::excelModelOpensWithItsSettings()
{
    Workbook wb;
    QVERIFY(open(excelModel().build(), QStringLiteral("model.xlsx"), &wb));
    const std::optional<SolverModel> model = solverModelOf(wb, 0);
    QVERIFY(model.has_value());
    QCOMPARE(*model, expectedModel());
    QVERIFY(!solverModelOf(wb, 1).has_value()); // the Notes sheet has none
    for (const DefinedName &name : wb.definedNames()) {
        if (name.name.startsWith(QLatin1String("solver_"))) {
            QVERIFY(name.hidden);
            QCOMPARE(name.sheet, 0);
        }
    }
    QVERIFY(wb.namedRange(QStringLiteral("Hours")).has_value());
    // And it solves: the integer product mix's optimum is the LP's (2, 6).
    const SolverOutcome out = solveModel(wb, 0, *model);
    QVERIFY2(solverStatusFeasible(out.status), qPrintable(out.message));
    QCOMPARE(toNumber(wb.cellValue(0, 1, 4)).value_or(-1), 36.0);
}

void CalcIoSolverTest::excelModelSurvivesASave()
{
    Workbook wb;
    QVERIFY(open(excelModel().build(), QStringLiteral("model.xlsx"), &wb));
    const auto package = save(wb, QStringLiteral("model-saved.xlsx"));
    QVERIFY(package);
    const QHash<QString, SavedName> names = solverNames(*package);
    QCOMPARE(names.size(), 33);
    for (const SavedName &name : names) {
        QCOMPARE(name.localSheetId, QStringLiteral("0"));
        QCOMPARE(name.hidden, QStringLiteral("1"));
    }
    QCOMPARE(names.value(QStringLiteral("solver_opt")).body, QStringLiteral("Model!$E$2"));
    QCOMPARE(names.value(QStringLiteral("solver_adj")).body, QStringLiteral("Model!$B$2:$C$2"));
    QCOMPARE(names.value(QStringLiteral("solver_rhs1")).body, QStringLiteral("integer"));
    QCOMPARE(names.value(QStringLiteral("solver_rhs2")).body, QStringLiteral("Model!$E$4:$E$6"));
    QCOMPARE(names.value(QStringLiteral("solver_pre")).body, QStringLiteral("0.000001"));
    QFile file(path(QStringLiteral("model-saved.xlsx")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    Workbook again;
    QVERIFY(open(file.readAll(), QStringLiteral("model-again.xlsx"), &again));
    QCOMPARE(*solverModelOf(again, 0), expectedModel());
}

void CalcIoSolverTest::modelMadeHereIsExcels()
{
    Workbook wb;
    QVERIFY(open(excelModel().build(), QStringLiteral("model.xlsx"), &wb));
    // A new model on the second sheet: GRG Nonlinear, Min, Value Of options,
    // a bin and a formula right side.
    wb.setCellInput(1, {0, 0}, QStringLiteral("1"));
    wb.setCellInput(1, {0, 1}, QStringLiteral("=A1^2"));
    SolverModel model;
    model.objective = QStringLiteral("Notes!$B$1");
    model.goal = SolverGoal::ValueOf;
    model.valueOf = 2.5;
    model.variables = QStringLiteral("Notes!$A$1,Notes!$C$1");
    model.constraints = {{QStringLiteral("Notes!$C$1"), SolverRelation::Binary, QString()},
                         {QStringLiteral("Notes!$A$1"), SolverRelation::LessEqual, QStringLiteral("SUM(Notes!$C$1:$C$2)+3")}};
    model.nonNegative = false;
    model.options.maxTime = 60;
    model.options.convergence = 0.00001;
    model.options.multistart = true;
    setSolverModel(wb, 1, model);
    const auto package = save(wb, QStringLiteral("made.xlsx"));
    QVERIFY(package);
    QHash<QString, SavedName> notes;
    for (const SavedName &name : savedDefinedNames(*package)) {
        if (name.name.startsWith(QLatin1String("solver_")) && name.localSheetId == QLatin1String("1")) {
            QCOMPARE(name.hidden, QStringLiteral("1"));
            notes.insert(name.name, name);
        }
    }
    QCOMPARE(notes.size(), 33);
    QCOMPARE(notes.value(QStringLiteral("solver_typ")).body, QStringLiteral("3"));
    QCOMPARE(notes.value(QStringLiteral("solver_val")).body, QStringLiteral("2.5"));
    QCOMPARE(notes.value(QStringLiteral("solver_adj")).body, QStringLiteral("Notes!$A$1,Notes!$C$1"));
    QCOMPARE(notes.value(QStringLiteral("solver_rel1")).body, QStringLiteral("5"));
    QCOMPARE(notes.value(QStringLiteral("solver_rhs1")).body, QStringLiteral("binary"));
    QCOMPARE(notes.value(QStringLiteral("solver_rhs2")).body, QStringLiteral("SUM(Notes!$C$1:$C$2)+3"));
    QCOMPARE(notes.value(QStringLiteral("solver_neg")).body, QStringLiteral("2"));
    QCOMPARE(notes.value(QStringLiteral("solver_eng")).body, QStringLiteral("1"));
    QCOMPARE(notes.value(QStringLiteral("solver_tim")).body, QStringLiteral("60"));
    QCOMPARE(notes.value(QStringLiteral("solver_cvg")).body, QStringLiteral("0.00001"));
    QCOMPARE(notes.value(QStringLiteral("solver_msl")).body, QStringLiteral("1"));
    QCOMPARE(notes.value(QStringLiteral("solver_ver")).body, QStringLiteral("3"));
    // Excel's own model on the first sheet is still there, untouched.
    const QList<SavedName> all = savedDefinedNames(*package);
    QCOMPARE(std::count_if(all.begin(), all.end(), [](const SavedName &name) { return name.name.startsWith(QLatin1String("solver_")); }),
             66);
    QFile file(path(QStringLiteral("made.xlsx")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    Workbook again;
    QVERIFY(open(file.readAll(), QStringLiteral("made-again.xlsx"), &again));
    QCOMPARE(*solverModelOf(again, 1), model);
    QCOMPARE(*solverModelOf(again, 0), expectedModel());
}

QTEST_GUILESS_MAIN(CalcIoSolverTest)
#include "tst_calc_io_solver.moc"
