// SPDX-License-Identifier: GPL-3.0-or-later
#include "equation_session.h"
#include "math_latex.h"
#include "math_linear.h"
#include "math_omml.h"
#include "math_layout.h"
#include <functional>

namespace QindaOffice::Quick {
EquationSession::EquationSession(ActionModel *actions, QObject *parent)
    : QObject(parent), m_actions(actions), m_editor(this)
{
    using S = Math::Structure;
    struct Template { const char *id; const char *label; std::function<S()> make; };
    const Template templates[] = {
        {"Fraction", QT_TR_NOOP("Fraction"), [] { return S::fraction(); }},
        {"Binomial", QT_TR_NOOP("Binomial"), [] { return S::fraction(Math::FractionType::NoBar); }},
        {"Superscript", QT_TR_NOOP("Superscript"), [] { return S::superscript(); }},
        {"Subscript", QT_TR_NOOP("Subscript"), [] { return S::subscript(); }},
        {"SubSuperscript", QT_TR_NOOP("Subscript and superscript"), [] { return S::subSuperscript(); }},
        {"Prescript", QT_TR_NOOP("Prescript"), [] { return S::leftSubSuperscript(); }},
        {"SquareRoot", QT_TR_NOOP("Square root"), [] { return S::radical(); }},
        {"Root", QT_TR_NOOP("Root with degree"), [] { return S::radical(true); }},
        {"Integral", QT_TR_NOOP("Integral"), [] { return S::integral(); }},
        {"Sum", QT_TR_NOOP("Large operator"), [] { return S::largeOperator(); }},
        {"Bracket", QT_TR_NOOP("Brackets"), [] { return S::bracket(); }},
        {"Function", QT_TR_NOOP("Function"), [] { return S::function(); }},
        {"Accent", QT_TR_NOOP("Accent"), [] { return S::accent(); }},
        {"Bar", QT_TR_NOOP("Overbar"), [] { return S::bar(); }},
        {"Group", QT_TR_NOOP("Underbrace"), [] { return S::groupCharacter(); }},
        {"Limit", QT_TR_NOOP("Limit"), [] { return S::limit(); }},
        {"Logarithm", QT_TR_NOOP("Logarithm"), [] { return S::logarithm(); }},
        {"Operator", QT_TR_NOOP("Labelled operator"), [] { return S::operatorWithText(); }},
        {"Matrix", QT_TR_NOOP("Matrix"), [] { return S::matrix(); }},
        {"Array", QT_TR_NOOP("Equation array"), [] { return S::equationArray(); }},
        {"BorderBox", QT_TR_NOOP("Bordered box"), [] { return S::boxed(); }},
        {"Box", QT_TR_NOOP("Box"), [] { return S::box(); }},
        {"Phantom", QT_TR_NOOP("Phantom"), [] { return S::phantom(); }}
    };
    for (const auto &entry : templates) {
        const QString id = QStringLiteral("equation") + QString::fromLatin1(entry.id);
        m_templates.append(id);
        auto *command = m_actions->add(id, tr(entry.label), {}, QStringLiteral("sigma"));
        connect(command, &ActionEntry::triggered, this, [this, make = entry.make] {
            m_editor.insertStructure(make()); emit focusRequested();
        });
    }
    const auto add = [this](const QString &id, const QString &label, auto run) {
        connect(m_actions->add(id, label), &ActionEntry::triggered, this, run);
    };
    add(QStringLiteral("equationBuild"), tr("Build professional equation"), [this] { emit buildRequested(); });
    add(QStringLiteral("equationUndo"), tr("Undo equation edit"), [this] { m_editor.undo(); });
    add(QStringLiteral("equationRedo"), tr("Redo equation edit"), [this] { m_editor.redo(); });
    add(QStringLiteral("equationPreviousSlot"), tr("Previous slot"), [this] { m_editor.nextSlot(true); emit focusRequested(); });
    add(QStringLiteral("equationNextSlot"), tr("Next slot"), [this] { m_editor.nextSlot(); emit focusRequested(); });
    connect(&m_editor, &Math::EquationEditor::changed, this, [this] { m_error.clear(); emit changed(); });
    connect(&m_editor, &Math::EquationEditor::historyChanged, this, &EquationSession::syncActions);
    syncActions();
}
QString EquationSession::linearText() const { return Math::toLinear(m_editor.equation()); }
QString EquationSession::latexText() const { return Math::toLatex(m_editor.equation()); }
ActionEntry *EquationSession::action(const QString &id) const { return m_actions->entry(id); }
void EquationSession::syncActions()
{
    action(QStringLiteral("equationBuild"))->setEnabled(m_active);
    for (const QString &id : m_templates) action(id)->setEnabled(m_active);
    action(QStringLiteral("equationUndo"))->setEnabled(m_active && m_editor.canUndo());
    action(QStringLiteral("equationRedo"))->setEnabled(m_active && m_editor.canRedo());
    action(QStringLiteral("equationPreviousSlot"))->setEnabled(m_active);
    action(QStringLiteral("equationNextSlot"))->setEnabled(m_active);
}
bool EquationSession::begin(const QString &xml, qreal size)
{
    const auto equation = xml.isEmpty() ? std::optional(Math::makeEquation(false)) : Math::readOmmlFragment(xml);
    if (!equation) { m_error = tr("This equation could not be read."); emit changed(); return false; }
    m_pointSize = qBound<qreal>(6, size, 144);
    Math::LayoutOptions options;
    options.fontSizePt = m_pointSize;
    if (!Math::EquationLayout(*equation, options).isValid()) {
        m_error = tr("No OpenType math font is available."); emit changed(); return false;
    }
    m_original = xml;
    m_initial = Math::writeOmmlFragment(*equation);
    m_active = true;
    m_editor.reset(*equation);
    emit activeChanged(); emit requested();
    return true;
}
QString EquationSession::resultXml() const
{
    const QString xml = Math::writeOmmlFragment(m_editor.equation());
    return !m_original.isEmpty() && xml == m_initial ? m_original : xml;
}
bool EquationSession::convert(const QString &source, bool latex)
{
    if (!m_active) return false;
    QString error;
    const auto equation = latex ? Math::fromLatex(source, m_editor.equation().display, &error)
                                : Math::fromLinear(source, m_editor.equation().display, &error);
    if (!equation) { m_error = error; emit changed(); return false; }
    m_editor.replaceEquation(*equation);
    emit focusRequested();
    return true;
}
void EquationSession::accept()
{
    if (!m_active) return;
    const QString xml = resultXml();
    m_active = false; syncActions(); emit activeChanged(); emit accepted(xml);
}
void EquationSession::cancel()
{
    if (!m_active) return;
    m_active = false; syncActions(); emit activeChanged();
}
}
