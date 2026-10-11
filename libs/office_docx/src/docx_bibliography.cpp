// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_bibliography.h"
#include <QRegularExpression>
#include <QDate>

namespace QindaOffice::Docx {
const BibliographySource *Bibliography::find(const QString &tag) const
{
    for (const auto &source : sources) if (source.tag() == tag) return &source;
    return nullptr;
}
bool supportedSourceType(const QString &type)
{
    return type == u"Book" || type == u"JournalArticle" || type == u"InternetSite";
}
bool validateSource(const QVariantMap &values, QString *error)
{
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    static const QRegularExpression tag(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,63}$"));
    if (!tag.match(values.value(QStringLiteral("tag")).toString()).hasMatch())
        return fail(QStringLiteral("Use a source tag of 1–64 letters, digits or underscores, starting with a letter or underscore."));
    if (!supportedSourceType(values.value(QStringLiteral("type")).toString()))
        return fail(QStringLiteral("Choose Book, Journal article or Website."));
    if (values.value(QStringLiteral("title")).toString().trimmed().isEmpty())
        return fail(QStringLiteral("Enter the source title."));
    const QString accessed = values.value(QStringLiteral("accessed")).toString();
    if (!accessed.isEmpty() && !QDate::fromString(accessed, Qt::ISODate).isValid()) return fail(QStringLiteral("Use YYYY-MM-DD for the accessed date."));
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        if (it.value().toString().size() > 4096) return fail(QStringLiteral("Source fields are limited to 4096 characters."));
    return true;
}
namespace {
QString authorLabel(const QVariantMap &values, bool shortForm)
{
    const QString raw = values.value(QStringLiteral("author")).toString().trimmed();
    if (values.value(QStringLiteral("corporate")).toBool()) return raw;
    const auto people = raw.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    QStringList names;
    for (const QString &person : people) {
        const QString name = shortForm ? person.section(QLatin1Char(','), 0, 0).trimmed() : person.trimmed();
        if (!name.isEmpty()) names.append(name);
    }
    if (shortForm && names.size() > 2) return names.first() + QStringLiteral(" et al.");
    if (names.size() == 2) return names.first() + QStringLiteral(" & ") + names.last();
    return names.join(QStringLiteral("; "));
}
QString joinSentence(const QStringList &parts)
{
    QString result;
    for (const auto &part : parts) {
        const QString text = part.trimmed();
        if (text.isEmpty()) continue;
        if (!result.isEmpty()) result += QLatin1Char(' ');
        result += text;
        if (!text.endsWith(QLatin1Char('.')) && !text.endsWith(QLatin1Char('?')) && !text.endsWith(QLatin1Char('!'))) result += QLatin1Char('.');
    }
    return result;
}
} // namespace
QString citationText(const BibliographySource &source, const QString &style, int number, const QString &pages)
{
    QString label;
    if (style == u"numeric") label = QString::number(number);
    else {
        label = authorLabel(source.values, true);
        if (label.isEmpty()) label = source.values.value(QStringLiteral("title")).toString();
        QString year = source.values.value(QStringLiteral("year")).toString().trimmed();
        if (year.isEmpty()) year = QStringLiteral("n.d.");
        label += QStringLiteral(", ") + year;
    }
    if (!pages.trimmed().isEmpty()) label += QStringLiteral(", p. ") + pages.trimmed();
    return (style == u"numeric" ? QStringLiteral("[") : QStringLiteral("(")) + label
        + (style == u"numeric" ? QStringLiteral("]") : QStringLiteral(")"));
}
QString bibliographyText(const BibliographySource &source, const QString &style, int number)
{
    const auto &v = source.values;
    QString author = authorLabel(v, false);
    QString year = v.value(QStringLiteral("year")).toString().trimmed();
    if (year.isEmpty()) year = QStringLiteral("n.d.");
    QStringList parts{author, QStringLiteral("(%1)").arg(year), v.value(QStringLiteral("title")).toString()};
    const QString type = v.value(QStringLiteral("type")).toString();
    if (type == u"JournalArticle") {
        QString journal = v.value(QStringLiteral("journal")).toString();
        const QString volume = v.value(QStringLiteral("volume")).toString();
        const QString issue = v.value(QStringLiteral("issue")).toString();
        if (!volume.isEmpty()) journal += QStringLiteral(", ") + volume;
        if (!issue.isEmpty()) journal += QStringLiteral("(%1)").arg(issue);
        const QString pages = v.value(QStringLiteral("pages")).toString();
        if (!pages.isEmpty()) journal += QStringLiteral(", ") + pages;
        parts.append(journal);
    } else if (type == u"Book") {
        const QString city = v.value(QStringLiteral("city")).toString();
        const QString publisher = v.value(QStringLiteral("publisher")).toString();
        parts.append(city.isEmpty() || publisher.isEmpty() ? city + publisher : city + QStringLiteral(": ") + publisher);
    }
    if (type == u"InternetSite") parts.append(v.value(QStringLiteral("site")).toString());
    QString result = joinSentence(parts);
    const QString url = v.value(QStringLiteral("url")).toString().trimmed();
    if (!url.isEmpty()) result += QLatin1Char(' ') + url;
    const QString accessed = v.value(QStringLiteral("accessed")).toString().trimmed();
    if (!accessed.isEmpty()) result += QStringLiteral(" (accessed %1)").arg(accessed);
    return (style == u"numeric" ? QStringLiteral("[%1] ").arg(number) : QString()) + result;
}
} // namespace QindaOffice::Docx
