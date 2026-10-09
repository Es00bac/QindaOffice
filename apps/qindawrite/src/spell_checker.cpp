// SPDX-License-Identifier: GPL-3.0-or-later
#include "spell_checker.h"

#include "docx_properties.h"

#include <sonnet/speller.h>

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>

#include <algorithm>

namespace QindaOffice::Apps::Write {

SpellChecker::SpellChecker(QTextDocument *document)
    : QSyntaxHighlighter(document)
    , m_speller(std::make_unique<Sonnet::Speller>())
{
    rehighlight();
}

SpellChecker::~SpellChecker() = default;

bool SpellChecker::isActive() const
{
    return m_speller->isValid();
}

bool SpellChecker::isMisspelled(const QString &word) const
{
    return m_speller->isValid() && !m_ignoredWords.contains(word.toLower())
        && m_speller->isMisspelled(word);
}

void SpellChecker::ignoreWord(const QString &word)
{
    m_ignoredWords.insert(word.toLower());
    // Re-run the highlighter so existing squiggles drop immediately.
    rehighlight();
}

QStringList SpellChecker::suggestions(const QString &word) const
{
    if (!m_speller->isValid()) {
        return {};
    }
    return m_speller->suggest(word);
}

void SpellChecker::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    // Re-run the pass so turning it off actually clears the underlines
    // instead of leaving the last ones frozen on screen.
    rehighlight();
}

void SpellChecker::highlightBlock(const QString &text)
{
    if (!m_enabled) {
        return;
    }
    if (!m_speller->isValid()) {
        return;
    }
    static const QRegularExpression kWords(
        QStringLiteral("\\b[A-Za-z][A-Za-z'\\-]{1,}\\b"));
    // A field's result is not a word to check: «AddressBlock», «First_Name»,
    // a page number (M77; Word does not check them either).
    QList<std::pair<int, int>> fields; // [start, end) in the block
    const QTextBlock block = currentBlock();
    for (auto f = block.begin(); !f.atEnd(); ++f) {
        if (f.fragment().isValid() && f.fragment().charFormat().hasProperty(Docx::kFieldInstruction)) {
            const int start = f.fragment().position() - block.position();
            fields.append({start, start + f.fragment().length()});
        }
    }
    auto inField = [&fields](int start, int end) {
        return std::any_of(fields.cbegin(), fields.cend(),
                           [&](const std::pair<int, int> &r) { return start < r.second && end > r.first; });
    };
    auto it = kWords.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (inField(int(match.capturedStart()), int(match.capturedEnd()))) {
            continue;
        }
        if (isMisspelled(match.captured())) {
            QTextCharFormat format;
            format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            format.setUnderlineColor(Qt::red);
            setFormat(match.capturedStart(), match.capturedLength(), format);
        }
    }
}

} // namespace QindaOffice::Apps::Write
