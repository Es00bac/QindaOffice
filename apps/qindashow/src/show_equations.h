// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "equation_session.h"
namespace QindaOffice::Apps::Show {
class ShowHost;
class ShowEquations : public QObject {
    Q_OBJECT
public:
    explicit ShowEquations(ShowHost *host);
    Quick::EquationSession *session() { return &m_session; }
private:
    void begin(bool existing);
    void commit(const QString &xml);
    void sync();
    ShowHost *m_host;
    Quick::EquationSession m_session;
    QString m_shapeId, m_slideId;
};
}
