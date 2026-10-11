// SPDX-License-Identifier: GPL-3.0-or-later
#include "pivot_calculated.h"
#include "eval.h"
#include "workbook.h"
#include <QObject>

namespace QindaOffice::Calc {
namespace {
QString alias(qint32 field) { return QStringLiteral("_QQOFIELD_%1").arg(field); }
qint32 fieldNamed(const PivotCache &cache, const QString &name)
{
    for (qsizetype f = 0; f < cache.fields.size(); ++f)
        if (cache.fields.at(f).name.compare(name, Qt::CaseInsensitive) == 0) return qint32(f);
    return -1;
}
NodePtr parsed(const PivotCache &cache, qint32 field)
{
    const QString source = cache.fields.at(field).formula;
    QString formula;
    for (qsizetype i = 0; i < source.size();) {
        const QChar ch = source.at(i);
        if (ch == QLatin1Char('"')) {
            // Preserve a string verbatim; its contents are not field references.
            formula += ch; ++i;
            while (i < source.size()) {
                formula += source.at(i);
                if (source.at(i++) == QLatin1Char('"')) {
                    if (i < source.size() && source.at(i) == QLatin1Char('"')) { formula += source.at(i++); continue; }
                    break;
                }
            }
        } else if (ch == QLatin1Char('\'')) {
            QString name; ++i;
            bool closed = false;
            while (i < source.size()) {
                const QChar c = source.at(i++);
                if (c == QLatin1Char('\'')) {
                    if (i < source.size() && source.at(i) == QLatin1Char('\'')) { name += c; ++i; }
                    else { closed = true; break; }
                } else name += c;
            }
            const qint32 f = fieldNamed(cache, name);
            if (!closed || f < 0) return {};
            formula += alias(f);
        } else if (ch.isLetter() || ch == QLatin1Char('_')) {
            const qsizetype first = i++;
            while (i < source.size() && (source.at(i).isLetterOrNumber() || source.at(i) == QLatin1Char('_')
                                          || source.at(i) == QLatin1Char('.'))) ++i;
            const QString name = source.mid(first, i - first);
            qsizetype next = i;
            while (next < source.size() && source.at(next).isSpace()) ++next;
            const qint32 f = fieldNamed(cache, name);
            formula += f >= 0 && (next == source.size() || source.at(next) != QLatin1Char('(')) ? alias(f) : name;
        } else { formula += ch; ++i; }
    }
    if (formula.startsWith(QLatin1Char('='))) formula.remove(0, 1);
    return parseFormula(formula).root;
}
bool validNode(const NodePtr &node, const PivotCache &cache, QSet<qint32> &visiting);
bool validField(const PivotCache &cache, qint32 field, QSet<qint32> &visiting)
{
    if (field < 0 || field >= cache.fields.size() || visiting.size() >= 64 || visiting.contains(field)) return false;
    if (cache.fields.at(field).formula.isEmpty()) return cache.fields.at(field).database;
    visiting.insert(field);
    const bool valid = validNode(parsed(cache, field), cache, visiting);
    visiting.remove(field);
    return valid;
}
bool validNode(const NodePtr &node, const PivotCache &cache, QSet<qint32> &visiting)
{
    if (!node) return false;
    if (node->kind == Node::Kind::Name) {
        if (!node->text.startsWith(QLatin1String("_QQOFIELD_"))) return false;
        return validField(cache, node->text.mid(10).toInt(), visiting);
    }
    if (node->kind == Node::Kind::Ref || node->kind == Node::Kind::StructRef || node->kind == Node::Kind::Array) return false;
    if (node->kind == Node::Kind::Call) {
        const auto found = functionRegistry().constFind(node->text);
        if (found == functionRegistry().constEnd() || found->isVolatile || found->preserveRefArgs) return false;
    }
    if (node->left && !validNode(node->left, cache, visiting)) return false;
    if (node->right && !validNode(node->right, cache, visiting)) return false;
    for (const auto &arg : node->args) if (!validNode(arg, cache, visiting)) return false;
    return true;
}
Value evaluate(const PivotCache &cache, qint32 field, const QVector<PivotAcc> &sums, QSet<qint32> &visiting)
{
    if (field < 0 || field >= cache.fields.size() || visiting.size() >= 64 || visiting.contains(field)) return Err::Ref;
    if (cache.fields.at(field).formula.isEmpty()) return field < sums.size() ? pivotFinalize(sums.at(field), PivotFunction::Sum) : Value(0.0);
    visiting.insert(field);
    const NodePtr node = parsed(cache, field);
    Workbook emptyHost;
    EvalContext context;
    context.host = &emptyHost;
    // Only referenced fields are evaluated, so an unrelated circular field
    // cannot poison every calculated field in a shared cache.
    std::function<void(const NodePtr &)> bind = [&](const NodePtr &part) {
        if (!part) return;
        if (part->kind == Node::Kind::Name && part->text.startsWith(QLatin1String("_QQOFIELD_")))
            context.locals.insert(part->text, evaluate(cache, part->text.mid(10).toInt(), sums, visiting));
        bind(part->left); bind(part->right);
        for (const auto &arg : part->args) bind(arg);
    };
    bind(node);
    const Value value = node ? evalNode(node, context) : Value(Err::Name);
    visiting.remove(field);
    return value;
}
}
QString pivotCalculatedProblem(const PivotCache &cache, qint32 field)
{
    QSet<qint32> visiting;
    return validField(cache, field, visiting) ? QString() : QObject::tr("Use field names and ordinary formulas; cell references, unknown fields and circular formulas are not allowed.");
}
Value pivotCalculatedValue(const PivotCache &cache, qint32 field, const QVector<PivotAcc> &sums)
{
    if (!pivotCalculatedProblem(cache, field).isEmpty()) return Err::Name;
    QSet<qint32> visiting;
    return evaluate(cache, field, sums, visiting);
}
} // namespace QindaOffice::Calc
