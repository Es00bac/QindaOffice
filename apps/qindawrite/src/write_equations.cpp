// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_equations.h"
#include "write_host.h"
#include "page_text_control.h"
#include "docx_math.h"
#include "docx_properties.h"
namespace QindaOffice::Apps::Write {
Quick::EquationSession *WriteHost::equations() const { return m_equations->session(); }

WriteEquations::WriteEquations(WriteHost *host)
    : QObject(host), m_host(host), m_session(host->actions(), this)
{
    auto *insert = host->actions()->add(QStringLiteral("writeInsertEquationAction"), tr("Insert equation…"),
                                        QKeySequence(QStringLiteral("Alt+=")), QStringLiteral("sigma"));
    auto *edit = host->actions()->add(QStringLiteral("writeEditEquationAction"), tr("Edit equation…"), {}, QStringLiteral("sigma"));
    connect(insert, &Quick::ActionEntry::triggered, this, [this] { begin(false); });
    connect(edit, &Quick::ActionEntry::triggered, this, [this] { begin(true); });
    connect(host->control(), &PageTextControl::cursorPositionChanged, this, &WriteEquations::sync);
    connect(host->control(), &PageTextControl::documentChanged, this, [this] { m_session.cancel(); m_target = {}; sync(); });
    connect(&m_session, &Quick::EquationSession::accepted, this, &WriteEquations::commit);
    sync();
}
QList<Quick::MenuNode> WriteEquations::menuItems() const
{
    using Quick::MenuNode;
    return {MenuNode::action(QStringLiteral("writeInsertEquationAction")), MenuNode::action(QStringLiteral("writeEditEquationAction"))};
}
void WriteEquations::sync()
{
    m_host->actions()->setEnabled(QStringLiteral("writeEditEquationAction"), !Docx::equationAt(m_host->control()->textCursor()).isNull());
}
void WriteEquations::begin(bool existing)
{
    const QTextCursor cursor = m_host->control()->textCursor();
    m_target = existing ? Docx::equationAt(cursor) : cursor;
    if (m_target.isNull()) return;
    const auto format = m_target.charFormat();
    const QString xml = existing ? format.stringProperty(Docx::kInlineXml) : QString();
    qreal size = format.fontPointSize();
    if (size <= 0) size = cursor.document()->defaultFont().pointSizeF();
    if (!m_session.begin(xml, size > 0 ? size : 11))
        emit m_host->messageRequested(tr("Equation"), m_session.error(), QStringLiteral("danger"));
}
bool WriteEquations::editAt(int position)
{
    QTextCursor cursor(m_host->control()->document()); cursor.setPosition(position);
    const auto found = Docx::equationAt(cursor);
    if (found.isNull()) return false;
    m_host->control()->setTextCursor(found); begin(true); return true;
}
void WriteEquations::commit(const QString &xml)
{
    if (m_target.isNull() || m_target.document() != m_host->control()->document()) return;
    const auto old = m_target.charFormat();
    const bool replacing = m_target.hasSelection() && Docx::isEquation(old);
    if (replacing && old.stringProperty(Docx::kInlineXml) == xml) return;
    const auto format = Docx::equationFormat(xml, old, old.stringProperty(Docx::kInlineSourcePart),
                                            replacing ? old.intProperty(Docx::kInlineSerial) : 0);
    m_target.beginEditBlock();
    m_target.insertText(QString(QChar::ObjectReplacementCharacter), format);
    m_target.endEditBlock();
    m_host->control()->setTextCursor(m_target);
    // Following typing must not inherit the custom object's preservation XML.
    QTextCharFormat plain = old;
    plain.setObjectType(QTextFormat::NoObject);
    for (int key : {Docx::kInlineXml, Docx::kInlineSourcePart, Docx::kInlineSerial, Docx::kInlineLabel}) plain.clearProperty(key);
    m_host->control()->setCurrentCharFormat(plain);
    sync();
}
}
