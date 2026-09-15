// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_find_bar.h"

#include "spreadsheet_document.h"
#include "spreadsheet_view.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

namespace QindaOffice::Apps::QindaCalc {

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

CalcFindBar::CalcFindBar(SpreadsheetDocument *document, SpreadsheetView *view, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_view(view)
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

    m_findEdit->setObjectName(QStringLiteral("calcFindEdit"));
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->installEventFilter(this);

    connect(next, &QToolButton::clicked, this, [this] { Q_UNUSED(findIn(false)); });
    connect(prev, &QToolButton::clicked, this, [this] { Q_UNUSED(findIn(true)); });
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { Q_UNUSED(findIn(false)); });
    connect(close, &QToolButton::clicked, this, [this] {
        hide();
        m_view->setFocus();
    });
}

void CalcFindBar::openFind()
{
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void CalcFindBar::findNext()
{
    if (isHidden()) {
        openFind();
        return;
    }
    Q_UNUSED(findIn(false));
}

void CalcFindBar::findPrevious()
{
    if (isHidden()) {
        openFind();
        return;
    }
    Q_UNUSED(findIn(true));
}

bool CalcFindBar::findIn(bool backward)
{
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) {
        return false;
    }
    const Qt::CaseSensitivity cs =
        m_caseCheck->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;

    // Row-major candidate list from the sparse cell hash, sorted so
    // next/previous are deterministic.
    const CalcEng::Sheet &sheet =
        m_document->workbook()->sheetAt(m_document->activeSheet());
    QList<CalcEng::CellAddr> hits;
    for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
        const CalcEng::CellAddr addr{qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)};
        const QString input = m_document->inputTextAt(addr);
        const QString display = m_document->displayTextAt(addr);
        if (input.contains(needle, cs) || display.contains(needle, cs)) {
            hits.append(addr);
        }
    }
    std::sort(hits.begin(), hits.end(), [](const CalcEng::CellAddr &a, const CalcEng::CellAddr &b) {
        return a.row != b.row ? a.row < b.row : a.col < b.col;
    });
    if (hits.isEmpty()) {
        emit message(tr("Not found: %1").arg(needle));
        return false;
    }
    const CalcEng::CellAddr cursor = m_view->cursorCell();
    CalcEng::CellAddr next;
    if (backward) {
        next = hits.last();
        for (auto it = hits.crbegin(); it != hits.crend(); ++it) {
            if (it->row < cursor.row || (it->row == cursor.row && it->col < cursor.col)) {
                next = *it;
                break;
            }
        }
    } else {
        next = hits.first();
        for (const CalcEng::CellAddr &hit : hits) {
            if (hit.row > cursor.row || (hit.row == cursor.row && hit.col > cursor.col)) {
                next = hit;
                break;
            }
        }
    }
    if (next == cursor) {
        emit message(tr("Search wrapped around the sheet"));
    }
    m_view->setCursorCell(next);
    return true;
}

void CalcFindBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        m_view->setFocus();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool CalcFindBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_findEdit && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            hide();
            m_view->setFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace QindaOffice::Apps::QindaCalc
