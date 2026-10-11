// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "action_model.h"
#include <QTextCursor>
#include <QVariantMap>
namespace QindaOffice::Quick { struct MenuNode; }
namespace QindaOffice::Apps::Write {
class PageTextControl;
class WriteDocument;
// Window-scoped Word graphic commands and the modal editor transaction.
class GraphicsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    GraphicsController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model, QObject *parent);
    [[nodiscard]] QList<Quick::MenuNode> insertItems() const;
    [[nodiscard]] QString error() const { return m_error; }
    Q_INVOKABLE bool insert(const QString &kind);
    Q_INVOKABLE QVariantMap info() const;
    Q_INVOKABLE bool apply(const QVariantMap &answer);
    Q_INVOKABLE bool open();
    Q_INVOKABLE void clearError() { if (!m_error.isEmpty()) { m_error.clear(); emit errorChanged(); } }
    bool editAt(int position);
    void sync();
signals:
    void dialogRequested(const QVariantMap &info);
    void errorChanged();
    void message(const QString &text);
private:
    [[nodiscard]] QTextCursor selected() const;
    bool fail(const QString &error);
    PageTextControl *m_control;
    WriteDocument *m_document;
    Quick::ActionEntry *m_edit;
    QTextCursor m_pending;
    QTextCharFormat m_original;
    QString m_error;
};
} // namespace QindaOffice::Apps::Write
