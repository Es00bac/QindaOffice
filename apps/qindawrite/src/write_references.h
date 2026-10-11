// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "action_model.h"
#include <QTextCursor>
#include <QVariantMap>
namespace QindaOffice::Quick { struct MenuNode; }
namespace QindaOffice::Apps::Write {
class PageTextControl;
class WriteDocument;
// Window-scoped References commands. Edits are body-only, validated before
// mutation and grouped with the document's native text undo stack.
class ReferencesController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(QString style READ style NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    ReferencesController(PageTextControl *, WriteDocument *, Quick::ActionModel *, QObject *parent);
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;
    [[nodiscard]] QVariantList sources() const;
    [[nodiscard]] QString style() const;
    [[nodiscard]] QString error() const { return m_error; }
    Q_INVOKABLE bool saveSource(const QString &oldTag, const QVariantMap &values);
    Q_INVOKABLE bool deleteSource(const QString &tag);
    Q_INVOKABLE bool setStyle(const QString &style);
    Q_INVOKABLE bool insertCitation(const QString &tag, const QString &pages = QString());
    Q_INVOKABLE QVariantMap citationInfo() const;
    Q_INVOKABLE bool editCitation(const QString &tag, const QString &pages);
    Q_INVOKABLE bool insertBibliography();
    Q_INVOKABLE bool insertCaption(const QString &label, const QString &text);
    Q_INVOKABLE bool insertCrossReference(const QString &bookmark, const QString &kind, bool link);
    Q_INVOKABLE QVariantList targets() const;
    Q_INVOKABLE QVariantMap tocOptions() const;
    Q_INVOKABLE bool applyToc(const QVariantMap &options);
    Q_INVOKABLE bool refresh();
    Q_INVOKABLE bool follow(const QString &bookmark = QString());
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void request(const QString &kind);
signals:
    void changed();
    void errorChanged();
    void dialogRequested(const QString &kind);
    void message(const QString &message);
private:
    bool fail(const QString &error);
    bool body() const;
    void finish(QTextCursor cursor);
    void updateBoundaryFormat();
    PageTextControl *m_control;
    WriteDocument *m_document;
    QString m_error;
    QList<Quick::ActionEntry *> m_actions;
};
} // namespace QindaOffice::Apps::Write
