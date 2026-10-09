// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_font.h"

#include "docx_properties.h"
#include "menu_tree.h"
#include "ooxml_units.h"
#include "page_text_control.h"
#include "write_document.h"

#include <QHash>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace Units = Ooxml::Units;

namespace {

const QHash<QString, QTextCharFormat::UnderlineStyle> &qtUnderlines()
{
    static const QHash<QString, QTextCharFormat::UnderlineStyle> map{
        {QStringLiteral("single"), QTextCharFormat::SingleUnderline},
        {QStringLiteral("dash"), QTextCharFormat::DashUnderline},
        {QStringLiteral("dotted"), QTextCharFormat::DotLine},
        {QStringLiteral("dotDash"), QTextCharFormat::DashDotLine},
        {QStringLiteral("dotDotDash"), QTextCharFormat::DashDotDotLine},
        {QStringLiteral("wave"), QTextCharFormat::WaveUnderline}};
    return map;
}

QString underlineName(const QTextCharFormat &f)
{
    if (f.underlineStyle() == QTextCharFormat::NoUnderline) {
        return QStringLiteral("none");
    }
    if (const QString word = f.stringProperty(Docx::kUnderlineWord); !word.isEmpty()) {
        return word;
    }
    const QString name = qtUnderlines().key(f.underlineStyle());
    return name.isEmpty() ? QStringLiteral("single") : name;
}

qreal pointSize(const QTextCharFormat &f, const QFont &fallback)
{
    return f.fontPointSize() > 0 ? f.fontPointSize() : (fallback.pointSizeF() > 0 ? fallback.pointSizeF() : 11.0);
}

// Applies the dialog's changed values to one run's format; clears go
// through the full format (QTextCursor::setCharFormat), so "automatic"
// and "none" really remove what was there.
void applyChanges(QTextCharFormat &f, const QVariantMap &answer, const QStringList &changed, const QFont &base)
{
    const auto has = [&](const char *key) { return changed.contains(QString::fromLatin1(key)); };
    const auto value = [&](const char *key) { return answer.value(QString::fromLatin1(key)); };
    if (has("family") && !value("family").toString().isEmpty()) {
        f.setFontFamilies({value("family").toString()});
    }
    if (has("size") && value("size").toReal() > 0) {
        f.setFontPointSize(qBound(1.0, value("size").toReal(), 1638.0));
    }
    if (has("bold")) {
        f.setFontWeight(value("bold").toBool() ? QFont::Bold : QFont::Normal);
    }
    if (has("italic")) {
        f.setFontItalic(value("italic").toBool());
    }
    if (has("color")) {
        const QColor color(value("color").toString());
        if (color.isValid()) {
            f.setForeground(color);
        } else {
            f.clearForeground();
        }
    }
    if (has("underline")) {
        const QString name = value("underline").toString();
        f.clearProperty(Docx::kUnderlineWord);
        if (name == u"none" || name.isEmpty()) {
            f.setUnderlineStyle(QTextCharFormat::NoUnderline);
        } else if (qtUnderlines().contains(name)) {
            f.setUnderlineStyle(qtUnderlines().value(name));
        } else {
            f.setUnderlineStyle(QTextCharFormat::SingleUnderline); // drawn as one line, kept as Word's
            f.setProperty(Docx::kUnderlineWord, name);
        }
    }
    if (has("underlineColor")) {
        const QColor color(value("underlineColor").toString());
        if (color.isValid()) {
            f.setUnderlineColor(color);
        } else {
            f.clearProperty(QTextFormat::TextUnderlineColor);
        }
    }
    if (has("strike") || has("doubleStrike")) {
        const bool twin = value("doubleStrike").toBool();
        f.setFontStrikeOut(twin || value("strike").toBool());
        if (twin) {
            f.setProperty(Docx::kDoubleStrike, true);
        } else {
            f.clearProperty(Docx::kDoubleStrike);
        }
    }
    if (has("superscript") || has("subscript")) {
        f.setVerticalAlignment(value("superscript").toBool() ? QTextCharFormat::AlignSuperScript
                                   : value("subscript").toBool() ? QTextCharFormat::AlignSubScript
                                                                 : QTextCharFormat::AlignNormal);
    }
    if (has("smallCaps") || has("allCaps")) {
        f.setFontCapitalization(value("allCaps").toBool()     ? QFont::AllUppercase
                                    : value("smallCaps").toBool() ? QFont::SmallCaps
                                                                  : QFont::MixedCase);
    }
    if (has("hidden")) {
        if (value("hidden").toBool()) {
            f.setProperty(Docx::kHiddenText, true);
        } else {
            f.clearProperty(Docx::kHiddenText);
        }
    }
    if (has("spacing")) {
        const qreal pt = qBound(-1584.0, value("spacing").toReal(), 1584.0);
        if (qAbs(pt) < 0.01) {
            f.clearProperty(QTextFormat::FontLetterSpacing);
            f.clearProperty(QTextFormat::FontLetterSpacingType);
        } else {
            f.setFontLetterSpacingType(QFont::AbsoluteSpacing);
            f.setFontLetterSpacing(Units::ptToPx(pt));
        }
    }
    if (has("scale")) {
        const int scale = qBound(1, value("scale").toInt(), 600);
        if (scale == 100) {
            f.clearProperty(QTextFormat::FontStretch);
        } else {
            f.setFontStretch(scale);
        }
    }
    if (has("position")) {
        // Word raises by points; Qt by a percentage of this run's size.
        const qreal pt = qBound(-1584.0, value("position").toReal(), 1584.0);
        f.setBaselineOffset(pt / pointSize(f, base) * 100.0);
    }
    if (has("kerning") || has("kernAt")) {
        const int halfPoints = qRound(value("kernAt").toReal() * 2);
        if (value("kerning").toBool() && halfPoints > 0) {
            f.setProperty(Docx::kKerning, halfPoints);
            f.setFontKerning(true);
        } else {
            f.clearProperty(Docx::kKerning);
        }
    }
}

QChar recase(QChar c, bool upper)
{
    return upper ? c.toUpper() : c.toLower();
}

} // namespace

FontController::FontController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model,
                               QObject *parent)
    : QObject(parent)
    , m_control(control)
    , m_document(document)
{
    m_font = model->add(QStringLiteral("writeFontAction"), tr("&Font…"), QKeySequence(QStringLiteral("Ctrl+D")),
                        QStringLiteral("type"));
    connect(m_font, &ActionEntry::triggered, this, &FontController::fontDialogRequested);
    const struct {
        const char *id;
        const char *text;
        Case mode;
    } cases[] = {{"writeCaseSentenceAction", QT_TR_NOOP("&Sentence case."), Case::Sentence},
                 {"writeCaseLowerAction", QT_TR_NOOP("&lowercase"), Case::Lower},
                 {"writeCaseUpperAction", QT_TR_NOOP("&UPPERCASE"), Case::Upper},
                 {"writeCaseCapitalizeAction", QT_TR_NOOP("&Capitalize Each Word"), Case::Capitalize},
                 {"writeCaseToggleAction", QT_TR_NOOP("&tOGGLE cASE"), Case::Toggle}};
    for (const auto &c : cases) {
        ActionEntry *entry = model->add(QString::fromLatin1(c.id), tr(c.text));
        connect(entry, &ActionEntry::triggered, this, [this, mode = c.mode] { changeCase(mode); });
        m_cases.append(entry);
    }
    m_cycle = model->add(QStringLiteral("writeChangeCaseAction"), tr("Change &Case"),
                         QKeySequence(QStringLiteral("Shift+F3")), QStringLiteral("arrow-up-down"));
    connect(m_cycle, &ActionEntry::triggered, this, [this] { cycleCase(); });
}

QList<MenuNode> FontController::menuItems() const
{
    QList<MenuNode> cases{MenuNode::action(m_cycle->id()), MenuNode::separator()};
    for (const ActionEntry *entry : m_cases) {
        cases.append(MenuNode::action(entry->id()));
    }
    return {MenuNode::action(m_font->id()), MenuNode::submenu(QStringLiteral("writeChangeCaseMenu"), tr("Change C&ase"), cases)};
}

QStringList FontController::caseIds() const
{
    QStringList ids{m_cycle->id()};
    for (const ActionEntry *entry : m_cases) {
        ids.append(entry->id());
    }
    return ids;
}

QString FontController::fontId() const
{
    return m_font->id();
}

QVariantMap FontController::fontInfo() const
{
    const QTextCursor cursor = m_control->textCursor();
    QTextCharFormat f = m_control->currentCharFormat();
    if (cursor.hasSelection()) {
        QTextCursor first(cursor.document());
        first.setPosition(cursor.selectionStart() + 1);
        f = first.charFormat();
    }
    return fontInfoOf(f, m_control->document()->defaultFont());
}

QVariantMap fontInfoOf(const QTextCharFormat &f, const QFont &base)
{
    const QStringList families = f.fontFamilies().toStringList();
    const qreal size = pointSize(f, base);
    return {{QStringLiteral("family"), families.isEmpty() || families.first().isEmpty() ? base.family() : families.first()},
            {QStringLiteral("size"), size},
            {QStringLiteral("bold"), f.fontWeight() >= QFont::DemiBold},
            {QStringLiteral("italic"), f.fontItalic()},
            {QStringLiteral("color"), f.hasProperty(QTextFormat::ForegroundBrush) && f.foreground().style() != Qt::NoBrush
                                          ? f.foreground().color().name()
                                          : QString()},
            {QStringLiteral("underline"), underlineName(f)},
            {QStringLiteral("underlineColor"), f.hasProperty(QTextFormat::TextUnderlineColor) && f.underlineColor().isValid()
                                                   ? f.underlineColor().name()
                                                   : QString()},
            {QStringLiteral("strike"), f.fontStrikeOut() && !f.boolProperty(Docx::kDoubleStrike)},
            {QStringLiteral("doubleStrike"), f.fontStrikeOut() && f.boolProperty(Docx::kDoubleStrike)},
            {QStringLiteral("superscript"), f.verticalAlignment() == QTextCharFormat::AlignSuperScript},
            {QStringLiteral("subscript"), f.verticalAlignment() == QTextCharFormat::AlignSubScript},
            {QStringLiteral("smallCaps"), f.fontCapitalization() == QFont::SmallCaps},
            {QStringLiteral("allCaps"), f.fontCapitalization() == QFont::AllUppercase},
            {QStringLiteral("hidden"), f.boolProperty(Docx::kHiddenText)},
            {QStringLiteral("spacing"), f.hasProperty(QTextFormat::FontLetterSpacing) && f.fontLetterSpacingType() == QFont::AbsoluteSpacing
                                            ? Units::pxToPt(f.fontLetterSpacing())
                                            : 0.0},
            {QStringLiteral("scale"), f.hasProperty(QTextFormat::FontStretch) && f.fontStretch() > 0 ? f.fontStretch() : 100},
            {QStringLiteral("position"), f.baselineOffset() / 100.0 * size},
            {QStringLiteral("kerning"), f.intProperty(Docx::kKerning) > 0},
            {QStringLiteral("kernAt"), f.intProperty(Docx::kKerning) > 0 ? f.intProperty(Docx::kKerning) / 2.0 : size}};
}

bool FontController::applyFont(const QVariantMap &answer)
{
    const QStringList changed = answer.value(QStringLiteral("changed")).toStringList();
    if (changed.isEmpty()) {
        return false;
    }
    const QFont base = m_control->document()->defaultFont();
    QTextCursor cursor = m_control->textCursor();
    if (!cursor.hasSelection()) {
        QTextCharFormat typing = m_control->currentCharFormat();
        applyChanges(typing, answer, changed, base);
        m_control->setCurrentCharFormat(typing);
        return true;
    }
    cursor.beginEditBlock();
    applyFontAt(cursor, answer);
    cursor.endEditBlock();
    m_document->setDirty(true);
    return true;
}

bool applyFontAt(const QTextCursor &cursor, const QVariantMap &answer)
{
    const QStringList changed = answer.value(QStringLiteral("changed")).toStringList();
    if (changed.isEmpty() || !cursor.hasSelection()) {
        return false;
    }
    const QFont base = cursor.document()->defaultFont();
    // Run by run, so each keeps what the dialog did not touch and a raise
    // in points becomes each run's own percentage.
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    QTextDocument *document = cursor.document();
    QList<std::pair<std::pair<int, int>, QTextCharFormat>> runs;
    for (QTextBlock block = document->findBlock(start); block.isValid() && block.position() < end; block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const int from = qMax(start, fragment.position());
            const int to = qMin(end, fragment.position() + fragment.length());
            const QTextCharFormat old = fragment.charFormat();
            if (from >= to || old.isImageFormat() || old.hasProperty(Docx::kInlineXml)) {
                continue;
            }
            QTextCharFormat f = old;
            applyChanges(f, answer, changed, base);
            runs.append({{from, to}, f});
        }
    }
    for (const auto &[range, format] : std::as_const(runs)) {
        QTextCursor span(document);
        span.setPosition(range.first);
        span.setPosition(range.second, QTextCursor::KeepAnchor);
        span.setCharFormat(format);
    }
    return true;
}

bool FontController::changeCase(Case mode)
{
    QTextCursor cursor = m_control->textCursor();
    if (!cursor.hasSelection()) {
        cursor.select(QTextCursor::WordUnderCursor); // Word changes the word at the caret
    }
    if (!cursor.hasSelection()) {
        return false;
    }
    QTextDocument *document = cursor.document();
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    QString text;
    for (int p = start; p < end; ++p) {
        text.append(document->characterAt(p));
    }
    // Letter by letter, so every run keeps its length and formatting.
    QString out = text;
    bool capitalNext = true;
    bool stopSeen = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isLetter()) {
            switch (mode) {
            case Case::Lower:
                out[i] = c.toLower();
                break;
            case Case::Upper:
                out[i] = c.toUpper();
                break;
            case Case::Toggle:
                out[i] = c.isUpper() ? c.toLower() : c.toUpper();
                break;
            case Case::Capitalize:
                out[i] = recase(c, i == 0 || !(text.at(i - 1).isLetter() || text.at(i - 1) == u'\''));
                break;
            case Case::Sentence:
                out[i] = recase(c, capitalNext);
                break;
            }
            capitalNext = false;
            stopSeen = false;
        } else if (c == u'.' || c == u'!' || c == u'?') {
            stopSeen = true;
        } else if (c == QChar::ParagraphSeparator || (c.isSpace() && stopSeen)) {
            capitalNext = true;
        }
    }
    if (out == text) {
        return false;
    }
    cursor.beginEditBlock();
    // Each stretch of changed letters in one format is replaced in that format.
    const auto formatAt = [document](int position) {
        QTextCursor at(document);
        at.setPosition(position + 1);
        return at.charFormat();
    };
    for (int p = start; p < end;) {
        if (out.at(p - start) == text.at(p - start)) {
            ++p;
            continue;
        }
        const QTextCharFormat format = formatAt(p);
        int q = p + 1;
        while (q < end && out.at(q - start) != text.at(q - start) && formatAt(q) == format) {
            ++q;
        }
        QTextCursor span(document);
        span.setPosition(p);
        span.setPosition(q, QTextCursor::KeepAnchor);
        span.insertText(out.mid(p - start, q - p), format);
        p = q;
    }
    cursor.endEditBlock();
    QTextCursor selection(document);
    selection.setPosition(start);
    selection.setPosition(end, QTextCursor::KeepAnchor);
    m_control->setTextCursor(selection);
    m_document->setDirty(true);
    return true;
}

bool FontController::cycleCase()
{
    QTextCursor cursor = m_control->textCursor();
    if (!cursor.hasSelection()) {
        cursor.select(QTextCursor::WordUnderCursor);
        m_control->setTextCursor(cursor);
    }
    const QString text = cursor.selectedText();
    QString letters;
    for (const QChar c : text) {
        if (c.isLetter()) {
            letters.append(c);
        }
    }
    if (letters.isEmpty()) {
        return false;
    }
    // lowercase → UPPERCASE → Capitalize Each Word → Sentence case → lowercase.
    if (letters == letters.toLower()) {
        return changeCase(Case::Upper);
    }
    if (letters == letters.toUpper()) {
        return changeCase(Case::Capitalize);
    }
    const bool capitalized = [&] {
        for (qsizetype i = 0; i < text.size(); ++i) {
            const bool start = i == 0 || !(text.at(i - 1).isLetter() || text.at(i - 1) == u'\'');
            if (text.at(i).isLetter() && (start ? !text.at(i).isUpper() : !text.at(i).isLower())) {
                return false;
            }
        }
        return true;
    }();
    // A lone capitalised word is already in sentence case: on to lowercase.
    return (capitalized && changeCase(Case::Sentence)) || changeCase(Case::Lower);
}

} // namespace QindaOffice::Apps::Write
