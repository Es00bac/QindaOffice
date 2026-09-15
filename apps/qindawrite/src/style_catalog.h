// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Named paragraph styles (plan section 7.1): a small catalog of built-in
// styles (Title, Heading 1-6, Quote, Code, Normal) plus a resolver that
// re-derives style identity from formatting after HTML/Markdown round
// trips (custom properties are not written by Qt's writers, so identity
// survives classification, not serialization).

#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>

namespace QindaOffice::Apps::Write {

inline constexpr int kStyleIdProperty = QTextFormat::UserProperty + 1;

struct ParagraphStyle {
    int id = 0;
    QString name;
    QTextBlockFormat blockFormat;
    QTextCharFormat charFormat;
};

class StyleCatalog {
public:
    enum BuiltInId {
        Normal = 0,
        Title,
        Heading1,
        Heading2,
        Heading3,
        Heading4,
        Heading5,
        Heading6,
        Quote,
        Code,
    };

    StyleCatalog();

    [[nodiscard]] const ParagraphStyle &at(int id) const { return m_styles.at(id); }
    [[nodiscard]] int count() const { return int(m_styles.size()); }
    [[nodiscard]] QStringList names() const;

    // Applies the style to the block under cursor: block format (with the
    // id property), block char format, then char format over the range.
    void apply(QTextCursor cursor, int id) const;

private:
    QVector<ParagraphStyle> m_styles;
};

// Derives the style id from a block's formatting (plan section 7.1):
// headingLevel n -> Heading n; fixed-pitch or non-breakable -> Code;
// left margin >= 30 && italic -> Quote; heading 1 && centred -> Title;
// else Normal.
[[nodiscard]] int classifyBlockStyle(const QTextBlock &block);

} // namespace QindaOffice::Apps::Write
