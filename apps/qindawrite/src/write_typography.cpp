// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_typography.h"

#include "page_text_view.h"
#include "write_document.h"

#include "standard_actions.h"

#include <QtWidgets/private/qwidgettextcontrol_p.h>

#include <QActionGroup>
#include <QColorDialog>
#include <QDateTime>
#include <QIcon>
#include <QInputDialog>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

namespace {

QAction *make(QObject *parent, const char *id, const QString &text,
              const QKeySequence &shortcut = {}, const char *iconName = nullptr)
{
    auto *action = new QAction(text, parent);
    action->setObjectName(QString::fromLatin1(id));
    if (iconName != nullptr) {
        action->setIcon(QIcon::fromTheme(QString::fromLatin1(iconName)));
    }
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    return action;
}

// A small, useful set rather than a full Unicode browser: the characters a
// document actually needs that no keyboard has a key for.
const char *const kSymbols[] = {"—", "–", "…", "•", "·", "©", "®", "™", "°",
                                "±", "×", "÷", "≈", "≠", "≤", "≥", "→", "←",
                                "€", "£", "¥", "¢", "§", "¶", "«", "»", "“",
                                "”", "‘", "’", "½", "¼", "¾", "α", "β", "π"};

} // namespace

WriteTypography::WriteTypography(PageTextView *view, WriteDocument *document, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_document(document)
{
    createActions();
    connect(m_view->control(), &QWidgetTextControl::cursorPositionChanged, this, [this] {
        syncFromCursor();
        emit countsChanged();
    });
    connect(m_document->textDocument(), &QTextDocument::contentsChanged, this,
            [this] { emit countsChanged(); });
    syncFromCursor();
}

void WriteTypography::createActions()
{
    using namespace Shell::ActionId;
    m_highlight = make(this, kFormatHighlight, tr("&Highlight Colour…"), {},
                       "format-fill-color");
    m_superscript = make(this, kFormatSuperscript, tr("Su&perscript"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+P")), "format-text-superscript");
    m_subscript = make(this, kFormatSubscript, tr("Su&bscript"),
                       QKeySequence(QStringLiteral("Ctrl+Shift+B")), "format-text-subscript");
    m_superscript->setCheckable(true);
    m_subscript->setCheckable(true);
    m_growFont = make(this, kFormatGrowFont, tr("&Grow Font"),
                      QKeySequence(QStringLiteral("Ctrl+Shift+>")), "format-font-size-more");
    m_shrinkFont = make(this, kFormatShrinkFont, tr("S&hrink Font"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+<")), "format-font-size-less");

    auto *spacingGroup = new QActionGroup(this);
    spacingGroup->setObjectName(QStringLiteral("writeLineSpacingGroup"));
    m_spacingSingle = make(this, kFormatLineSpacingSingle, tr("&Single"));
    m_spacingOneHalf = make(this, kFormatLineSpacingOneHalf, tr("1.&5 Lines"));
    m_spacingDouble = make(this, kFormatLineSpacingDouble, tr("&Double"));
    for (QAction *action : {m_spacingSingle, m_spacingOneHalf, m_spacingDouble}) {
        action->setCheckable(true);
        spacingGroup->addAction(action);
    }
    m_spacingSingle->setChecked(true);

    m_insertDate = make(this, kInsertDate, tr("&Date"));
    m_insertTime = make(this, kInsertTime, tr("&Time"));
    m_insertSymbol = make(this, kInsertSymbol, tr("&Special Character…"));
    m_insertRule = make(this, kInsertHorizontalRule, tr("Horizontal &Rule"));
    m_insertHyperlink = make(this, kInsertHyperlink, tr("&Hyperlink…"),
                             QKeySequence(QStringLiteral("Ctrl+K")), "insert-link");
    m_wordCount = make(this, kToolsWordCount, tr("&Word Count…"));

    connect(m_highlight, &QAction::triggered, this, &WriteTypography::pickHighlight);
    connect(m_superscript, &QAction::triggered, this, [this](bool on) {
        if (m_syncing) {
            return;
        }
        QTextCharFormat format;
        format.setVerticalAlignment(on ? QTextCharFormat::AlignSuperScript
                                       : QTextCharFormat::AlignNormal);
        applyCharFormat(format);
    });
    connect(m_subscript, &QAction::triggered, this, [this](bool on) {
        if (m_syncing) {
            return;
        }
        QTextCharFormat format;
        format.setVerticalAlignment(on ? QTextCharFormat::AlignSubScript
                                       : QTextCharFormat::AlignNormal);
        applyCharFormat(format);
    });
    connect(m_growFont, &QAction::triggered, this, [this] { stepFontSize(1); });
    connect(m_shrinkFont, &QAction::triggered, this, [this] { stepFontSize(-1); });
    connect(m_spacingSingle, &QAction::triggered, this, [this] { applyLineSpacing(100); });
    connect(m_spacingOneHalf, &QAction::triggered, this, [this] { applyLineSpacing(150); });
    connect(m_spacingDouble, &QAction::triggered, this, [this] { applyLineSpacing(200); });
    connect(m_insertDate, &QAction::triggered, this, [this] { insertDateTime(false); });
    connect(m_insertTime, &QAction::triggered, this, [this] { insertDateTime(true); });
    connect(m_insertSymbol, &QAction::triggered, this, &WriteTypography::insertSymbol);
    connect(m_insertRule, &QAction::triggered, this, &WriteTypography::insertHorizontalRule);
    connect(m_insertHyperlink, &QAction::triggered, this, &WriteTypography::insertHyperlink);
    connect(m_wordCount, &QAction::triggered, this, &WriteTypography::showWordCount);
}

void WriteTypography::applyCharFormat(const QTextCharFormat &format)
{
    QWidgetTextControl *control = m_view->control();
    const QTextCursor cursor = control->textCursor();
    if (cursor.hasSelection()) {
        QTextCursor edit = cursor;
        edit.mergeCharFormat(format);
    } else {
        QTextCharFormat merged = control->currentCharFormat();
        merged.merge(format);
        control->setCurrentCharFormat(merged);
    }
    m_document->setDirty(true);
    m_view->viewport()->update();
}

void WriteTypography::applyLineSpacing(qreal percent)
{
    if (m_syncing) {
        return;
    }
    QTextCursor cursor = m_view->control()->textCursor();
    QTextBlockFormat format;
    // ProportionalHeight is a percentage of the line's natural height, which
    // is what "1.5 lines" means everywhere else.
    format.setLineHeight(percent, QTextBlockFormat::ProportionalHeight);
    cursor.mergeBlockFormat(format);
    m_document->setDirty(true);
    m_view->viewport()->update();
}

void WriteTypography::stepFontSize(int delta)
{
    QWidgetTextControl *control = m_view->control();
    const qreal current = control->currentCharFormat().fontPointSize() > 0
        ? control->currentCharFormat().fontPointSize()
        : m_document->textDocument()->defaultFont().pointSizeF();
    // Two points a step, bounded: an unbounded step reaches unreadable in
    // both directions after a few presses.
    const qreal next = qBound(6.0, current + 2.0 * delta, 96.0);
    QTextCharFormat format;
    format.setFontPointSize(next);
    applyCharFormat(format);
}

void WriteTypography::pickHighlight()
{
    auto *parent = qobject_cast<QWidget *>(m_view);
    const QColor color = QColorDialog::getColor(Qt::yellow, parent, tr("Highlight Colour"));
    if (!color.isValid()) {
        return;
    }
    QTextCharFormat format;
    format.setBackground(color);
    applyCharFormat(format);
}

void WriteTypography::insertDateTime(bool time)
{
    const QLocale locale;
    const QString text = time ? locale.toString(QTime::currentTime(), QLocale::ShortFormat)
                              : locale.toString(QDate::currentDate(), QLocale::LongFormat);
    QTextCursor cursor = m_view->control()->textCursor();
    cursor.insertText(text);
    m_document->setDirty(true);
}

void WriteTypography::insertSymbol()
{
    QStringList items;
    items.reserve(int(std::size(kSymbols)));
    for (const char *symbol : kSymbols) {
        items.append(QString::fromUtf8(symbol));
    }
    bool ok = false;
    const QString chosen =
        QInputDialog::getItem(qobject_cast<QWidget *>(m_view), tr("Special Character"),
                              tr("Character:"), items, 0, false, &ok);
    if (!ok || chosen.isEmpty()) {
        return;
    }
    QTextCursor cursor = m_view->control()->textCursor();
    cursor.insertText(chosen);
    m_document->setDirty(true);
}

void WriteTypography::insertHorizontalRule()
{
    QTextCursor cursor = m_view->control()->textCursor();
    QTextBlockFormat rule;
    // A bottom border on an empty block is how a horizontal rule survives
    // the HTML round trip; a drawn line would not be in the document at all.
    rule.setProperty(QTextFormat::BlockTrailingHorizontalRulerWidth, 100);
    cursor.insertBlock(rule);
    // The next block goes back to normal, or everything after the rule
    // inherits it.
    cursor.insertBlock(QTextBlockFormat());
    m_document->setDirty(true);
}

void WriteTypography::insertHyperlink()
{
    QTextCursor cursor = m_view->control()->textCursor();
    const QString selected = cursor.selectedText();
    bool ok = false;
    const QString url =
        QInputDialog::getText(qobject_cast<QWidget *>(m_view), tr("Hyperlink"), tr("Address:"),
                              QLineEdit::Normal, QStringLiteral("https://"), &ok)
            .trimmed();
    if (!ok || url.isEmpty() || url == QLatin1String("https://")) {
        return;
    }
    QString label = selected;
    if (label.isEmpty()) {
        label = QInputDialog::getText(qobject_cast<QWidget *>(m_view), tr("Hyperlink"),
                                      tr("Text to show:"), QLineEdit::Normal, url, &ok)
                    .trimmed();
        if (!ok || label.isEmpty()) {
            return;
        }
    }
    QTextCharFormat link;
    link.setAnchor(true);
    link.setAnchorHref(url);
    link.setForeground(QColor(0x1a, 0x5f, 0xb4));
    link.setFontUnderline(true);
    cursor.insertText(label, link);
    // Stop the link format bleeding into whatever is typed next. Clearing
    // the foreground (rather than setting black) leaves the colour to the
    // document, which is what keeps the page readable under a dark theme.
    QTextCharFormat plain = m_view->control()->currentCharFormat();
    plain.setAnchor(false);
    plain.setAnchorHref(QString());
    plain.setFontUnderline(false);
    plain.clearForeground();
    m_view->control()->setCurrentCharFormat(plain);
    m_document->setDirty(true);
}

WriteTypography::Counts WriteTypography::counts() const
{
    const QTextCursor cursor = m_view->control()->textCursor();
    Counts out;
    out.selectionOnly = cursor.hasSelection();
    const QString text = out.selectionOnly
        ? cursor.selectedText()
        : m_document->textDocument()->toPlainText();
    out.characters = int(text.size());
    for (const QChar c : text) {
        if (!c.isSpace()) {
            ++out.charactersNoSpaces;
        }
    }
    out.words = int(text.split(QRegularExpression(QStringLiteral("\\s+")),
                               Qt::SkipEmptyParts)
                        .size());
    if (out.selectionOnly) {
        // QTextCursor::selectedText separates blocks with U+2029.
        out.paragraphs = int(text.count(QChar(0x2029))) + (text.isEmpty() ? 0 : 1);
    } else {
        out.paragraphs = m_document->textDocument()->blockCount();
    }
    return out;
}

void WriteTypography::showWordCount()
{
    const Counts c = counts();
    QMessageBox::information(
        qobject_cast<QWidget *>(m_view), tr("Word Count"),
        tr("%1\n\nWords: %2\nCharacters: %3\nCharacters (no spaces): %4\nParagraphs: %5")
            .arg(c.selectionOnly ? tr("Selection") : tr("Whole document"))
            .arg(c.words)
            .arg(c.characters)
            .arg(c.charactersNoSpaces)
            .arg(c.paragraphs));
}

void WriteTypography::syncFromCursor()
{
    m_syncing = true;
    const QTextCharFormat format = m_view->control()->currentCharFormat();
    m_superscript->setChecked(format.verticalAlignment() == QTextCharFormat::AlignSuperScript);
    m_subscript->setChecked(format.verticalAlignment() == QTextCharFormat::AlignSubScript);
    const QTextBlockFormat block = m_view->control()->textCursor().blockFormat();
    const qreal spacing = block.lineHeightType() == QTextBlockFormat::ProportionalHeight
        ? block.lineHeight()
        : 100.0;
    m_spacingDouble->setChecked(spacing >= 190);
    m_spacingOneHalf->setChecked(spacing >= 140 && spacing < 190);
    m_spacingSingle->setChecked(spacing < 140);
    m_syncing = false;
}

void WriteTypography::populateFormatMenu(QMenu *menu)
{
    menu->addAction(m_highlight);
    menu->addAction(m_superscript);
    menu->addAction(m_subscript);
    menu->addSeparator();
    menu->addAction(m_growFont);
    menu->addAction(m_shrinkFont);
    auto *spacing = menu->addMenu(tr("Line &Spacing"));
    spacing->setObjectName(QStringLiteral("writeLineSpacingMenu"));
    spacing->addAction(m_spacingSingle);
    spacing->addAction(m_spacingOneHalf);
    spacing->addAction(m_spacingDouble);
}

void WriteTypography::populateInsertMenu(QMenu *menu)
{
    menu->addAction(m_insertHyperlink);
    menu->addAction(m_insertSymbol);
    menu->addAction(m_insertRule);
    menu->addSeparator();
    menu->addAction(m_insertDate);
    menu->addAction(m_insertTime);
}

QList<QAction *> WriteTypography::stripActions() const
{
    return {m_highlight, m_superscript, m_subscript, m_growFont, m_shrinkFont};
}

} // namespace QindaOffice::Apps::Write
