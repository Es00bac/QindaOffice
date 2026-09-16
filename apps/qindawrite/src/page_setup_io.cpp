// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_setup_io.h"

#include <QRegularExpression>

namespace QindaOffice::Apps::Write {

namespace {

QString readMeta(const QString &html, const QString &name)
{
    // <meta name="qindawrite-page" content="W H M"> - attribute order fixed
    // by our own writer; content is HTML-escaped text.
    // No static caching: the pattern is built per meta name.
    const QRegularExpression kPattern(
        QStringLiteral("<meta\\s+name=\"%1\"\\s+content=\"([^\"]*)\"\\s*>").arg(name));
    const QRegularExpressionMatch match = kPattern.match(html);
    if (!match.hasMatch()) {
        return QString();
    }
    QString value = match.captured(1);
    // AGENT-GUARD: &amp; decodes LAST — decoding it first double-unescaped
    // nested entities (&amp;lt; became "<" instead of "&lt;").
    value.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    value.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    value.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    value.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return value;
}

QString escapeMeta(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return out;
}

} // namespace

PageSetup pageSetupFromHtml(const QString &html)
{
    PageSetup setup;
    const QString pageMeta = readMeta(html, QStringLiteral("qindawrite-page"));
    if (!pageMeta.isEmpty()) {
        const QStringList parts = pageMeta.split(QLatin1Char(' '));
        if (parts.size() == 3) {
            setup.pageSize = QSizeF(parts[0].toDouble(), parts[1].toDouble());
            setup.margin = parts[2].toDouble();
            setup.hasCustomPageSize = setup.pageSize.isValid() && setup.pageSize.width() > 0;
        }
    }
    setup.header = readMeta(html, QStringLiteral("qindawrite-header"));
    setup.footer = readMeta(html, QStringLiteral("qindawrite-footer"));
    return setup;
}

QString injectPageSetupMeta(const QString &html, const PageSetup &setup)
{
    const int head = html.indexOf(QStringLiteral("</head>"));
    if (head < 0) {
        return html;
    }
    QString meta =
        QStringLiteral("<meta name=\"qindawrite-page\" content=\"%1 %2 %3\">")
            .arg(setup.pageSize.width())
            .arg(setup.pageSize.height())
            .arg(setup.margin);
    if (!setup.header.isEmpty()) {
        meta += QStringLiteral("<meta name=\"qindawrite-header\" content=\"%1\">")
                    .arg(escapeMeta(setup.header));
    }
    if (!setup.footer.isEmpty()) {
        meta += QStringLiteral("<meta name=\"qindawrite-footer\" content=\"%1\">")
                    .arg(escapeMeta(setup.footer));
    }
    QString out = html;
    out.insert(head, meta);
    return out;
}

QString formatPageDecoration(const QString &pattern, int page, int pageCount)
{
    QString out = pattern;
    out.replace(QStringLiteral("{pages}"), QString::number(pageCount));
    out.replace(QStringLiteral("{page}"), QString::number(page));
    return out;
}

} // namespace QindaOffice::Apps::Write
