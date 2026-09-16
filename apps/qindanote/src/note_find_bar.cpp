// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_find_bar.h"

#include "note_window.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

namespace QindaOffice::Apps::Notes {

namespace {

QToolButton *barButton(const QString &text, const QString &icon, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setIcon(QIcon::fromTheme(icon));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    return button;
}

// Title plus every typed text block of one page. Ink is not text and is
// deliberately not searched (handwriting recognition is not in this suite).
QString pageText(const NotePage &page)
{
    QString text = page.title;
    const QJsonArray blocks = QJsonDocument::fromJson(page.envelope.blocksJson)
                                  .object()
                                  .value(QStringLiteral("blocks"))
                                  .toArray();
    for (const QJsonValue &value : blocks) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("type")).toString() == QLatin1String("text")) {
            text += QLatin1Char('\n') + object.value(QStringLiteral("text")).toString();
        }
    }
    return text;
}

} // namespace

NoteFindBar::NoteFindBar(NoteDocument *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
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

    m_findEdit->setObjectName(QStringLiteral("noteFindEdit"));
    m_findEdit->setClearButtonEnabled(true);
    next->setObjectName(QStringLiteral("noteFindNextButton"));
    prev->setObjectName(QStringLiteral("noteFindPreviousButton"));

    connect(next, &QToolButton::clicked, this, [this] { Q_UNUSED(findIn(false)); });
    connect(prev, &QToolButton::clicked, this, [this] { Q_UNUSED(findIn(true)); });
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { Q_UNUSED(findIn(false)); });
    connect(close, &QToolButton::clicked, this, [this] { hide(); });
}

void NoteFindBar::setCurrentPage(const QString &sectionId, const QString &pageId)
{
    m_currentSectionId = sectionId;
    m_currentPageId = pageId;
}

void NoteFindBar::openFind()
{
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void NoteFindBar::findNext()
{
    if (isHidden()) {
        openFind();
        return;
    }
    Q_UNUSED(findIn(false));
}

void NoteFindBar::findPrevious()
{
    if (isHidden()) {
        openFind();
        return;
    }
    Q_UNUSED(findIn(true));
}

QList<NoteFindBar::Hit> NoteFindBar::matches() const
{
    QList<Hit> hits;
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) {
        return hits;
    }
    const Qt::CaseSensitivity cs =
        m_caseCheck->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    Notebook *notebook = m_document->notebook();
    const QList<QString> sectionIds = [notebook] {
        QList<QString> ids;
        for (const NoteSection &section : notebook->sections()) {
            ids.append(section.id);
        }
        return ids;
    }();
    for (const QString &sectionId : sectionIds) {
        NoteSection *section = notebook->sectionById(sectionId);
        if (section == nullptr) {
            continue;
        }
        for (NotePage &page : section->pages) {
            if (!page.loaded && notebook->isOpen()) {
                static_cast<void>(notebook->loadPage(*section, page, nullptr));
            }
            if (pageText(page).contains(needle, cs)) {
                hits.append({sectionId, page.id});
            }
        }
    }
    return hits;
}

bool NoteFindBar::findIn(bool backward)
{
    const QList<Hit> hits = matches();
    if (hits.isEmpty()) {
        emit message(tr("Not found: %1").arg(m_findEdit->text()));
        return false;
    }
    int current = -1;
    for (int i = 0; i < hits.size(); ++i) {
        if (hits.at(i).pageId == m_currentPageId) {
            current = i;
            break;
        }
    }
    // Wrap in both directions; with the cursor off the hit list, Next starts
    // at the first match and Previous at the last.
    int index = 0;
    if (current < 0) {
        index = backward ? hits.size() - 1 : 0;
    } else {
        index = backward ? (current - 1 + hits.size()) % hits.size()
                         : (current + 1) % hits.size();
    }
    const Hit &hit = hits.at(index);
    emit pageRequested(hit.sectionId, hit.pageId);
    emit message(tr("Match %1 of %2").arg(index + 1).arg(hits.size()));
    return true;
}

void NoteFindBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace QindaOffice::Apps::Notes
