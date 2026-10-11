// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "action_model.h"
#include "math_editor.h"
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Quick {
// One window's private draft. Accepting emits native OMML; the app adapter
// owns the document undo transaction. Cancel never changes the document.
// GUI thread only; ActionModel must outlive this object.
class EquationSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the document window.")
    Q_PROPERTY(QString linearText READ linearText NOTIFY changed)
    Q_PROPERTY(QString latexText READ latexText NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QStringList templateIds READ templateIds CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(qreal pointSize READ pointSize NOTIFY changed)
public:
    explicit EquationSession(ActionModel *actions, QObject *parent = nullptr);
    [[nodiscard]] Math::EquationEditor *editor() { return &m_editor; }
    [[nodiscard]] QString linearText() const;
    [[nodiscard]] QString latexText() const;
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] qreal pointSize() const { return m_pointSize; }
    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] QStringList templateIds() const { return m_templates; }
    [[nodiscard]] QString resultXml() const;
    Q_INVOKABLE QindaOffice::Quick::ActionEntry *action(const QString &id) const;
    // Invalid XML is rejected visibly, with no draft/document change.
    bool begin(const QString &xml = {}, qreal pointSize = 18);
    Q_INVOKABLE bool convert(const QString &source, bool latex);
    Q_INVOKABLE void accept();
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void activeChanged();
    void requested();
    void accepted(const QString &xml);
    void focusRequested();
    void buildRequested();
private:
    void syncActions();
    ActionModel *m_actions;
    Math::EquationEditor m_editor;
    QStringList m_templates;
    QString m_original, m_initial, m_error;
    qreal m_pointSize = 18;
    bool m_active = false;
};
}
