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
    , m_replaceEdit(new QLineEdit(this))
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
    auto *replaceLabel = new QLabel(tr("Replace:"), this);
    QToolButton *replaceOne = barButton(tr("Replace"), QStringLiteral("edit-find-replace"), this);
    QToolButton *replaceAllButton = barButton(tr("All"), QStringLiteral("edit-find-replace"), this);
    m_replaceEdit->setObjectName(QStringLiteral("calcReplaceEdit"));
    m_replaceEdit->setClearButtonEnabled(true);
    replaceOne->setObjectName(QStringLiteral("calcReplaceButton"));
    replaceAllButton->setObjectName(QStringLiteral("calcReplaceAllButton"));
    layout->addWidget(replaceLabel);
    layout->addWidget(m_replaceEdit, 2);
    layout->addWidget(replaceOne);
    layout->addWidget(replaceAllButton);
    layout->addWidget(m_caseCheck);
    layout->addWidget(close);
    // The replace controls are one row that Ctrl+F hides and Ctrl+H reveals.
    m_replaceRow = {replaceLabel, m_replaceEdit, replaceOne, replaceAllButton};
    setReplaceVisible(false);
    connect(replaceOne, &QToolButton::clicked, this, &CalcFindBar::replaceCurrent);
    connect(replaceAllButton, &QToolButton::clicked, this, &CalcFindBar::replaceAll);
    connect(m_replaceEdit, &QLineEdit::returnPressed, this, &CalcFindBar::replaceCurrent);

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

void CalcFindBar::setReplaceVisible(bool visible)
{
    for (QWidget *widget : m_replaceRow) {
        widget->setVisible(visible);
    }
}

void CalcFindBar::openFind()
{
    setReplaceVisible(false);
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void CalcFindBar::openReplace()
{
    setReplaceVisible(true);
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

QList<CalcEng::CellAddr> CalcFindBar::matches() const
{
    const QString needle = m_findEdit->text();
    QList<CalcEng::CellAddr> hits;
    if (needle.isEmpty()) {
        return hits;
    }
    const Qt::CaseSensitivity cs =
        m_caseCheck->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    // Row-major candidate list from the sparse cell hash, sorted so
    // next/previous (and Replace All) are deterministic.
    const CalcEng::Sheet &sheet =
        m_document->workbook()->sheetAt(m_document->activeSheet());
    for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
        const CalcEng::CellAddr addr{qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)};
        if (m_document->inputTextAt(addr).contains(needle, cs)
            || m_document->displayTextAt(addr).contains(needle, cs)) {
            hits.append(addr);
        }
    }
    std::sort(hits.begin(), hits.end(), [](const CalcEng::CellAddr &a, const CalcEng::CellAddr &b) {
        return a.row != b.row ? a.row < b.row : a.col < b.col;
    });
    return hits;
}

void CalcFindBar::replaceCurrent()
{
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) {
        return;
    }
    const Qt::CaseSensitivity cs =
        m_caseCheck->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    // AGENT-GUARD: replace in the INPUT text, never the displayed text. The
    // display is formatted output (a number format, a formula's result); the
    // input is what the user typed and what round-trips to the file.
    const CalcEng::CellAddr cursor = m_view->cursorCell();
    const QString input = m_document->inputTextAt(cursor);
    if (!input.contains(needle, cs)) {
        Q_UNUSED(findIn(false)); // not on a match: go to the next one
        return;
    }
    QString replaced = input;
    replaced.replace(needle, m_replaceEdit->text(), cs);
    m_document->applyInput(cursor, replaced);
    m_view->refreshAll();
    emit message(tr("Replaced in %1").arg(addrText(cursor)));
    Q_UNUSED(findIn(false));
}

void CalcFindBar::replaceAll()
{
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) {
        return;
    }
    const Qt::CaseSensitivity cs =
        m_caseCheck->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QList<QPair<CalcEng::CellAddr, QString>> edits;
    for (const CalcEng::CellAddr &addr : matches()) {
        const QString input = m_document->inputTextAt(addr);
        if (!input.contains(needle, cs)) {
            continue; // matched on display text only; the input has nothing to change
        }
        QString replaced = input;
        replaced.replace(needle, m_replaceEdit->text(), cs);
        edits.append({addr, replaced});
    }
    if (edits.isEmpty()) {
        emit message(tr("Not found: %1").arg(needle));
        return;
    }
    // One undo step for the whole sweep.
    m_document->applyInputs(edits, tr("Replace all"));
    m_view->refreshAll();
    emit message(tr("Replaced %n occurrence(s)", nullptr, int(edits.size())));
}

bool CalcFindBar::findIn(bool backward)
{
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) {
        return false;
    }
    const QList<CalcEng::CellAddr> hits = matches();
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
