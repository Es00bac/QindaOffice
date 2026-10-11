// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the LaTeX reader (math_latex_parse.cpp: tokens, groups,
// scripts, brackets, \left…\right; math_latex_commands.cpp: the commands,
// environments, big operators and functions).

#include "math_build_p.h"

#include <QList>
#include <QString>

namespace QindaOffice::Math::detail {

// What ends an expression (not consumed).
enum LatexStop : int { Brace = 1, Amp = 2, Row = 4, Right = 8, End = 16, Bracket = 32, Square = 64 };

[[nodiscard]] inline char32_t onlyChar(const QString &s)
{
    const QList<uint> chars = s.toUcs4();
    return chars.size() == 1 ? char32_t(chars.first()) : 0;
}

class LatexParser {
public:
    explicit LatexParser(const QString &source);
    [[nodiscard]] Argument parse();

private:
    QList<char32_t> m_s;
    qsizetype m_i = 0;
    int m_depth = 0; // open plain brackets
    int m_bars = 0;  // open |…| and ‖…‖
    RunStyle m_style; // \mathbf{…} and friends

    [[nodiscard]] char32_t peek(qsizetype k = 0) const { return m_i + k < m_s.size() ? m_s.at(m_i + k) : 0; }
    [[nodiscard]] bool atEnd() const { return m_i >= m_s.size(); }
    void skipSpaces();
    // The command at the cursor without consuming it ("" when none).
    [[nodiscard]] QString peekCommand() const;
    QString readCommand();
    // {raw text} as written (environment names, \text, \operatorname).
    QString rawGroup();

    AtomList expression(int stops);
    // A braced group or one token: a command's argument.
    Argument argument();
    [[nodiscard]] bool closes(char32_t c, const AtomList &atoms, int stops) const;
    void addChar(AtomList &atoms, char32_t c);
    // False at a stop (not consumed).
    bool element(AtomList &atoms, int stops);
    void scripts(AtomList &atoms);
    void bracket(AtomList &atoms, char32_t open, int stops);
    QString delimiterToken();
    NodePtr leftRight();
    // math_latex_commands.cpp
    void command(AtomList &atoms, const QString &name, int stops);
    NodePtr environment();
    NodePtr nary(char32_t op);
    NodePtr function(const QString &name);
};

} // namespace QindaOffice::Math::detail
