// SPDX-License-Identifier: GPL-3.0-or-later
// Formula text transforms between Excel's file spelling and the engine's
// (xlsx_formula.h, M61, D-069).

#include "xlsx_formula.h"

#include "xlsx_formula_tokens.h"

#include "functions/functions.h"
#include "lexer.h"
#include "parser.h"

#include <QHash>
#include <QList>
#include <QSet>

namespace QindaOffice::Calc::IO {

namespace {

const QString kXlfn = QStringLiteral("_xlfn.");
const QString kXlws = QStringLiteral("_xlfn._xlws.");
const QString kXlpm = QStringLiteral("_xlpm.");

// AGENT-NOTE: Microsoft's list of "future functions" — every worksheet
// function added after Excel 2007 is stored with _xlfn., FILTER and SORT
// with _xlfn._xlws. (support.microsoft.com, "Issue: An _xlfn. prefix is
// displayed in front of a formula"; the same list XlsxWriter documents).
// Adding a name here is safe for round trips (read strips exactly what
// write adds back); leaving one out only costs a user-typed formula its
// prefix, and an unknown prefixed function in a file is kept verbatim.
const QHash<QString, QString> &prefixTable()
{
    static const QHash<QString, QString> table = [] {
        QHash<QString, QString> t;
        const char *const kFuture[] = {
            "ACOT", "ACOTH", "AGGREGATE", "ANCHORARRAY", "ARABIC", "ARRAYTOTEXT", "BASE", "BETA.DIST",
            "BETA.INV", "BINOM.DIST", "BINOM.DIST.RANGE", "BINOM.INV", "BITAND", "BITLSHIFT", "BITOR",
            "BITRSHIFT", "BITXOR", "BYCOL", "BYROW", "CEILING.MATH", "CEILING.PRECISE", "CHISQ.DIST",
            "CHISQ.DIST.RT", "CHISQ.INV", "CHISQ.INV.RT", "CHISQ.TEST", "CHOOSECOLS", "CHOOSEROWS", "COMBINA",
            "CONCAT", "CONFIDENCE.NORM", "CONFIDENCE.T", "COT", "COTH", "COVARIANCE.P", "COVARIANCE.S", "CSC",
            "CSCH", "DAYS", "DECIMAL", "DROP", "ECMA.CEILING", "ENCODEURL", "ERF.PRECISE", "ERFC.PRECISE",
            "EXPAND", "EXPON.DIST", "F.DIST", "F.DIST.RT", "F.INV", "F.INV.RT", "F.TEST", "FIELDVALUE",
            "FILTERXML", "FLOOR.MATH", "FLOOR.PRECISE", "FORECAST.ETS", "FORECAST.ETS.CONFINT",
            "FORECAST.ETS.SEASONALITY", "FORECAST.ETS.STAT", "FORECAST.LINEAR", "FORMULATEXT", "GAMMA",
            "GAMMA.DIST", "GAMMA.INV", "GAMMALN.PRECISE", "GAUSS", "GROUPBY", "HSTACK", "HYPGEOM.DIST", "IFNA",
            "IFS", "IMAGE", "IMCOSH", "IMCOT", "IMCSC", "IMCSCH", "IMSEC", "IMSECH", "IMSINH", "IMTAN",
            "ISFORMULA", "ISO.CEILING", "ISOMITTED", "ISOWEEKNUM", "LAMBDA", "LET", "LOGNORM.DIST",
            "LOGNORM.INV", "MAKEARRAY", "MAP", "MAXIFS", "MINIFS", "MODE.MULT", "MODE.SNGL", "MUNIT",
            "NEGBINOM.DIST", "NETWORKDAYS.INTL", "NORM.DIST", "NORM.INV", "NORM.S.DIST", "NORM.S.INV",
            "NUMBERVALUE", "PDURATION", "PERCENTILE.EXC", "PERCENTILE.INC", "PERCENTOF", "PERCENTRANK.EXC",
            "PERCENTRANK.INC", "PERMUTATIONA", "PHI", "PIVOTBY", "POISSON.DIST", "QUARTILE.EXC",
            "QUARTILE.INC", "QUERYSTRING", "RANDARRAY", "RANK.AVG", "RANK.EQ", "REDUCE", "REGEXEXTRACT",
            "REGEXREPLACE", "REGEXTEST", "RRI", "SCAN", "SEC", "SECH", "SEQUENCE", "SHEET", "SHEETS", "SINGLE",
            "SKEW.P", "SORTBY", "STDEV.P", "STDEV.S", "STOCKHISTORY", "SWITCH", "T.DIST", "T.DIST.2T",
            "T.DIST.RT", "T.INV", "T.INV.2T", "T.TEST", "TAKE", "TEXTAFTER", "TEXTBEFORE", "TEXTJOIN",
            "TEXTSPLIT", "TOCOL", "TOROW", "TRIMRANGE", "UNICHAR", "UNICODE", "UNIQUE", "VALUETOTEXT", "VAR.P",
            "VAR.S", "VSTACK", "WEBSERVICE", "WEIBULL.DIST", "WORKDAY.INTL", "WRAPCOLS", "WRAPROWS", "XLOOKUP",
            "XMATCH", "XOR", "Z.TEST"};
        for (const char *name : kFuture) {
            t.insert(QString::fromLatin1(name), kXlfn);
        }
        t.insert(QStringLiteral("FILTER"), kXlws);
        t.insert(QStringLiteral("SORT"), kXlws);
        return t;
    }();
    return table;
}

bool isOp(const Token &token, QLatin1StringView op)
{
    return token.type == Token::Type::Op && token.text == op;
}

// Rebuilds `text` with some tokens replaced: every replacement covers
// [pos, pos + length) of the original; everything between is copied as is.
struct Splicer {
    const QString &text;
    QString out;
    qsizetype copied = 0;
    void replace(qsizetype pos, qsizetype length, const QString &with)
    {
        out += QStringView(text).mid(copied, pos - copied);
        out += with;
        copied = pos + length;
    }
    QString finish()
    {
        out += QStringView(text).mid(copied);
        return out;
    }
};

// `name` without a leading prefix (case-insensitive), or empty.
QString afterPrefix(const QString &name, const QString &prefix)
{
    return name.startsWith(prefix, Qt::CaseInsensitive) ? name.mid(prefix.size()) : QString();
}

// A LET or LAMBDA call being walked: its parameters are written _xlpm.name.
struct Scope {
    int depth = 0;     // parenthesis depth of the call's own arguments
    int argument = 0;  // index of the argument being read
    bool lambda = false;
    QSet<QString> parameters; // upper-case
};

} // namespace

QString futureFunctionPrefix(const QString &upperName)
{
    return prefixTable().value(upperName);
}

namespace {

// Square brackets hold structured-reference parts (Table1[Q1],
// [[#This Row],[Amount]]): column names there are text, never a function,
// a parameter or a cell address — the walkers leave them alone. Since M69
// the lexer hands a balanced structured reference over as ONE token, which
// the walkers skip; only a bracket it could not pair is counted here.
int bracketDelta(const Token &token)
{
    if (isOp(token, QLatin1StringView("["))) {
        return 1;
    }
    return isOp(token, QLatin1StringView("]")) ? -1 : 0;
}

} // namespace

namespace {
// The M61 walks (below): Excel's prefixes off, and back on.
QString withoutPrefixes(const QString &text);
QString withPrefixes(const QString &text);
} // namespace

QString formulaFromFile(const QString &text, const QString &containingTable)
{
    // M69 (D-078): the prefixes go first, so _xlfn.SINGLE is SINGLE by the
    // time it becomes '@'.
    return singleAsAt(structRefsForDisplay(withoutPrefixes(text), containingTable));
}

QString formulaForFile(const QString &text, const QString &containingTable)
{
    // M69 (D-078): structured references and '@' in the file's spelling
    // first; the walk below then gives SINGLE its _xlfn. prefix.
    return withPrefixes(atAsSingle(structRefsForFile(text, containingTable)));
}

namespace {

QString withoutPrefixes(const QString &text)
{
    const QVector<Token> tokens = lexFormula(text);
    Splicer splice{text, {}};
    int brackets = 0;
    for (const Token &token : tokens) {
        brackets += bracketDelta(token);
        if (token.type != Token::Type::Identifier || brackets > 0) {
            continue;
        }
        QString bare;
        if (const QString param = afterPrefix(token.text, kXlpm); !param.isEmpty()) {
            bare = param;
        } else if (const QString ws = afterPrefix(token.text, kXlws); !ws.isEmpty()) {
            bare = prefixTable().value(ws.toUpper()) == kXlws ? ws : QString();
        } else if (const QString fn = afterPrefix(token.text, kXlfn); !fn.isEmpty()) {
            bare = prefixTable().value(fn.toUpper()) == kXlfn ? fn : QString();
        }
        if (!bare.isEmpty()) {
            splice.replace(token.pos, token.text.size(), bare);
        }
    }
    return splice.finish();
}

QString withPrefixes(const QString &text)
{
    const QVector<Token> tokens = lexFormula(text);
    Splicer splice{text, {}};
    QList<Scope> scopes;
    int depth = 0;
    int braces = 0;
    int brackets = 0;
    const auto inScope = [&scopes](const QString &upper) {
        for (const Scope &scope : std::as_const(scopes)) {
            if (scope.parameters.contains(upper)) {
                return true;
            }
        }
        return false;
    };
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const Token &token = tokens.at(i);
        const Token &next = tokens.at(qMin(i + 1, tokens.size() - 1));
        const Token &previous = tokens.at(qMax<qsizetype>(i - 1, 0));
        brackets += bracketDelta(token);
        if (brackets > 0) {
            continue;
        }
        if (token.type == Token::Type::Op) {
            if (isOp(token, QLatin1StringView("{"))) {
                ++braces;
            } else if (isOp(token, QLatin1StringView("}"))) {
                --braces;
            } else if (isOp(token, QLatin1StringView("("))) {
                ++depth;
            } else if (isOp(token, QLatin1StringView(")"))) {
                if (!scopes.isEmpty() && scopes.last().depth == depth) {
                    scopes.removeLast();
                }
                --depth;
            } else if (isOp(token, QLatin1StringView(",")) || isOp(token, QLatin1StringView(";"))) {
                if (braces == 0 && !scopes.isEmpty() && scopes.last().depth == depth) {
                    ++scopes.last().argument;
                }
                if (braces == 0 && token.text == QLatin1String(";")) {
                    splice.replace(token.pos, 1, QStringLiteral(","));
                }
            }
            continue;
        }
        if (token.type != Token::Type::Identifier) {
            continue;
        }
        if (isOp(next, QLatin1StringView("("))) {
            // A LAMBDA bound by LET and called: f(2) is _xlpm.f(2).
            const QString parameter = afterPrefix(token.text, kXlpm);
            if (inScope((parameter.isEmpty() ? token.text : parameter).toUpper())) {
                splice.replace(token.pos, token.text.size(),
                               kXlpm + (parameter.isEmpty() ? token.text : parameter));
                continue;
            }
            // A function call. An existing prefix is kept (normalised to
            // Excel's lower case); a bare known name gets its own.
            QString bare = token.text;
            QString prefix;
            for (const QString &candidate : {kXlws, kXlfn}) {
                if (const QString rest = afterPrefix(bare, candidate); !rest.isEmpty()) {
                    bare = rest;
                    prefix = candidate;
                    break;
                }
            }
            const QString upper = bare.toUpper();
            const bool known = functionRegistry().contains(upper) || prefixTable().contains(upper);
            if (prefix.isEmpty()) {
                prefix = futureFunctionPrefix(upper);
            }
            const QString spelled = prefix + (known ? upper : bare);
            if (spelled != token.text) {
                splice.replace(token.pos, token.text.size(), spelled);
            }
            if (upper == QLatin1String("LET") || upper == QLatin1String("LAMBDA")) {
                scopes.append({depth + 1, 0, upper == QLatin1String("LAMBDA"), {}});
            }
            continue;
        }
        // A name. Inside LET/LAMBDA a lone argument followed by ',' declares
        // a parameter (every other LET argument; every LAMBDA argument but
        // the body), and later uses of it carry the prefix too.
        QString bare = afterPrefix(token.text, kXlpm);
        if (bare.isEmpty()) {
            bare = token.text;
        }
        const QString upper = bare.toUpper();
        bool parameter = false;
        if (!scopes.isEmpty()) {
            Scope &scope = scopes.last();
            const bool lone = scope.depth == depth
                && (isOp(previous, QLatin1StringView("(")) || isOp(previous, QLatin1StringView(","))
                    || isOp(previous, QLatin1StringView(";")))
                && (isOp(next, QLatin1StringView(",")) || isOp(next, QLatin1StringView(";")));
            if (lone && (scope.lambda || scope.argument % 2 == 0)) {
                scope.parameters.insert(upper);
                parameter = true;
            }
        }
        if (parameter || inScope(upper)) {
            splice.replace(token.pos, token.text.size(), kXlpm + bare);
        }
    }
    return splice.finish();
}

} // namespace

QString shiftedFormulaText(const QString &text, qint32 dr, qint32 dc)
{
    const QVector<Token> tokens = lexFormula(text);
    Splicer splice{text, {}};
    int brackets = 0;
    for (const Token &token : tokens) {
        brackets += bracketDelta(token);
        if (token.type != Token::Type::Ref || brackets > 0) {
            continue;
        }
        const ParseResult parsed = parseFormula(token.text);
        if (!parsed.error.isEmpty() || !parsed.root || parsed.root->kind != Node::Kind::Ref) {
            continue;
        }
        const QString moved = nodeText(shiftNode(parsed.root, dr, dc));
        if (moved != token.text) {
            splice.replace(token.pos, token.text.size(), moved);
        }
    }
    return splice.finish();
}

} // namespace QindaOffice::Calc::IO
