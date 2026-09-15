// SPDX-License-Identifier: GPL-3.0-or-later
#include "spell_checker.h"

#include <sonnet/speller.h>

#include <QRegularExpression>
#include <QTextDocument>

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
    return m_speller->isValid() && m_speller->isMisspelled(word);
}

QStringList SpellChecker::suggestions(const QString &word) const
{
    if (!m_speller->isValid()) {
        return {};
    }
    return m_speller->suggest(word);
}

void SpellChecker::highlightBlock(const QString &text)
{
    if (!m_speller->isValid()) {
        return;
    }
    static const QRegularExpression kWords(
        QStringLiteral("\\b[A-Za-z][A-Za-z'\\-]{1,}\\b"));
    auto it = kWords.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (m_speller->isMisspelled(match.captured())) {
            QTextCharFormat format;
            format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
            format.setUnderlineColor(Qt::red);
            setFormat(match.capturedStart(), match.capturedLength(), format);
        }
    }
}

} // namespace QindaOffice::Apps::Write
