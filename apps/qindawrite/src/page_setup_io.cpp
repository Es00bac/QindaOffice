// SPDX-License-Identifier: GPL-3.0-or-later
#include "page_setup_io.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

namespace {

QString readMeta(const QString &html, const QString &name)
{
    // <meta name="qindawrite-page" content="…"> - attribute order fixed by
    // our own writer; content is HTML-escaped text.
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

const QTextDocument *PageDecoration::headerFor(int page) const
{
    if (differentFirstPage && page == 1) {
        return headers[size_t(Docx::HeaderFooterSlot::First)];
    }
    if (differentOddEven && page % 2 == 0 && headers[size_t(Docx::HeaderFooterSlot::Even)] != nullptr) {
        return headers[size_t(Docx::HeaderFooterSlot::Even)];
    }
    return headers[size_t(Docx::HeaderFooterSlot::Default)];
}

const QTextDocument *PageDecoration::footerFor(int page) const
{
    if (differentFirstPage && page == 1) {
        return footers[size_t(Docx::HeaderFooterSlot::First)];
    }
    if (differentOddEven && page % 2 == 0 && footers[size_t(Docx::HeaderFooterSlot::Even)] != nullptr) {
        return footers[size_t(Docx::HeaderFooterSlot::Even)];
    }
    return footers[size_t(Docx::HeaderFooterSlot::Default)];
}

bool PageDecoration::isEmpty() const
{
    for (size_t i = 0; i < headers.size(); ++i) {
        if ((headers[i] != nullptr && !headers[i]->isEmpty()) || (footers[i] != nullptr && !footers[i]->isEmpty())) {
            return false;
        }
    }
    return footnotes.isEmpty();
}

PageSetup pageSetupFromHtml(const QString &html)
{
    PageSetup setup;
    const QStringList parts = readMeta(html, QStringLiteral("qindawrite-page")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QList<qreal> n;
    for (const QString &p : parts) {
        n.append(p.toDouble());
    }
    if (n.size() == 3 || n.size() == 8) {
        setup.layout.pageSize = QSizeF(n[0], n[1]);
        if (n.size() == 3) {
            // M14: one uniform margin.
            setup.layout.margins = QMarginsF(n[2], n[2], n[2], n[2]);
        } else {
            setup.layout.margins = QMarginsF(n[2], n[3], n[4], n[5]);
            setup.layout.headerDistance = n[6];
            setup.layout.footerDistance = n[7];
        }
        setup.hasCustomPageSize = setup.layout.pageSize.isValid() && setup.layout.pageSize.width() > 0;
    }
    setup.header = readMeta(html, QStringLiteral("qindawrite-header"));
    setup.footer = readMeta(html, QStringLiteral("qindawrite-footer"));
    const QString notes = readMeta(html, QStringLiteral("qindawrite-footnotes"));
    if (!notes.isEmpty()) {
        const QJsonObject object = QJsonDocument::fromJson(notes.toUtf8()).object();
        for (const QString &id : object.keys()) {
            setup.footnotes.insert(id, object.value(id).toString());
        }
    }
    return setup;
}

QString injectPageSetupMeta(const QString &html, const PageSetup &setup)
{
    const int head = int(html.indexOf(QStringLiteral("</head>")));
    if (head < 0) {
        return html;
    }
    const Docx::PageLayout &l = setup.layout;
    QString meta = QStringLiteral("<meta name=\"qindawrite-page\" content=\"%1 %2 %3 %4 %5 %6 %7 %8\">")
                       .arg(l.pageSize.width())
                       .arg(l.pageSize.height())
                       .arg(l.margins.left())
                       .arg(l.margins.top())
                       .arg(l.margins.right())
                       .arg(l.margins.bottom())
                       .arg(l.headerDistance)
                       .arg(l.footerDistance);
    if (!setup.header.isEmpty()) {
        meta += QStringLiteral("<meta name=\"qindawrite-header\" content=\"%1\">").arg(escapeMeta(setup.header));
    }
    if (!setup.footer.isEmpty()) {
        meta += QStringLiteral("<meta name=\"qindawrite-footer\" content=\"%1\">").arg(escapeMeta(setup.footer));
    }
    if (!setup.footnotes.isEmpty()) {
        QJsonObject object;
        for (const auto &[id, text] : setup.footnotes.asKeyValueRange()) {
            object.insert(id, text);
        }
        meta += QStringLiteral("<meta name=\"qindawrite-footnotes\" content=\"%1\">")
                    .arg(escapeMeta(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact))));
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
