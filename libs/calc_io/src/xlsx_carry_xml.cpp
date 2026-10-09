// SPDX-License-Identifier: GPL-3.0-or-later
// Attribute edits on carried parts (xlsx_carry_xml.h, M66, D-075).

#include "xlsx_carry_xml.h"

#include "xlsx_xml.h"

#include <QRegularExpression>

namespace QindaOffice::Calc::IO::Carry {

namespace {

QString decoded(QString value)
{
    value.replace(QLatin1String("&quot;"), QLatin1String("\""));
    value.replace(QLatin1String("&apos;"), QLatin1String("'"));
    value.replace(QLatin1String("&lt;"), QLatin1String("<"));
    value.replace(QLatin1String("&gt;"), QLatin1String(">"));
    value.replace(QLatin1String("&amp;"), QLatin1String("&"));
    return value;
}

QRegularExpression attributePattern(const QString &name)
{
    return QRegularExpression(QStringLiteral("\\s%1\\s*=\\s*(\"([^\"]*)\"|'([^']*)')")
                                  .arg(QRegularExpression::escape(name)));
}

} // namespace

QByteArray editStartTags(const QByteArray &xml, const QString &localName,
                         const std::function<QString(const QString &)> &edit)
{
    const QString text = QString::fromUtf8(xml);
    // "<name" or "<prefix:name", then white space, '/' or '>'.
    const QRegularExpression open(QStringLiteral("<(?:[A-Za-z_][\\w.-]*:)?%1(?=[\\s/>])")
                                      .arg(QRegularExpression::escape(localName)));
    QString out;
    out.reserve(text.size());
    qsizetype copied = 0;
    auto it = open.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const qsizetype start = match.capturedStart();
        // The tag ends at the first '>' outside a quoted value.
        qsizetype end = match.capturedEnd();
        QChar quote;
        for (; end < text.size(); ++end) {
            const QChar c = text.at(end);
            if (!quote.isNull()) {
                quote = c == quote ? QChar() : quote;
            } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                quote = c;
            } else if (c == QLatin1Char('>')) {
                break;
            }
        }
        if (end >= text.size()) {
            break;
        }
        out += QStringView(text).mid(copied, start - copied);
        out += edit(text.mid(start, end + 1 - start));
        copied = end + 1;
    }
    out += QStringView(text).mid(copied);
    return out.toUtf8();
}

QString tagAttribute(const QString &tag, const QString &name)
{
    const QRegularExpressionMatch match = attributePattern(name).match(tag);
    if (!match.hasMatch()) {
        return {};
    }
    return decoded(match.captured(2).isNull() ? match.captured(3) : match.captured(2));
}

QString withTagAttribute(const QString &tag, const QString &name, const QString &value)
{
    const QString attribute = QLatin1Char(' ') + name + QStringLiteral("=\"")
        + QString::fromUtf8(xmlAttribute(value)) + QLatin1Char('"');
    const QRegularExpressionMatch match = attributePattern(name).match(tag);
    if (match.hasMatch()) {
        QString out = tag;
        out.replace(match.capturedStart(), match.capturedLength(), attribute);
        return out;
    }
    const qsizetype end = tag.endsWith(QLatin1String("/>")) ? tag.size() - 2 : tag.size() - 1;
    QString out = tag;
    out.insert(end, attribute);
    return out;
}

} // namespace QindaOffice::Calc::IO::Carry
