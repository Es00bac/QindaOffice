// SPDX-License-Identifier: GPL-3.0-or-later
// DocumentStyles' M73 commands (docx_restyle.h, D-082): character styles
// and a changed style re-derived in the text. Both "rebase": a property a
// paragraph or run shows because its style gave it follows the style's new
// value; a property it says itself (a value that differs from what the old
// style gave) stays. The import snapshots follow the same rule, so a
// property that was as Word wrote it is still written from Word's markup —
// which says nothing of it: the style does.

#include "docx_restyle.h"

#include "docx_format.h"
#include "docx_metrics.h"
#include "docx_paragraph_format.h"
#include "docx_properties.h"
#include "docx_sections.h"
#include "docx_text.h"
#include "docx_writer_p.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>

namespace QindaOffice::Docx {

namespace {

// The look a style gives a run (what charFormatFromRPr sets).
constexpr int kRunKeys[] = {QTextFormat::FontFamilies,      QTextFormat::FontPointSize,     QTextFormat::FontWeight,
                            QTextFormat::FontItalic,        QTextFormat::FontStrikeOut,     QTextFormat::FontCapitalization,
                            QTextFormat::TextUnderlineStyle, QTextFormat::TextUnderlineColor, QTextFormat::ForegroundBrush,
                            QTextFormat::BackgroundBrush,   QTextFormat::TextVerticalAlignment, QTextFormat::FontLetterSpacing,
                            QTextFormat::FontLetterSpacingType, QTextFormat::FontStretch, QTextFormat::TextBaselineOffset,
                            kUnderlineWord,                 kDoubleStrike,                  kHiddenText,
                            kKerning};
// The look a style gives a paragraph (what blockFormatFromPPr sets) that
// can follow it property by property; spacing and line height have their
// own rules (folded margins, Word's line metrics).
constexpr int kBlockKeys[] = {QTextFormat::BlockAlignment,  QTextFormat::BlockRightMargin, QTextFormat::BlockNonBreakableLines,
                              QTextFormat::LayoutDirection, QTextFormat::BackgroundBrush,  QTextFormat::TabPositions,
                              kKeepWithNext,                kWidowControl,                 kContextualSpacing,
                              kOutlineLevel,                kTabStops,                     kParagraphBorderSpec,
                              kParagraphBorders};

template <typename Format>
void follow(Format &f, const QTextFormat &now, const QTextFormat &oldBase, const QTextFormat &newBase, int key)
{
    if (now.property(key) == oldBase.property(key) && oldBase.property(key) != newBase.property(key)) {
        if (newBase.hasProperty(key)) {
            f.setProperty(key, newBase.property(key));
        } else {
            f.clearProperty(key);
        }
    }
}

// The snapshot keys that were as imported stay as imported.
QVariantMap movedSnapshot(const QVariantMap &snapshot, const QVariantMap &before, const QVariantMap &after)
{
    QVariantMap out = snapshot;
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        if (it.value() == before.value(it.key())) {
            out.insert(it.key(), after.value(it.key()));
        }
    }
    return out;
}

QTextCharFormat rebasedRun(const QTextCharFormat &now, const QTextCharFormat &oldBase, const QTextCharFormat &newBase)
{
    QTextCharFormat f = now;
    for (const int key : kRunKeys) {
        follow(f, now, oldBase, newBase, key);
    }
    if (now.hasProperty(kRunSnapshot)) {
        f.setProperty(kRunSnapshot, movedSnapshot(now.property(kRunSnapshot).toMap(), charSnapshot(now), charSnapshot(f)));
    }
    return f;
}

QTextCharFormat runBase(const StyleSheet &styles, const Ooxml::Theme &theme, const QString &paragraphStyle,
                        const QString &characterStyle)
{
    const Ooxml::XmlElementPtr paragraphRun = styles.paragraphRunProperties(paragraphStyle);
    return charFormatFromRPr(mergeProperties(paragraphRun.get(), styles.characterRunProperties(characterStyle).get()).get(), theme);
}

// The fragments of `block` the range [from, to) covers.
QList<std::pair<std::pair<int, int>, QTextCharFormat>> runsOf(const QTextBlock &block, int from, int to)
{
    QList<std::pair<std::pair<int, int>, QTextCharFormat>> out;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        const int start = qMax(from, fragment.position());
        const int end = qMin(to, fragment.position() + fragment.length());
        const QTextCharFormat format = fragment.charFormat();
        if (start >= end || format.isImageFormat() || format.objectType() != QTextFormat::NoObject
            || format.hasProperty(kInlineXml)) {
            continue;
        }
        out.append({{start, end}, format});
    }
    return out;
}

void setRun(QTextDocument *document, std::pair<int, int> range, const QTextCharFormat &format)
{
    QTextCursor span(document);
    span.setPosition(range.first);
    span.setPosition(range.second, QTextCursor::KeepAnchor);
    span.setCharFormat(format);
}

} // namespace

QString DocumentStyles::paragraphStyleOf(const QTextBlock &block) const
{
    const QTextBlockFormat f = block.blockFormat();
    QString id = f.stringProperty(kParagraphStyleId);
    if (id.isEmpty() && f.hasProperty(kWriteStyleIdProperty)) {
        id = styleFor(f.intProperty(kWriteStyleIdProperty));
        if (id.isEmpty()) {
            id = detail::builtInStyleId(f.intProperty(kWriteStyleIdProperty));
        }
    }
    return m_styles.style(id) != nullptr ? id : m_styles.defaultParagraphStyle();
}

void DocumentStyles::applyCharacter(const QTextCursor &selection, const QString &styleId) const
{
    QTextDocument *document = selection.document();
    const int from = selection.selectionStart();
    const int to = selection.selectionEnd();
    for (QTextBlock block = document->findBlock(from); block.isValid() && block.position() <= to; block = block.next()) {
        const QString paragraphStyle = paragraphStyleOf(block);
        const QTextCharFormat newBase = runBase(m_styles, m_theme, paragraphStyle, styleId);
        for (const auto &[range, old] : runsOf(block, from, to)) {
            const QTextCharFormat oldBase = runBase(m_styles, m_theme, paragraphStyle, old.stringProperty(kRunStyleId));
            QTextCharFormat format = rebasedRun(old, oldBase, newBase);
            if (styleId.isEmpty()) {
                format.clearProperty(kRunStyleId);
            } else {
                format.setProperty(kRunStyleId, styleId);
            }
            setRun(document, range, format);
        }
    }
}

void DocumentStyles::restyle(QTextDocument *document, const StyleSheet &before, const QSet<QString> &changed) const
{
    const auto reaches = [&](const StyleSheet &sheet, const QString &id) {
        const QStringList chain = sheet.chainOf(id);
        return std::any_of(chain.cbegin(), chain.cend(), [&](const QString &link) { return changed.contains(link); });
    };
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const QString styleId = paragraphStyleOf(block);
        const bool paragraphChanged = reaches(before, styleId) || reaches(m_styles, styleId);
        if (paragraphChanged && !block.blockFormat().boolProperty(kParagraphSynthetic)) {
            const QTextBlockFormat now = block.blockFormat();
            const QTextBlockFormat oldBase = blockFormatFromPPr(before.paragraphProperties(styleId).get(), m_theme);
            const QTextBlockFormat newBase = blockFormatFromPPr(m_styles.paragraphProperties(styleId).get(), m_theme);
            QTextBlockFormat f = now;
            for (const int key : kBlockKeys) {
                if (key == QTextFormat::BlockRightMargin) {
                    // Compared and set as the paragraph's own (a section's
                    // display pad on top stays, M88).
                    if (qFuzzyCompare(1 + ownRightMargin(now), 1 + oldBase.doubleProperty(key))
                        && oldBase.property(key) != newBase.property(key)) {
                        setOwnRightMargin(f, newBase.doubleProperty(key));
                    }
                    continue;
                }
                follow(f, now, oldBase, newBase, key);
            }
            if (block.textList() == nullptr) { // a list item's indents are its level's
                for (const int key : {int(QTextFormat::BlockLeftMargin), int(QTextFormat::TextIndent)}) {
                    follow(f, now, oldBase, newBase, key);
                }
            }
            if (before.headingLevel(styleId) != m_styles.headingLevel(styleId) && !now.hasProperty(kOutlineLevel)) {
                const int heading = m_styles.headingLevel(styleId);
                f.setHeadingLevel(heading <= 6 ? heading : 0);
            }
            const LineSpacing own = ownLineSpacing(now);
            const LineSpacing oldLine = ownLineSpacing(oldBase);
            const LineSpacing newLine = ownLineSpacing(newBase);
            if (own.type == oldLine.type && qAbs(own.value - oldLine.value) < 0.01
                && (oldLine.type != newLine.type || qAbs(oldLine.value - newLine.value) >= 0.01)) {
                setWordLineSpacing(f, block, newLine.type, newLine.value);
            }
            if (now.hasProperty(kParagraphSnapshot)) {
                f.setProperty(kParagraphSnapshot, movedSnapshot(now.property(kParagraphSnapshot).toMap(), blockSnapshot(now),
                                                                blockSnapshot(f)));
            }
            QTextCursor(block).setBlockFormat(f);
            // Spacing: the own values follow the style, folded as the reader folds them.
            if (ownSpacing(now) == ownSpacing(oldBase) && !(ownSpacing(oldBase) == ownSpacing(newBase))) {
                const QVariant snapshot = block.blockFormat().property(kParagraphSnapshot);
                setParagraphSpacing(block, ownSpacing(newBase));
                if (snapshot.isValid()) {
                    QTextBlockFormat g = block.blockFormat();
                    g.setProperty(kParagraphSnapshot, movedSnapshot(snapshot.toMap(), blockSnapshot(f), blockSnapshot(g)));
                    QTextCursor(block).setBlockFormat(g);
                }
            }
        }
        // Runs: the paragraph style's run properties, or a changed character style.
        QList<std::pair<std::pair<int, int>, QTextCharFormat>> runs;
        for (const auto &[range, old] : runsOf(block, block.position(), block.position() + block.length())) {
            const QString characterStyle = old.stringProperty(kRunStyleId);
            if (!paragraphChanged && !reaches(before, characterStyle) && !reaches(m_styles, characterStyle)) {
                continue;
            }
            const QTextCharFormat oldBase = runBase(before, m_theme, styleId, characterStyle);
            const QTextCharFormat newBase = runBase(m_styles, m_theme, styleId, characterStyle);
            runs.append({range, rebasedRun(old, oldBase, newBase)});
        }
        for (const auto &[range, format] : std::as_const(runs)) {
            setRun(document, range, format);
        }
        if (paragraphChanged) {
            // The paragraph mark: what is typed next, and the list number's look.
            QTextCursor mark(block);
            setBlockCharFormatKeepingCell(mark, rebasedRun(block.charFormat(), runBase(before, m_theme, styleId, {}),
                                                           runBase(m_styles, m_theme, styleId, {})));
        }
    }
}

} // namespace QindaOffice::Docx
