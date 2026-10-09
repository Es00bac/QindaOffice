// SPDX-License-Identifier: GPL-3.0-or-later
// Word's merge field codes: the labels a main document shows, the codes
// the Mailings commands insert, and evaluation for one recipient. The
// tokenizer is in docx_merge_tokens.cpp; the Address Block / Greeting Line
// formats and Match Fields are in docx_merge_address.cpp.

#include "docx_merge_fields.h"
#include "docx_merge_fields_p.h"

#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>

#include <functional>

namespace QindaOffice::Docx {


using detail::FieldCode;
using detail::FieldToken;

namespace {

// Quoted text with its nested fields evaluated ("Dear { MERGEFIELD First }").
QString expandText(const QString &text, MergeCursor &cursor)
{
    QString out;
    const qsizetype n = text.size();
    for (qsizetype i = 0; i < n; ++i) {
        if (text.at(i) != QLatin1Char('{')) {
            out += text.at(i);
            continue;
        }
        int depth = 1;
        bool quoted = false;
        qsizetype j = i + 1;
        for (; j < n; ++j) {
            const QChar ch = text.at(j);
            if (ch == QLatin1Char('"')) {
                quoted = !quoted;
            } else if (!quoted && ch == QLatin1Char('{')) {
                ++depth;
            } else if (!quoted && ch == QLatin1Char('}') && --depth == 0) {
                break;
            }
        }
        out += evaluateMergeField(text.mid(i + 1, j - i - 1).trimmed(), cursor);
        i = j;
    }
    return out;
}

QString tokenValue(const FieldToken &token, MergeCursor &cursor)
{
    switch (token.kind) {
    case FieldToken::Field:
        return evaluateMergeField(token.text, cursor);
    case FieldToken::Quoted:
        return expandText(token.text, cursor);
    default:
        return token.text;
    }
}

bool numberOf(const QString &text, double *value)
{
    bool ok = false;
    QString clean = text.trimmed();
    clean.remove(QLatin1Char(','));
    *value = clean.toDouble(&ok);
    return ok && !clean.isEmpty();
}

bool isOperator(const QString &text)
{
    static const QStringList kOps{QStringLiteral("="),  QStringLiteral("<>"), QStringLiteral("<"),
                                  QStringLiteral(">"),  QStringLiteral("<="), QStringLiteral(">=")};
    return kOps.contains(text);
}

// Word's comparison: numbers as numbers, text case-sensitively, and on =
// and <> a quoted right side with * or ? is a wildcard pattern.
bool compare(const QString &left, const QString &op, const FieldToken &rightToken, MergeCursor &cursor)
{
    const QString right = tokenValue(rightToken, cursor);
    if ((op == u"=" || op == u"<>") && rightToken.kind == FieldToken::Quoted
        && (right.contains(QLatin1Char('*')) || right.contains(QLatin1Char('?')))) {
        const QRegularExpression pattern(QRegularExpression::wildcardToRegularExpression(
            right, QRegularExpression::NonPathWildcardConversion));
        const bool match = pattern.match(left).hasMatch();
        return op == u"=" ? match : !match;
    }
    int c = 0;
    double x = 0;
    double y = 0;
    if (numberOf(left, &x) && numberOf(right, &y)) {
        c = x < y ? -1 : x > y ? 1 : 0;
    } else {
        c = QString::compare(left, right, Qt::CaseSensitive);
    }
    if (op == u"<>") {
        return c != 0;
    }
    if (op == u"<") {
        return c < 0;
    }
    if (op == u">") {
        return c > 0;
    }
    if (op == u"<=") {
        return c <= 0;
    }
    if (op == u">=") {
        return c >= 0;
    }
    return c == 0;
}

bool condition(const FieldCode &code, MergeCursor &cursor)
{
    if (code.args.size() < 3 || !isOperator(code.args.at(1).text)) {
        return false;
    }
    return compare(tokenValue(code.args.at(0), cursor), code.args.at(1).text, code.args.at(2), cursor);
}

QString formatDate(const QString &value, QString picture)
{
    QDateTime when = QDateTime::fromString(value.trimmed(), Qt::ISODate);
    if (!when.isValid()) {
        for (const char *f : {"M/d/yyyy", "M/d/yy", "d.M.yyyy", "yyyy-MM-dd", "d-MMM-yyyy", "MMMM d, yyyy"}) {
            const QDate date = QDate::fromString(value.trimmed(), QString::fromLatin1(f));
            if (date.isValid()) {
                when = QDateTime(date, QTime(0, 0));
                break;
            }
        }
    }
    if (!when.isValid()) {
        return value;
    }
    picture.replace(QStringLiteral("AM/PM"), QStringLiteral("AP")).replace(QStringLiteral("am/pm"), QStringLiteral("ap"));
    picture.remove(QLatin1Char('\''));
    return QLocale::c().toString(when, picture);
}

QString formatNumber(const QString &value, const QString &picture)
{
    double v = 0;
    if (!numberOf(value, &v)) {
        return value;
    }
    const QStringList sections = picture.split(QLatin1Char(';'));
    QString pic = sections.first();
    bool sign = v < 0;
    if (v < 0 && sections.size() > 1) {
        pic = sections.at(1);
        sign = false;
    } else if (v == 0 && sections.size() > 2) {
        pic = sections.at(2);
    }
    static const QRegularExpression core(QStringLiteral("[0#][0#,.]*"));
    const QRegularExpressionMatch m = core.match(pic);
    if (!m.hasMatch()) {
        return pic.remove(QLatin1Char('\''));
    }
    const QString digits = m.captured(0);
    const qsizetype dot = digits.indexOf(QLatin1Char('.'));
    const QString integer = dot >= 0 ? digits.left(dot) : digits;
    const QString fraction = dot >= 0 ? digits.mid(dot + 1) : QString();
    const int decimals = int(fraction.size());
    const int required = int(fraction.count(QLatin1Char('0')));
    QString text = QString::number(qAbs(v), 'f', decimals);
    QString whole = text.section(QLatin1Char('.'), 0, 0);
    QString frac = text.section(QLatin1Char('.'), 1);
    while (frac.size() > required && frac.endsWith(QLatin1Char('0'))) {
        frac.chop(1);
    }
    const int minimum = int(integer.count(QLatin1Char('0')));
    while (whole.size() < minimum) {
        whole.prepend(QLatin1Char('0'));
    }
    if (minimum == 0 && whole == u"0") {
        whole.clear();
    }
    if (integer.contains(QLatin1Char(','))) {
        for (qsizetype i = whole.size() - 3; i > 0; i -= 3) {
            whole.insert(i, QLatin1Char(','));
        }
    }
    QString number = frac.isEmpty() ? whole : whole + QLatin1Char('.') + frac;
    QString prefix = pic.left(m.capturedStart());
    QString suffix = pic.mid(m.capturedEnd());
    return prefix.remove(QLatin1Char('\'')) + (sign ? QStringLiteral("-") : QString()) + number
        + suffix.remove(QLatin1Char('\''));
}

QString applyFormatSwitch(const QString &value, const FieldCode &code)
{
    QString out = value;
    for (const auto &[key, argument] : code.switches) {
        if (key == u"\\*") {
            const QString f = argument.text.toUpper();
            if (f == u"UPPER") {
                out = out.toUpper();
            } else if (f == u"LOWER") {
                out = out.toLower();
            } else if (f == u"CAPS") {
                QString caps = out.toLower();
                for (qsizetype i = 0; i < caps.size(); ++i) {
                    if (caps.at(i).isLetter() && (i == 0 || caps.at(i - 1).isSpace())) {
                        caps[i] = caps.at(i).toUpper();
                    }
                }
                out = caps;
            } else if (f == u"FIRSTCAP" && !out.isEmpty()) {
                out[0] = out.at(0).toUpper();
            }
        } else if (key == u"\\@" && !out.isEmpty()) {
            out = formatDate(out, argument.text);
        } else if (key == u"\\#" && !out.isEmpty()) {
            out = formatNumber(out, argument.text);
        }
    }
    return out;
}

QString fieldName(const FieldCode &code)
{
    return code.args.isEmpty() ? QString() : code.args.first().text.trimmed();
}

} // namespace

int mergeColumnIndex(const MergeData &data, const QString &name)
{
    if (const int exact = data.fieldIndex(name); exact >= 0) {
        return exact;
    }
    QString spaced = name;
    spaced.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (const int index = data.fieldIndex(spaced); index >= 0) {
        return index;
    }
    static const QRegularExpression noise(QStringLiteral("[^\\p{L}\\p{N}]"));
    QString wanted = name;
    wanted.remove(noise);
    for (int i = 0; i < data.fields.size(); ++i) {
        QString plain = data.fields.at(i);
        plain.remove(noise);
        if (!plain.isEmpty() && plain.compare(wanted, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

QString MergeCursor::value(const QString &column) const
{
    if (data == nullptr || record() < 0) {
        return {};
    }
    const int index = mergeColumnIndex(*data, column);
    return index >= 0 ? data->records.at(record()).value(index) : QString();
}

void MergeCursor::next()
{
    if (position + 1 < order.size()) {
        ++position;
    } else {
        exhausted = true;
    }
}

MergeFieldKind mergeFieldKind(const QString &instruction)
{
    const QString type = instruction.trimmed().section(QLatin1Char(' '), 0, 0).toUpper();
    if (type == u"MERGEFIELD") {
        return MergeFieldKind::MergeField;
    }
    if (type == u"ADDRESSBLOCK") {
        return MergeFieldKind::AddressBlock;
    }
    if (type == u"GREETINGLINE") {
        return MergeFieldKind::GreetingLine;
    }
    if (type == u"SKIPIF") {
        return MergeFieldKind::SkipIf;
    }
    if (type == u"NEXT") {
        return MergeFieldKind::Next;
    }
    if (type == u"NEXTIF") {
        return MergeFieldKind::NextIf;
    }
    if (type == u"MERGEREC") {
        return MergeFieldKind::MergeRec;
    }
    if (type == u"MERGESEQ") {
        return MergeFieldKind::MergeSeq;
    }
    if (type == u"IF" && instruction.contains(QStringLiteral("MERGEFIELD"), Qt::CaseInsensitive)) {
        return MergeFieldKind::If;
    }
    return MergeFieldKind::None;
}

bool mergeFieldHasResult(MergeFieldKind kind)
{
    return kind != MergeFieldKind::Next && kind != MergeFieldKind::NextIf && kind != MergeFieldKind::SkipIf;
}

QString mergeFieldLabel(const QString &instruction)
{
    const FieldCode code = detail::parseField(instruction);
    switch (mergeFieldKind(instruction)) {
    case MergeFieldKind::MergeField:
        return QStringLiteral("«%1»").arg(fieldName(code));
    case MergeFieldKind::AddressBlock:
        return QStringLiteral("«AddressBlock»");
    case MergeFieldKind::GreetingLine:
        return QStringLiteral("«GreetingLine»");
    case MergeFieldKind::Next:
        return QStringLiteral("«Next Record»");
    case MergeFieldKind::NextIf:
        return QStringLiteral("«Next Record If…»");
    case MergeFieldKind::SkipIf:
        return QStringLiteral("«Skip Record If…»");
    case MergeFieldKind::MergeRec:
        return QStringLiteral("«Merge Record #»");
    case MergeFieldKind::MergeSeq:
        return QStringLiteral("«Merge Sequence #»");
    case MergeFieldKind::If:
        return QStringLiteral("«IF»");
    case MergeFieldKind::None:
        break;
    }
    return {};
}

QStringList mergeFieldColumns(const QString &instruction)
{
    QStringList columns;
    const FieldCode code = detail::parseField(instruction);
    if (code.type == u"MERGEFIELD") {
        columns.append(fieldName(code));
        return columns;
    }
    std::function<void(const QString &)> scan = [&](const QString &text) {
        static const QRegularExpression nested(QStringLiteral("\\{\\s*MERGEFIELD\\s+(\"[^\"]+\"|[^\\s}\\\\]+)"),
                                               QRegularExpression::CaseInsensitiveOption);
        for (const QRegularExpressionMatch &m : nested.globalMatch(text)) {
            QString name = m.captured(1);
            name.remove(QLatin1Char('"'));
            if (!columns.contains(name)) {
                columns.append(name);
            }
        }
    };
    scan(instruction);
    return columns;
}

QString mergeFieldCode(const QString &column)
{
    return column.contains(QLatin1Char(' ')) ? QStringLiteral("MERGEFIELD \"%1\"").arg(column)
                                             : QStringLiteral("MERGEFIELD %1").arg(column);
}

QString ifCode(const QString &column, const QString &op, const QString &value, const QString &then,
               const QString &otherwise)
{
    auto quote = [](QString text) { return QLatin1Char('"') + text.replace(QLatin1Char('"'), QStringLiteral("\\\"")) + QLatin1Char('"'); };
    return QStringLiteral("IF { %1 } %2 %3 %4 %5").arg(mergeFieldCode(column), op, quote(value), quote(then), quote(otherwise));
}

QString skipIfCode(const QString &column, const QString &op, const QString &value)
{
    QString quoted = value;
    return QStringLiteral("SKIPIF { %1 } %2 \"%3\"").arg(mergeFieldCode(column), op, quoted.replace(QLatin1Char('"'), QStringLiteral("\\\"")));
}

QString nextIfCode(const QString &column, const QString &op, const QString &value)
{
    QString quoted = value;
    return QStringLiteral("NEXTIF { %1 } %2 \"%3\"").arg(mergeFieldCode(column), op, quoted.replace(QLatin1Char('"'), QStringLiteral("\\\"")));
}

QString evaluateMergeField(const QString &instruction, MergeCursor &cursor, const QString &fallback)
{
    const FieldCode code = detail::parseField(instruction);
    QString out;
    if (code.type == u"MERGEFIELD") {
        out = cursor.value(fieldName(code));
        if (!out.isEmpty()) {
            if (const FieldToken *before = code.switchArgument(u"\\b")) {
                out.prepend(tokenValue(*before, cursor));
            }
            if (const FieldToken *after = code.switchArgument(u"\\f")) {
                out.append(tokenValue(*after, cursor));
            }
        }
    } else if (code.type == u"IF") {
        if (code.args.size() >= 3 && !isOperator(code.args.at(1).text)) {
            const QString value = tokenValue(code.args.at(0), cursor).trimmed();
            const bool truth = !value.isEmpty() && value != u"0";
            out = tokenValue(code.args.at(truth ? 1 : 2), cursor);
        } else {
            const bool truth = condition(code, cursor);
            const int pick = truth ? 3 : 4;
            out = pick < code.args.size() ? tokenValue(code.args.at(pick), cursor) : QString();
        }
    } else if (code.type == u"SKIPIF") {
        cursor.skip = cursor.skip || condition(code, cursor);
        return {};
    } else if (code.type == u"NEXTIF") {
        if (condition(code, cursor)) {
            cursor.next();
        }
        return {};
    } else if (code.type == u"NEXT") {
        cursor.next();
        return {};
    } else if (code.type == u"MERGEREC") {
        out = cursor.record() >= 0 ? QString::number(cursor.position + 1) : QString();
    } else if (code.type == u"MERGESEQ") {
        out = QString::number(cursor.sequence);
    } else if (code.type == u"ADDRESSBLOCK" || code.type == u"GREETINGLINE") {
        out = detail::formatAddressField(code, cursor);
    } else {
        return fallback;
    }
    return applyFormatSwitch(out, code);
}

} // namespace QindaOffice::Docx
