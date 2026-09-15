// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Spell check (plan section 6.1): a QSyntaxHighlighter underline pass over
// the document using Sonnet::Speller (hunspell dictionaries), plus a
// suggestions helper the context menu and Tools menu share. Sonnet's own
// Highlighter only accepts QTextEdit/QPlainTextEdit - the page view is
// neither, hence this small port.

#include <QSyntaxHighlighter>

namespace Sonnet {
class Speller;
}

namespace QindaOffice::Apps::Write {

class SpellChecker final : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit SpellChecker(QTextDocument *document);
    ~SpellChecker() override;

    [[nodiscard]] bool isActive() const;
    [[nodiscard]] bool isMisspelled(const QString &word) const;
    [[nodiscard]] QStringList suggestions(const QString &word) const;

protected:
    void highlightBlock(const QString &text) override;

private:
    std::unique_ptr<Sonnet::Speller> m_speller;
};

} // namespace QindaOffice::Apps::Write
