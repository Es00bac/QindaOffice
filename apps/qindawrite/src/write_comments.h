// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Review comments in QindaWrite, as in Word (the file side is
// docx_comments.h). A comment is a range of text — each run in it carries
// the comment's id (Docx::kCommentIds) — its mark, a zero-width character
// right after the range (Docx::kCommentReference), and its entry in
// WordParts::comments: author, date, text, the comment it answers, resolved.
// This controller adds comments to the selection, answers, edits,
// resolves and deletes them, lists them for the Comments pane in text order
// with their replies, and selects a comment's text. Each change to the
// text is one undo step; the list follows the text (a comment whose mark is
// gone — deleted or undone — is neither listed nor saved).

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <functional>

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class WriteComments final : public QObject {
    Q_OBJECT
    // [{id, author, initials, date, text, done, quote, replies: [{…}]}]
    Q_PROPERTY(QVariantList comments READ comments NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QString activeId READ activeId NOTIFY activeChanged)
    // A selection in the body text a new comment can go on.
    Q_PROPERTY(bool canAdd READ canAdd NOTIFY canAddChanged)
public:
    // `author` names the person commenting: the name tracked changes are
    // made under (write_revisions.h).
    WriteComments(WriteDocument *document, PageTextControl *control, std::function<QString()> author,
                  QObject *parent = nullptr);

    [[nodiscard]] QVariantList comments() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] QString activeId() const;
    [[nodiscard]] bool canAdd() const;

    // On the selection (the word at the caret when nothing is selected);
    // returns the new comment's id, empty when there is no body text there.
    Q_INVOKABLE QString add(const QString &text);
    Q_INVOKABLE QString reply(const QString &id, const QString &text);
    Q_INVOKABLE bool edit(const QString &id, const QString &text);
    Q_INVOKABLE bool setDone(const QString &id, bool done);
    // The comment with its replies: their ids leave the text, their marks
    // are deleted.
    Q_INVOKABLE bool remove(const QString &id);
    // Selects the comment's text (its mark when it has none) and makes it
    // the active one.
    Q_INVOKABLE bool select(const QString &id);
    Q_INVOKABLE bool next();
    Q_INVOKABLE bool previous();

signals:
    void changed();
    void activeChanged();
    void canAddChanged();
    void message(const QString &text);

private:
    void followCaret();
    [[nodiscard]] QStringList orderedTopLevel() const;
    [[nodiscard]] QString authorInitials(const QString &author) const;
    bool step(int direction);

    WriteDocument *m_document;
    PageTextControl *m_control;
    std::function<QString()> m_author;
    QTimer m_refresh;
    bool m_canAdd = false;
};

} // namespace QindaOffice::Apps::Write
