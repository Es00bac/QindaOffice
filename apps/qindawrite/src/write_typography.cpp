// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_typography.h"

#include "menu_tree.h"
#include "page_text_control.h"
#include "standard_actions.h"
#include "write_document.h"
#include "write_selection.h"

#include "docx_format.h"
#include "docx_metrics.h"

#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

// A small, useful set rather than a full Unicode browser: the characters a
// document actually needs that no keyboard has a key for.
const char *const kSymbols[] = {"—", "–", "…", "•", "·", "©", "®", "™", "°",
                                "±", "×", "÷", "≈", "≠", "≤", "≥", "→", "←",
                                "€", "£", "¥", "¢", "§", "¶", "«", "»", "“",
                                "”", "‘", "’", "½", "¼", "¾", "α", "β", "π"};

MenuNode a(const ActionEntry *entry)
{
    return MenuNode::action(entry->id());
}

} // namespace

WriteTypography::WriteTypography(PageTextControl *control, WriteDocument *document,
                                 Quick::ActionModel *model, QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    createActions(model);
    connect(m_control, &PageTextControl::cursorPositionChanged, this, [this] {
        syncFromCursor();
        emit countsChanged();
    });
    connect(m_document->textDocument(), &QTextDocument::contentsChanged, this,
            [this] { emit countsChanged(); });
    syncFromCursor();
}

void WriteTypography::createActions(Quick::ActionModel *model)
{
    using namespace Quick::ActionId;
    const auto make = [model](const char *id, const QString &text, const QKeySequence &shortcut = {},
                              const char *icon = nullptr) {
        return model->add(QString::fromLatin1(id), text, shortcut,
                          icon != nullptr ? QString::fromLatin1(icon) : QString());
    };
    m_highlight = make(kFormatHighlight, tr("&Highlight Colour…"), {}, "highlighter");
    m_superscript = make(kFormatSuperscript, tr("Su&perscript"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+P")), "superscript");
    m_subscript = make(kFormatSubscript, tr("Su&bscript"), QKeySequence(QStringLiteral("Ctrl+Shift+B")),
                       "subscript");
    m_superscript->setCheckable(true);
    m_subscript->setCheckable(true);
    m_growFont = make(kFormatGrowFont, tr("&Grow Font"), QKeySequence(QStringLiteral("Ctrl+Shift+>")),
                      "a-large-small");
    m_shrinkFont = make(kFormatShrinkFont, tr("S&hrink Font"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+<")), "shrink");
    m_spacingSingle = make(kFormatLineSpacingSingle, tr("&Single"));
    m_spacingOneHalf = make(kFormatLineSpacingOneHalf, tr("1.&5 Lines"));
    m_spacingDouble = make(kFormatLineSpacingDouble, tr("&Double"));
    for (ActionEntry *entry : {m_spacingSingle, m_spacingOneHalf, m_spacingDouble}) {
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("writeLineSpacing"));
    }
    m_spacingSingle->setChecked(true);
    m_insertDate = make(kInsertDate, tr("&Date"));
    m_insertTime = make(kInsertTime, tr("&Time"));
    m_insertSymbol = make(kInsertSymbol, tr("&Special Character…"));
    m_insertRule = make(kInsertHorizontalRule, tr("Horizontal &Rule"));
    m_insertHyperlink = make(kInsertHyperlink, tr("&Hyperlink…"), QKeySequence(QStringLiteral("Ctrl+K")),
                             "link");
    m_wordCount = make(kToolsWordCount, tr("&Word Count…"));

    connect(m_highlight, &ActionEntry::triggered, this, &WriteTypography::highlightRequested);
    connect(m_superscript, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            QTextCharFormat format;
            format.setVerticalAlignment(on ? QTextCharFormat::AlignSuperScript
                                           : QTextCharFormat::AlignNormal);
            applyCharFormat(format);
        }
    });
    connect(m_subscript, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            QTextCharFormat format;
            format.setVerticalAlignment(on ? QTextCharFormat::AlignSubScript
                                           : QTextCharFormat::AlignNormal);
            applyCharFormat(format);
        }
    });
    connect(m_growFont, &ActionEntry::triggered, this, [this] { stepFontSize(1); });
    connect(m_shrinkFont, &ActionEntry::triggered, this, [this] { stepFontSize(-1); });
    connect(m_spacingSingle, &ActionEntry::triggered, this, [this] { applyLineSpacing(100); });
    connect(m_spacingOneHalf, &ActionEntry::triggered, this, [this] { applyLineSpacing(150); });
    connect(m_spacingDouble, &ActionEntry::triggered, this, [this] { applyLineSpacing(200); });
    connect(m_insertDate, &ActionEntry::triggered, this, [this] { insertDateTime(false); });
    connect(m_insertTime, &ActionEntry::triggered, this, [this] { insertDateTime(true); });
    connect(m_insertSymbol, &ActionEntry::triggered, this, &WriteTypography::symbolRequested);
    connect(m_insertRule, &ActionEntry::triggered, this, [this] { insertHorizontalRule(); });
    connect(m_insertHyperlink, &ActionEntry::triggered, this,
            [this] { emit hyperlinkRequested(m_control->textCursor().selectedText()); });
    connect(m_wordCount, &ActionEntry::triggered, this, [this] { emit wordCountRequested(countsMap()); });
}

void WriteTypography::applyCharFormat(const QTextCharFormat &format)
{
    const QTextCursor cursor = m_control->textCursor();
    if (cursor.hasSelection()) {
        QTextCursor edit = cursor;
        edit.mergeCharFormat(format);
    } else {
        QTextCharFormat merged = m_control->currentCharFormat();
        merged.merge(format);
        m_control->setCurrentCharFormat(merged);
    }
    m_document->setDirty(true);
}

void WriteTypography::applyLineSpacing(qreal percent)
{
    if (m_syncing) {
        return;
    }
    QTextCursor cursor = m_control->textCursor();
    // ProportionalHeight is a percentage of the line's natural height, which
    // is what "1.5 lines" means everywhere else; a paragraph read from Word
    // keeps Word's line metrics (docx_metrics.h, as the Paragraph dialog).
    cursor.beginEditBlock();
    for (const QTextBlock &block : selectedBlocks(cursor)) {
        QTextBlockFormat format = block.blockFormat();
        Docx::setWordLineSpacing(format, block, QTextBlockFormat::ProportionalHeight, percent);
        QTextCursor(block).setBlockFormat(format);
    }
    cursor.endEditBlock();
    m_document->setDirty(true);
    syncFromCursor();
}

void WriteTypography::stepFontSize(int delta)
{
    const qreal current = m_control->currentCharFormat().fontPointSize() > 0
        ? m_control->currentCharFormat().fontPointSize()
        : m_document->textDocument()->defaultFont().pointSizeF();
    // Two points a step, bounded: an unbounded step reaches unreadable in
    // both directions after a few presses.
    QTextCharFormat format;
    format.setFontPointSize(qBound(6.0, current + 2.0 * delta, 96.0));
    applyCharFormat(format);
}

void WriteTypography::applyHighlight(const QColor &color)
{
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
    m_control->insertText(text);
    m_document->setDirty(true);
}

QStringList WriteTypography::symbols() const
{
    QStringList items;
    for (const char *symbol : kSymbols) {
        items.append(QString::fromUtf8(symbol));
    }
    return items;
}

void WriteTypography::insertSymbol(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    m_control->insertText(text);
    m_document->setDirty(true);
}

void WriteTypography::insertHorizontalRule()
{
    QTextCursor cursor = m_control->textCursor();
    QTextBlockFormat rule;
    // A bottom border on an empty block is how a horizontal rule survives
    // the HTML round trip; a drawn line would not be in the document at all.
    rule.setProperty(QTextFormat::BlockTrailingHorizontalRulerWidth, 100);
    cursor.insertBlock(rule);
    // The next block goes back to normal, or everything after the rule
    // inherits it.
    cursor.insertBlock(QTextBlockFormat());
    m_control->setTextCursor(cursor);
    m_document->setDirty(true);
}

void WriteTypography::insertHyperlink(const QString &rawUrl, const QString &rawLabel)
{
    const QString url = rawUrl.trimmed();
    if (url.isEmpty() || url == QLatin1String("https://")) {
        return;
    }
    QTextCursor cursor = m_control->textCursor();
    const QString label = rawLabel.trimmed().isEmpty()
        ? (cursor.hasSelection() ? cursor.selectedText() : url)
        : rawLabel.trimmed();
    QTextCharFormat link;
    link.setAnchor(true);
    link.setAnchorHref(url);
    link.setForeground(QColor(0x1a, 0x5f, 0xb4));
    link.setFontUnderline(true);
    cursor.insertText(label, link);
    m_control->setTextCursor(cursor);
    // Stop the link format bleeding into whatever is typed next. Clearing
    // the foreground (rather than setting black) leaves the colour to the
    // document, which is what keeps the page readable under a dark theme.
    QTextCharFormat plain = m_control->currentCharFormat();
    plain.setAnchor(false);
    plain.setAnchorHref(QString());
    plain.setFontUnderline(false);
    plain.clearForeground();
    m_control->setCurrentCharFormat(plain);
    m_document->setDirty(true);
}

WriteTypography::Counts WriteTypography::counts() const
{
    const QTextCursor cursor = m_control->textCursor();
    Counts out;
    out.selectionOnly = cursor.hasSelection();
    const QString text = out.selectionOnly ? cursor.selectedText() : m_document->textDocument()->toPlainText();
    out.characters = int(text.size());
    for (const QChar c : text) {
        if (!c.isSpace()) {
            ++out.charactersNoSpaces;
        }
    }
    out.words = int(text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size());
    if (out.selectionOnly) {
        // QTextCursor::selectedText separates blocks with U+2029.
        out.paragraphs = int(text.count(QChar(0x2029))) + (text.isEmpty() ? 0 : 1);
    } else {
        out.paragraphs = m_document->textDocument()->blockCount();
    }
    return out;
}

QVariantMap WriteTypography::countsMap() const
{
    const Counts c = counts();
    return {{QStringLiteral("words"), c.words},
            {QStringLiteral("characters"), c.characters},
            {QStringLiteral("charactersNoSpaces"), c.charactersNoSpaces},
            {QStringLiteral("paragraphs"), c.paragraphs},
            {QStringLiteral("selectionOnly"), c.selectionOnly}};
}

void WriteTypography::syncFromCursor()
{
    m_syncing = true;
    const QTextCharFormat format = m_control->currentCharFormat();
    m_superscript->setChecked(format.verticalAlignment() == QTextCharFormat::AlignSuperScript);
    m_subscript->setChecked(format.verticalAlignment() == QTextCharFormat::AlignSubScript);
    const QTextBlockFormat block = m_control->textCursor().blockFormat();
    const Docx::LineSpacing own = Docx::ownLineSpacing(block);
    const qreal spacing = own.type == QTextBlockFormat::ProportionalHeight ? own.value : 100.0;
    if (spacing >= 190) {
        m_spacingDouble->setChecked(true);
    } else if (spacing >= 140) {
        m_spacingOneHalf->setChecked(true);
    } else {
        m_spacingSingle->setChecked(true);
    }
    m_syncing = false;
}

QList<MenuNode> WriteTypography::formatMenuItems() const
{
    return {a(m_highlight), a(m_superscript), a(m_subscript), MenuNode::separator(), a(m_growFont),
            a(m_shrinkFont),
            MenuNode::submenu(QStringLiteral("writeLineSpacingMenu"), tr("Line &Spacing"),
                              {a(m_spacingSingle), a(m_spacingOneHalf), a(m_spacingDouble)})};
}

QList<MenuNode> WriteTypography::insertMenuItems() const
{
    return {a(m_insertHyperlink), a(m_insertSymbol), a(m_insertRule), MenuNode::separator(),
            a(m_insertDate), a(m_insertTime)};
}

QStringList WriteTypography::stripIds() const
{
    return {m_highlight->id(), m_superscript->id(), m_subscript->id(), m_growFont->id(),
            m_shrinkFont->id()};
}

QString WriteTypography::wordCountId() const
{
    return m_wordCount->id();
}

} // namespace QindaOffice::Apps::Write
