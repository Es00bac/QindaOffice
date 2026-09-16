// SPDX-License-Identifier: GPL-3.0-or-later
#include "find_replace_bar.h"

#include "page_text_view.h"
#include "write_document.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTextCursor>
#include <QToolButton>
#include <QtWidgets/private/qwidgettextcontrol_p.h>

namespace QindaOffice::Apps::Write {

namespace {

QToolButton *barButton(const QString &text, const QString &icon, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setIcon(QIcon::fromTheme(icon));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    return button;
}

} // namespace

FindReplaceBar::FindReplaceBar(PageTextView *view, WriteDocument *document,
                               QWidget *parent)
    : QWidget(parent)
    , m_view(view)
    , m_document(document)
    , m_findEdit(new QLineEdit(this))
    , m_replaceEdit(new QLineEdit(this))
    , m_caseCheck(new QCheckBox(tr("Case sensitive"), this))
    , m_wordsCheck(new QCheckBox(tr("Whole words"), this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(new QLabel(tr("Find:"), this));
    layout->addWidget(m_findEdit, 2);

    QToolButton *next = barButton(tr("Next"), QStringLiteral("go-down"), this);
    QToolButton *prev = barButton(tr("Previous"), QStringLiteral("go-up"), this);
    layout->addWidget(next);
    layout->addWidget(prev);
    layout->addWidget(m_caseCheck);
    layout->addWidget(m_wordsCheck);

    layout->addSpacing(12);
    layout->addWidget(new QLabel(tr("Replace:"), this));
    layout->addWidget(m_replaceEdit, 2);
    QToolButton *replaceOne = barButton(tr("Replace"), QStringLiteral("edit-find-replace"), this);
    QToolButton *replaceEvery = barButton(tr("All"), QStringLiteral("edit-find-replace"), this);
    layout->addWidget(replaceOne);
    layout->addWidget(replaceEvery);

    QToolButton *close = barButton(tr("Close"), QStringLiteral("window-close"), this);
    layout->addWidget(close);

    m_findEdit->installEventFilter(this);
    m_replaceEdit->installEventFilter(this);
    m_findEdit->setClearButtonEnabled(true);
    m_replaceEdit->setClearButtonEnabled(true);
    // Stable objectNames: tests and assistive tooling address the fields.
    m_findEdit->setObjectName(QStringLiteral("findEdit"));
    m_replaceEdit->setObjectName(QStringLiteral("replaceEdit"));

    connect(next, &QToolButton::clicked, this, [this] { Q_UNUSED(findIn(false)); });
    connect(prev, &QToolButton::clicked, this, [this] { Q_UNUSED(findIn(true)); });
    connect(m_findEdit, &QLineEdit::returnPressed, this,
            [this] { Q_UNUSED(findIn(false)); });
    connect(replaceOne, &QToolButton::clicked, this, [this] { replaceCurrent(); });
    connect(replaceEvery, &QToolButton::clicked, this, [this] { replaceAll(); });
    connect(close, &QToolButton::clicked, this, [this] {
        hide();
        m_view->setFocus();
    });
}

void FindReplaceBar::openFind()
{
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void FindReplaceBar::openReplace()
{
    show();
    m_replaceEdit->setFocus();
    m_replaceEdit->selectAll();
}

void FindReplaceBar::findNext()
{
    if (isHidden()) {
        openFind();
        return;
    }
    Q_UNUSED(findIn(false));
}

void FindReplaceBar::findPrevious()
{
    if (isHidden()) {
        openFind();
        return;
    }
    Q_UNUSED(findIn(true));
}

bool FindReplaceBar::findIn(bool backward)
{
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) {
        return false;
    }
    QWidgetTextControl *control = m_view->control();
    QTextDocument::FindFlags flags;
    if (m_caseCheck->isChecked()) {
        flags |= QTextDocument::FindCaseSensitively;
    }
    if (m_wordsCheck->isChecked()) {
        flags |= QTextDocument::FindWholeWords;
    }
    if (backward) {
        flags |= QTextDocument::FindBackward;
    }
    if (control->find(needle, flags)) {
        return true;
    }
    // Wrap around once, then report honestly. The user's caret is only
    // moved when the wrap actually finds something (the earlier release
    // jumped it to document start on any failed search).
    const QTextCursor restore = control->textCursor();
    QTextCursor cursor = control->textCursor();
    cursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    control->setTextCursor(cursor);
    if (control->find(needle, flags)) {
        emit message(tr("Search wrapped around the document"));
        return true;
    }
    control->setTextCursor(restore);
    emit message(tr("Not found: %1").arg(needle));
    return false;
}

void FindReplaceBar::replaceCurrent()
{
    QWidgetTextControl *control = m_view->control();
    QTextCursor cursor = control->textCursor();
    const QString needle = m_findEdit->text();
    const Qt::CaseSensitivity cs =
        m_caseCheck->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (cursor.hasSelection() && cursor.selectedText().compare(needle, cs) == 0) {
        cursor.insertText(m_replaceEdit->text());
        m_document->setDirty(true);
    }
    Q_UNUSED(findIn(false));
}

void FindReplaceBar::replaceAll()
{
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) {
        return;
    }
    QTextDocument *doc = m_document->textDocument();
    QTextDocument::FindFlags flags;
    if (m_caseCheck->isChecked()) {
        flags |= QTextDocument::FindCaseSensitively;
    }
    if (m_wordsCheck->isChecked()) {
        flags |= QTextDocument::FindWholeWords;
    }
    int count = 0;
    // AGENT-GUARD: the block cursor must stay VALID — doc->find() returning a
    // null cursor ended the loop and endEditBlock() ran on the null cursor,
    // a Qt no-op that left the document's editBlock counter stuck at 1 and
    // permanently broke undo grouping after the first Replace All.
    QTextCursor blockCursor(doc);
    blockCursor.beginEditBlock();
    QTextCursor cursor = doc->find(needle, QTextCursor(doc), flags);
    while (!cursor.isNull()) {
        cursor.insertText(m_replaceEdit->text());
        ++count;
        cursor = doc->find(needle, cursor, flags);
    }
    blockCursor.endEditBlock();
    if (count > 0) {
        m_document->setDirty(true);
    }
    emit message(tr("%n replacement(s) made", nullptr, count));
}

void FindReplaceBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        m_view->setFocus();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool FindReplaceBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            hide();
            m_view->setFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace QindaOffice::Apps::Write
