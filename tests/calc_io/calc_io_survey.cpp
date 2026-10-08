// SPDX-License-Identifier: GPL-3.0-or-later
// calc_io_survey — opens real .xlsx files through the calc_io reader and
// prints AGGREGATE counts only: how many files failed to load, how many
// cells hold #NAME?, how many text cells read as numbers, how many formulas
// arrived as text or could not be parsed. Nothing of a workbook's content
// (no cell text, no sheet or file names) is printed, so it can be pointed at
// private documents (M61, D-069: the before/after measurement).
//
//   calc_io_survey <file-with-one-path-per-line>
//
// AGENT-NOTE: keep it to the bridge API that existed before M61
// (readXlsxFile, Workbook, Sheet::cells) so the same source builds against an
// older calc_io for a "before" figure; the per-function breakdown of #NAME?
// cells needs M61's formula table and is compiled in when it exists.

#include "xlsx_bridge.h"

#if __has_include("xlsx_formula.h")
#include "functions/functions.h"
#include "lexer.h"
#include "xlsx_formula.h"
#define QQO_SURVEY_FUNCTIONS 1
#endif

#include <QCoreApplication>
#include <QFile>
#include <QMap>
#include <QTextStream>

#include <cstdio>

using namespace QindaOffice::Calc;

namespace {

struct Counts {
    int files = 0;
    int failed = 0;
    qint64 sheets = 0;
    qint64 cells = 0;
    qint64 formulas = 0;
    qint64 errors = 0;
    qint64 nameErrors = 0;
    qint64 textNumbers = 0;
    qint64 formulasAsText = 0;
    qint64 unparsedFormulas = 0;
    QMap<QString, qint64> missingFunctions; // documented Excel functions only
};

#ifdef QQO_SURVEY_FUNCTIONS
// Function names a #NAME? formula calls that the engine lacks — only names
// in Microsoft's documented list are kept, so nothing user-defined leaks.
void countMissing(const QString &formula, Counts *counts)
{
    const QVector<Token> tokens = lexFormula(formula);
    for (qsizetype i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens.at(i).type != Token::Type::Identifier || tokens.at(i + 1).text != QLatin1String("(")) {
            continue;
        }
        QString name = tokens.at(i).text.toUpper();
        for (const char *prefix : {"_XLFN._XLWS.", "_XLFN."}) {
            if (name.startsWith(QLatin1String(prefix))) {
                name = name.mid(int(qstrlen(prefix)));
            }
        }
        if (!functionRegistry().contains(name) && name != QLatin1String("LET")
            && !IO::futureFunctionPrefix(name).isEmpty()) {
            ++counts->missingFunctions[name];
        }
    }
}
#endif

void survey(const QString &path, Counts *counts)
{
    ++counts->files;
    Workbook wb;
    QString error;
    if (!IO::readXlsxFile(path, &wb, &error)) {
        ++counts->failed;
        return;
    }
    counts->sheets += wb.sheetCount();
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        for (const Cell &cell : wb.sheetAt(s).cells()) {
            if (isBlank(cell.value) && !cell.hasFormula()) {
                continue;
            }
            ++counts->cells;
            if (cell.hasFormula()) {
                ++counts->formulas;
                const ParseResult parsed = parseFormula(cell.formula);
                if (!parsed.error.isEmpty() || !parsed.root) {
                    ++counts->unparsedFormulas;
                }
            }
            if (isErr(cell.value)) {
                ++counts->errors;
                if (std::get<Err>(cell.value) == Err::Name) {
                    ++counts->nameErrors;
#ifdef QQO_SURVEY_FUNCTIONS
                    if (cell.hasFormula()) {
                        countMissing(cell.formula, counts);
                    }
#endif
                }
            }
            if (const auto *text = std::get_if<QString>(&cell.value)) {
                bool number = false;
                static_cast<void>(text->trimmed().toDouble(&number));
                if (number) {
                    ++counts->textNumbers;
                }
                if (!cell.hasFormula() && text->startsWith(QLatin1Char('='))) {
                    ++counts->formulasAsText;
                }
            }
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: calc_io_survey <file listing one .xlsx path per line>\n");
        return 2;
    }
    QFile list(QString::fromLocal8Bit(argv[1]));
    if (!list.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "cannot read the path list\n");
        return 2;
    }
    // Quiet: QXlsx and the reader may log per-file details.
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
    Counts counts;
    QTextStream in(&list);
    while (!in.atEnd()) {
        const QString path = in.readLine().trimmed();
        if (!path.isEmpty()) {
            survey(path, &counts);
        }
    }
    std::printf("files %d\nload failures %d\nsheets %lld\nnon-blank cells %lld\nformula cells %lld\n"
                "error cells %lld\n#NAME? cells %lld\ntext cells that read as numbers %lld\n"
                "formulas loaded as text %lld\nformulas the engine cannot parse %lld\n",
                counts.files, counts.failed, static_cast<long long>(counts.sheets),
                static_cast<long long>(counts.cells), static_cast<long long>(counts.formulas),
                static_cast<long long>(counts.errors), static_cast<long long>(counts.nameErrors),
                static_cast<long long>(counts.textNumbers), static_cast<long long>(counts.formulasAsText),
                static_cast<long long>(counts.unparsedFormulas));
    for (auto it = counts.missingFunctions.constBegin(); it != counts.missingFunctions.constEnd(); ++it) {
        std::printf("missing function %s: %lld formula cells\n", qPrintable(it.key()),
                    static_cast<long long>(it.value()));
    }
    return 0;
}
