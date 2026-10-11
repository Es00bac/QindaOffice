// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Review comments in QindaShow (M75, D-084), as PowerPoint's Review ▸
// Comments and like QindaWrite's (D-068): the Comments pane lists the
// current slide's threads with their replies; New Comment goes on the
// selected shape (else on the slide), a card replies, edits, resolves
// (reopens) and deletes; Previous / Next walk every thread of the deck,
// showing its slide and selecting its shape. Each change is ONE undo step
// (SlideStateCommand). The file side is show_model's pptx_review.h:
// PowerPoint's modern comments, or legacy ones in a deck that has them.
//
// AGENT-CONTRACT: the command ids (CommentActionId) are the dbusmenu
// activation identity (AGENTS.md rule 5): never renamed, never reused.

#include "presentation.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>
#include <QVariantList>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Quick {
struct MenuNode;
class ActionEntry;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

namespace CommentActionId {
inline const char kNew[] = "showNewCommentAction";
inline const char kDelete[] = "showDeleteCommentAction";
inline const char kPrevious[] = "showPreviousCommentAction";
inline const char kNext[] = "showNextCommentAction";
inline const char kPane[] = "showCommentsPaneAction";
} // namespace CommentActionId

class ShowComments final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the QindaShow host.")
    // [{id, author, initials, date, text, done, anchor, replies: [{…}]}]
    Q_PROPERTY(QVariantList comments READ comments NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(QString activeId READ activeId NOTIFY activeChanged)
    Q_PROPERTY(bool canAdd READ canAdd NOTIFY changed)
    // What a new comment goes on: the selected shape's name, or the slide.
    Q_PROPERTY(QString anchorName READ anchorName NOTIFY changed)
public:
    explicit ShowComments(ShowHost *host);

    [[nodiscard]] QVariantList comments() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] int total() const;
    [[nodiscard]] QString activeId() const { return m_active; }
    [[nodiscard]] bool canAdd() const;
    [[nodiscard]] QString anchorName() const;
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;
    // The name new comments are made under ($USER, as QindaWrite's).
    [[nodiscard]] QString author() const;

    Q_INVOKABLE QString add(const QString &text);
    Q_INVOKABLE QString reply(const QString &id, const QString &text);
    Q_INVOKABLE bool edit(const QString &id, const QString &text);
    // Move a thread onto a slide point (clears its shape anchor), one undo step.
    Q_INVOKABLE bool setPosition(const QString &id, qreal x, qreal y);
    Q_INVOKABLE bool setDone(const QString &id, bool done);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool select(const QString &id);
    Q_INVOKABLE bool next();
    Q_INVOKABLE bool previous();

signals:
    void changed();
    void activeChanged();
    // Show (or hide) the pane; `compose` puts the caret in New Comment.
    void paneRequested(bool visible, bool compose);

private:
    bool changeSlide(const QString &label, const std::function<bool(ShowModel::Slide &)> &mutate);
    bool step(int direction);
    void sync();

    ShowHost *m_host;
    QString m_active;
    QHash<QString, Quick::ActionEntry *> m_actions;
    bool m_syncing = false;
};

} // namespace QindaOffice::Apps::Show
