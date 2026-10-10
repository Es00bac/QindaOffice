// SPDX-License-Identifier: GPL-3.0-or-later
// Subqueries inside expressions and criteria (M86, D-094): "In (SELECT …)",
// "Exists (SELECT …)" and "(SELECT Max([amount]) FROM …)". The parser only
// cuts the subquery's text out (from its tokens' places in the source) and
// hands it to Context::subquery — the query compiler, which reads it as
// Access SQL and compiles it like any saved query, quoting every literal.
// Nothing here pastes user text into SQL.
#include "base_expression_parser.h"

namespace QindaOffice::Base::Expr::detail {

Value Parser::subqueryBody()
{
    const int first = m_pos; // at SELECT, just after "("
    int depth = 1;
    int i = m_pos;
    for (; i < m_t.size() && m_t.at(i).kind != Token::End; ++i) {
        if (m_t.at(i).kind == Token::Op && m_t.at(i).text == QLatin1String("(")) {
            ++depth;
        } else if (m_t.at(i).kind == Token::Op && m_t.at(i).text == QLatin1String(")") && --depth == 0) {
            break;
        }
    }
    if (i >= m_t.size() || m_t.at(i).kind == Token::End) {
        if (error.isEmpty()) {
            error = QStringLiteral("A subquery is not closed with \")\".");
        }
        m_pos = int(m_t.size()) - 1;
        return {};
    }
    const QString text = m_src.mid(m_t.at(first).begin, m_t.at(i).begin - m_t.at(first).begin).trimmed();
    m_pos = i + 1;
    if (!m_ctx.subquery) {
        if (error.isEmpty()) {
            error = QStringLiteral("A subquery (SELECT …) cannot be used here.");
        }
        return {};
    }
    QString problem;
    const QString sql = m_ctx.subquery(text, &problem);
    if (!problem.isEmpty()) {
        if (error.isEmpty()) {
            error = QStringLiteral("The subquery: %1").arg(problem);
        }
        return {};
    }
    return {QLatin1Char('(') + sql + QLatin1Char(')'), std::nullopt};
}

} // namespace QindaOffice::Base::Expr::detail
