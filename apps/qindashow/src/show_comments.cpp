// SPDX-License-Identifier: GPL-3.0-or-later
// Review comments in QindaShow (show_comments.h).

#include "show_comments.h"

#include "action_model.h"
#include "menu_tree.h"
#include "show_commands.h"
#include "show_document.h"
#include "show_host.h"
#include "slide_canvas_item.h"

#include <QLocale>
#include <QUndoStack>
#include <QUuid>

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace CommentId = CommentActionId;

namespace {

QString initialsOf(const QString &name)
{
    QString out;
    for (const QString &word : name.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        out += word.front().toUpper();
    }
    return out.left(3);
}

QVariantMap entry(const ShowModel::SlideComment &c)
{
    return {{QStringLiteral("id"), c.id},
            {QStringLiteral("author"), c.author},
            {QStringLiteral("initials"), c.initials},
            {QStringLiteral("date"), QLocale().toString(c.created.toLocalTime(), QLocale::ShortFormat)},
            {QStringLiteral("text"), c.text},
            {QStringLiteral("done"), c.resolved}};
}

// Every thread of the deck in order: slide row, comment id.
QList<std::pair<int, QString>> threads(const ShowModel::Presentation &deck)
{
    QList<std::pair<int, QString>> out;
    for (qsizetype i = 0; i < deck.slides.size(); ++i) {
        for (const ShowModel::SlideComment &c : deck.slides.at(i).comments) {
            if (!c.isReply()) {
                out.append({int(i), c.id});
            }
        }
    }
    return out;
}

} // namespace

ShowComments::ShowComments(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    Quick::ActionModel *model = host->actions();
    const auto make = [this, model](const char *id, const QString &text, const char *icon) {
        ActionEntry *e = model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
        m_actions.insert(QString::fromLatin1(id), e);
        return e;
    };
    connect(make(CommentId::kNew, tr("New &Comment"), "message-square-plus"), &ActionEntry::triggered, this,
            [this] { emit paneRequested(true, true); });
    connect(make(CommentId::kDelete, tr("&Delete Comment"), "message-square-x"), &ActionEntry::triggered, this,
            [this] { remove(m_active); });
    connect(make(CommentId::kPrevious, tr("Pre&vious Comment"), "chevron-up"), &ActionEntry::triggered, this,
            &ShowComments::previous);
    connect(make(CommentId::kNext, tr("Ne&xt Comment"), "chevron-down"), &ActionEntry::triggered, this, &ShowComments::next);
    ActionEntry *pane = make(CommentId::kPane, tr("Show Co&mments"), "message-square");
    pane->setCheckable(true);
    connect(pane, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            emit paneRequested(on, false);
        }
    });
    connect(host, &ShowHost::deckChanged, this, [this] { sync(); });
    connect(host, &ShowHost::currentSlideChanged, this, [this] { sync(); });
    connect(host->canvas(), &SlideCanvasItem::selectionChanged, this, [this] { emit changed(); });
    sync();
}

QList<MenuNode> ShowComments::menuItems() const
{
    QList<MenuNode> items;
    for (const char *id : {CommentId::kNew, CommentId::kDelete, CommentId::kPrevious, CommentId::kNext, CommentId::kPane}) {
        items.append(MenuNode::action(QString::fromLatin1(id)));
    }
    items.insert(2, MenuNode::separator());
    items.insert(5, MenuNode::separator());
    return items;
}

QString ShowComments::author() const
{
    const QString user = QString::fromLocal8Bit(qgetenv("USER"));
    return user.isEmpty() ? tr("Author") : user;
}

QVariantList ShowComments::comments() const
{
    QVariantList list;
    const ShowModel::Slide *slide = m_host->currentSlide();
    if (slide == nullptr) {
        return list;
    }
    QHash<QString, qsizetype> threadAt;
    for (const ShowModel::SlideComment &c : slide->comments) {
        if (!c.isReply()) {
            QVariantMap thread = entry(c);
            const ShowModel::Shape *shape = nullptr;
            for (const ShowModel::Shape &s : slide->shapes) {
                if (!c.shapeId.isEmpty() && s.id == c.shapeId) {
                    shape = &s;
                }
            }
            thread.insert(QStringLiteral("anchor"), shape != nullptr ? (shape->name.isEmpty() ? tr("a shape") : shape->name)
                                                                     : tr("this slide"));
            thread.insert(QStringLiteral("replies"), QVariantList());
            threadAt.insert(c.id, list.size());
            list.append(thread);
        }
    }
    for (const ShowModel::SlideComment &c : slide->comments) {
        if (c.isReply() && threadAt.contains(c.parentId)) {
            QVariantMap thread = list.at(threadAt.value(c.parentId)).toMap();
            QVariantList replies = thread.value(QStringLiteral("replies")).toList();
            replies.append(entry(c));
            thread.insert(QStringLiteral("replies"), replies);
            list[threadAt.value(c.parentId)] = thread;
        }
    }
    return list;
}

int ShowComments::count() const
{
    return int(comments().size());
}

int ShowComments::total() const
{
    return int(threads(*m_host->showDocument()->presentation()).size());
}

bool ShowComments::canAdd() const
{
    return m_host->currentSlide() != nullptr && !m_host->masterViewActive();
}

QString ShowComments::anchorName() const
{
    const ShowModel::Shape *shape = m_host->canvas()->selectionCount() == 1 ? m_host->canvas()->selectedShape() : nullptr;
    if (shape == nullptr) {
        return tr("this slide");
    }
    return shape->name.isEmpty() ? tr("the selected shape") : shape->name;
}

bool ShowComments::changeSlide(const QString &label, const std::function<bool(ShowModel::Slide &)> &mutate)
{
    ShowModel::Slide *slide = m_host->currentSlide();
    if (slide == nullptr || m_host->masterViewActive()) {
        return false;
    }
    const ShowModel::Slide before = *slide;
    ShowModel::Slide after = before;
    if (!mutate(after) || after == before) {
        return false;
    }
    *slide = after;
    auto *command = new SlideStateCommand(m_host->showDocument()->presentation(), slide->id, before, after);
    command->setText(label);
    m_host->showDocument()->undoStack()->push(command);
    m_host->showDocument()->setDirty(true);
    m_host->showDocument()->notifyContentChanged();
    sync();
    return true;
}

QString ShowComments::add(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return {};
    }
    ShowModel::SlideComment comment;
    comment.id = QUuid::createUuid().toString(QUuid::WithBraces).toUpper();
    comment.author = author();
    comment.initials = initialsOf(comment.author);
    comment.created = QDateTime::currentDateTimeUtc();
    comment.text = text.trimmed();
    if (const ShowModel::Shape *shape = m_host->canvas()->selectionCount() == 1 ? m_host->canvas()->selectedShape() : nullptr) {
        comment.shapeId = shape->id;
    }
    if (!changeSlide(tr("New comment"), [&](ShowModel::Slide &slide) {
            slide.comments.append(comment);
            return true;
        })) {
        return {};
    }
    m_active = comment.id;
    emit activeChanged();
    return comment.id;
}

QString ShowComments::reply(const QString &id, const QString &text)
{
    ShowModel::SlideComment answer;
    answer.id = QUuid::createUuid().toString(QUuid::WithBraces).toUpper();
    answer.author = author();
    answer.initials = initialsOf(answer.author);
    answer.created = QDateTime::currentDateTimeUtc();
    answer.text = text.trimmed();
    answer.parentId = id;
    const bool done = !answer.text.isEmpty() && changeSlide(tr("Reply"), [&](ShowModel::Slide &slide) {
        qsizetype at = -1;
        for (qsizetype i = 0; i < slide.comments.size(); ++i) {
            const ShowModel::SlideComment &c = slide.comments.at(i);
            if (c.id == id || c.parentId == id) {
                at = i; // after the thread's last reply
                answer.shapeId = c.shapeId;
                answer.resolved = c.resolved;
            }
        }
        if (at < 0) {
            return false;
        }
        slide.comments.insert(at + 1, answer);
        return true;
    });
    return done ? answer.id : QString();
}

bool ShowComments::edit(const QString &id, const QString &text)
{
    return !text.trimmed().isEmpty() && changeSlide(tr("Edit comment"), [&](ShowModel::Slide &slide) {
        for (ShowModel::SlideComment &c : slide.comments) {
            if (c.id == id) {
                c.text = text.trimmed();
                return true;
            }
        }
        return false;
    });
}

bool ShowComments::setDone(const QString &id, bool done)
{
    // A thread is resolved as a whole, replies with it.
    return changeSlide(done ? tr("Resolve comment") : tr("Reopen comment"), [&](ShowModel::Slide &slide) {
        bool found = false;
        for (ShowModel::SlideComment &c : slide.comments) {
            if (c.id == id || c.parentId == id) {
                c.resolved = done;
                found = true;
            }
        }
        return found;
    });
}

bool ShowComments::remove(const QString &id)
{
    if (id.isEmpty()) {
        return false;
    }
    const bool removed = changeSlide(tr("Delete comment"), [&](ShowModel::Slide &slide) {
        const qsizetype before = slide.comments.size();
        slide.comments.removeIf([&id](const ShowModel::SlideComment &c) { return c.id == id || c.parentId == id; });
        return slide.comments.size() != before;
    });
    if (removed && m_active == id) {
        m_active.clear();
        emit activeChanged();
    }
    return removed;
}

bool ShowComments::select(const QString &id)
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    for (qsizetype row = 0; row < deck->slides.size(); ++row) {
        for (const ShowModel::SlideComment &c : deck->slides.at(row).comments) {
            if (c.id != id) {
                continue;
            }
            if (row != m_host->currentSlideRow()) {
                m_host->selectSlideRow(int(row));
            }
            m_host->canvas()->clearSelection();
            if (!c.shapeId.isEmpty()) {
                m_host->canvas()->setSelected(c.shapeId, true);
            }
            m_active = id;
            emit activeChanged();
            sync();
            return true;
        }
    }
    return false;
}

bool ShowComments::step(int direction)
{
    const QList<std::pair<int, QString>> all = threads(*m_host->showDocument()->presentation());
    if (all.isEmpty()) {
        return false;
    }
    qsizetype at = -1;
    for (qsizetype i = 0; i < all.size(); ++i) {
        if (all.at(i).second == m_active) {
            at = i;
        }
    }
    if (at < 0) {
        // From the current slide on (or back).
        const int row = m_host->currentSlideRow();
        at = direction > 0 ? -1 : all.size();
        for (qsizetype i = 0; i < all.size(); ++i) {
            if (direction > 0 ? all.at(i).first < row : all.at(i).first <= row) {
                at = direction > 0 ? i : i + 1;
            }
        }
    }
    const qsizetype next = (at + direction + all.size()) % all.size();
    return select(all.at(next).second);
}

bool ShowComments::next()
{
    return step(1);
}

bool ShowComments::previous()
{
    return step(-1);
}

void ShowComments::sync()
{
    m_syncing = true;
    const bool any = total() > 0;
    m_actions.value(QString::fromLatin1(CommentId::kNew))->setEnabled(canAdd());
    m_actions.value(QString::fromLatin1(CommentId::kDelete))->setEnabled(!m_active.isEmpty());
    m_actions.value(QString::fromLatin1(CommentId::kPrevious))->setEnabled(any);
    m_actions.value(QString::fromLatin1(CommentId::kNext))->setEnabled(any);
    m_syncing = false;
    emit changed();
}

} // namespace QindaOffice::Apps::Show
