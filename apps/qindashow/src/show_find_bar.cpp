// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_find_bar.h"

#include "show_document.h"
#include "show_window.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

namespace QindaOffice::Apps::Show {

namespace {

QToolButton *barButton(const QString &text, const QString &icon, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setIcon(QIcon::fromTheme(icon));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    return button;
}

// All searchable text of one slide: title, every run of every shape, notes.
QString slideText(const ShowModel::Slide &slide)
{
    QString text = slide.title;
    for (const ShowModel::Shape &shape : slide.shapes) {
        for (const ShowModel::TextParagraph &paragraph : shape.paragraphs) {
            for (const ShowModel::TextRun &run : paragraph.runs) {
                text += QLatin1Char('\n') + run.text;
            }
        }
    }
    return text + QLatin1Char('\n') + slide.notes;
}

} // namespace

ShowFindBar::ShowFindBar(ShowWindow *window, QWidget *parent)
    : QWidget(parent)
    , m_window(window)
    , m_findEdit(new QLineEdit(this))
    , m_caseCheck(new QCheckBox(tr("Case sensitive"), this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->addWidget(new QLabel(tr("Find:"), this));
    layout->addWidget(m_findEdit, 2);
    QToolButton *next = barButton(tr("Next"), QStringLiteral("go-down"), this);
    QToolButton *prev = barButton(tr("Previous"), QStringLiteral("go-up"), this);
    QToolButton *close = barButton(tr("Close"), QStringLiteral("window-close"), this);
    layout->addWidget(next);
    layout->addWidget(prev);
    layout->addWidget(m_caseCheck);
    layout->addWidget(close);

    m_findEdit->setObjectName(QStringLiteral("showFindEdit"));
    m_findEdit->setClearButtonEnabled(true);
    next->setObjectName(QStringLiteral("showFindNextButton"));
    prev->setObjectName(QStringLiteral("showFindPreviousButton"));

    connect(next, &QToolButton::clicked, this, [this] { Q_UNUSED(findIn(false)); });
    connect(prev, &QToolButton::clicked, this, [this] { Q_UNUSED(findIn(true)); });
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { Q_UNUSED(findIn(false)); });
    connect(close, &QToolButton::clicked, this, [this] { hide(); });
}

void ShowFindBar::openFind()
{
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void ShowFindBar::findNext()
{
    if (isHidden()) {
        openFind();
        return;
    }
    Q_UNUSED(findIn(false));
}

void ShowFindBar::findPrevious()
{
    if (isHidden()) {
        openFind();
        return;
    }
    Q_UNUSED(findIn(true));
}

QList<int> ShowFindBar::matches() const
{
    QList<int> hits;
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) {
        return hits;
    }
    const Qt::CaseSensitivity cs =
        m_caseCheck->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const ShowModel::Presentation *deck = m_window->showDocument()->presentation();
    for (int i = 0; i < deck->slides.size(); ++i) {
        if (slideText(deck->slides.at(i)).contains(needle, cs)) {
            hits.append(i);
        }
    }
    return hits;
}

bool ShowFindBar::findIn(bool backward)
{
    const QList<int> hits = matches();
    if (hits.isEmpty()) {
        emit message(tr("Not found: %1").arg(m_findEdit->text()));
        return false;
    }
    const int current = m_window->currentSlideRow();
    int next = backward ? hits.last() : hits.first();
    if (backward) {
        for (auto it = hits.crbegin(); it != hits.crend(); ++it) {
            if (*it < current) {
                next = *it;
                break;
            }
        }
    } else {
        for (const int hit : hits) {
            if (hit > current) {
                next = hit;
                break;
            }
        }
    }
    if (next == current && hits.size() == 1) {
        emit message(tr("Only match is the current slide"));
    }
    emit slideRequested(next);
    emit message(tr("Slide %1 of %2 matching").arg(next + 1).arg(hits.size()));
    return true;
}

void ShowFindBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace QindaOffice::Apps::Show
