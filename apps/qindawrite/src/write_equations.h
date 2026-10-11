// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "equation_session.h"
#include "menu_tree.h"
#include <QTextCursor>
namespace QindaOffice::Apps::Write {
class WriteHost;
class WriteEquations : public QObject {
    Q_OBJECT
public:
    explicit WriteEquations(WriteHost *host);
    Quick::EquationSession *session() { return &m_session; }
    QList<Quick::MenuNode> menuItems() const;
    bool editAt(int position);
private:
    void begin(bool existing);
    void commit(const QString &xml);
    void sync();
    WriteHost *m_host;
    Quick::EquationSession m_session;
    QTextCursor m_target;
};
}
