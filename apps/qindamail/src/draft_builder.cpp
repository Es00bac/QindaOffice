// SPDX-License-Identifier: GPL-3.0-or-later
#include "draft_builder.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>

namespace QindaOffice::Apps::Mail::Drafts {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("MailDrafts", text);
}

QString unquote(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2 && text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"'))) {
        text = text.mid(1, text.size() - 2);
        text.replace(QStringLiteral("\\\""), QStringLiteral("\""));
        text.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    }
    return text.trimmed();
}

Address parseOne(const QString &part)
{
    const qsizetype open = part.lastIndexOf(QLatin1Char('<'));
    const qsizetype close = part.indexOf(QLatin1Char('>'), open + 1);
    if (open >= 0 && close > open) {
        return {unquote(part.left(open)), part.mid(open + 1, close - open - 1).trimmed()};
    }
    return {QString(), unquote(part)};
}

bool sameAddress(const QString &a, const QString &b)
{
    return !a.isEmpty() && a.compare(b, Qt::CaseInsensitive) == 0;
}

// Drops `own` and duplicates (against `seen`, updated) from a list.
bool isOwn(const QString &address, const QStringList &own)
{
    return own.contains(address, Qt::CaseInsensitive);
}

QList<Address> without(const QList<Address> &list, const QStringList &own, QSet<QString> *seen)
{
    QList<Address> out;
    for (const Address &address : list) {
        const QString key = address.address.toLower();
        if (key.isEmpty() || isOwn(address.address, own) || seen->contains(key)) {
            continue;
        }
        seen->insert(key);
        out.append(address);
    }
    return out;
}

Address senderOf(const QVariantMap &message)
{
    const QString address = message.value(QStringLiteral("senderAddress")).toString();
    QString name = message.value(QStringLiteral("sender")).toString();
    if (sameAddress(name, address)) {
        name.clear();
    }
    return {name, address};
}

QString localDate(const QVariantMap &message, const QLocale &locale)
{
    const QDateTime date = QDateTime::fromString(message.value(QStringLiteral("date")).toString(), Qt::ISODate);
    return date.isValid() ? locale.toString(date.toLocalTime(), QLocale::ShortFormat) : QString();
}

QString bareId(QString id)
{
    id = id.trimmed();
    id.remove(QLatin1Char('<')).remove(QLatin1Char('>'));
    return id.trimmed();
}

QString referencesFor(const QVariantMap &message)
{
    QStringList ids;
    static const QRegularExpression token(QStringLiteral("<([^<>\\s]+)>"));
    const QString existing = message.value(QStringLiteral("references")).toString();
    for (auto it = token.globalMatch(existing); it.hasNext();) {
        ids.append(it.next().captured(1));
    }
    const QString own = bareId(message.value(QStringLiteral("internetMessageId")).toString());
    const QString root = bareId(message.value(QStringLiteral("threadId")).toString());
    // AGENT-NOTE: store records keep only the conversation ROOT (threadId),
    // not the full References chain; root + parent is what RFC 5322 §3.6.4
    // asks for at minimum and what threading clients key on.
    if (ids.isEmpty() && !root.isEmpty() && root != own && root.contains(QLatin1Char('@'))) {
        ids.append(root);
    }
    if (!own.isEmpty() && !ids.contains(own)) {
        ids.append(own);
    }
    QStringList wrapped;
    for (const QString &id : std::as_const(ids)) {
        wrapped.append(QLatin1Char('<') + id + QLatin1Char('>'));
    }
    return wrapped.join(QLatin1Char(' '));
}

QString signatureBlock(const QString &signature)
{
    return signature.trimmed().isEmpty() ? QString() : QStringLiteral("\n\n-- \n") + signature.trimmed();
}

QString decode(const QString &part)
{
    return QUrl::fromPercentEncoding(part.toUtf8());
}

// Internationalized domain → ACE (the toolkit's SMTP speaks ASCII only).
QString asciiAddress(const QString &address)
{
    const qsizetype at = address.lastIndexOf(QLatin1Char('@'));
    if (at < 0) {
        return address;
    }
    const QString domain = address.mid(at + 1);
    for (const QChar c : domain) {
        if (c.unicode() > 0x7e) {
            const QByteArray ace = QUrl::toAce(domain);
            return ace.isEmpty() ? address : address.left(at + 1) + QString::fromLatin1(ace);
        }
    }
    return address;
}

void appendList(QStringList *target, const QString &value)
{
    for (const QString &item : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = item.trimmed();
        if (!trimmed.isEmpty()) {
            target->append(asciiAddress(trimmed));
        }
    }
}

} // namespace

QList<Address> parseAddressList(const QString &text)
{
    QList<Address> out;
    QString current;
    bool quoted = false;
    int angle = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\\') && quoted && i + 1 < text.size()) {
            current += c;
            current += text.at(++i);
            continue;
        }
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (!quoted && c == QLatin1Char('<')) {
            ++angle;
        } else if (!quoted && c == QLatin1Char('>') && angle > 0) {
            --angle;
        } else if (!quoted && angle == 0 && (c == QLatin1Char(',') || c == QLatin1Char(';'))) {
            const Address address = parseOne(current);
            if (!address.address.isEmpty()) {
                out.append(address);
            }
            current.clear();
            continue;
        }
        current += c;
    }
    const Address last = parseOne(current);
    if (!last.address.isEmpty()) {
        out.append(last);
    }
    return out;
}

QString formatAddress(const Address &address)
{
    if (address.name.isEmpty() || sameAddress(address.name, address.address)) {
        return address.address;
    }
    static const QRegularExpression special(QStringLiteral("[()<>\\[\\]:;@\\\\,.\"]"));
    QString name = address.name;
    if (special.match(name).hasMatch()) {
        name.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        name.replace(QStringLiteral("\""), QStringLiteral("\\\""));
        name = QLatin1Char('"') + name + QLatin1Char('"');
    }
    return name + QStringLiteral(" <") + address.address + QLatin1Char('>');
}

QString formatAddressList(const QList<Address> &addresses)
{
    QStringList parts;
    for (const Address &address : addresses) {
        parts.append(formatAddress(address));
    }
    return parts.join(QStringLiteral(", "));
}

QString replySubject(const QString &subject)
{
    static const QRegularExpression prefix(QStringLiteral("^\\s*(re|aw|sv|antw)(\\[\\d+\\])?\\s*:"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QString trimmed = subject.trimmed();
    return prefix.match(trimmed).hasMatch() ? trimmed : QStringLiteral("Re: ") + trimmed;
}

QString forwardSubject(const QString &subject)
{
    static const QRegularExpression prefix(QStringLiteral("^\\s*(fwd?|wg|tr|rv)\\s*:"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QString trimmed = subject.trimmed();
    return prefix.match(trimmed).hasMatch() ? trimmed : QStringLiteral("Fwd: ") + trimmed;
}

QString quoteBody(const QString &body)
{
    QString normalized = body;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    while (normalized.endsWith(QLatin1Char('\n'))) {
        normalized.chop(1);
    }
    QStringList lines = normalized.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        line = line.startsWith(QLatin1Char('>')) ? QLatin1Char('>') + line : QStringLiteral("> ") + line;
    }
    return lines.join(QLatin1Char('\n'));
}

QVariantMap reply(const QVariantMap &message, bool replyAll, const QStringList &ownAddresses,
                  const QString &signature, const QLocale &locale)
{
    const Address sender = senderOf(message);
    const QList<Address> originalTo = parseAddressList(message.value(QStringLiteral("to")).toString());
    const QList<Address> originalCc = parseAddressList(message.value(QStringLiteral("cc")).toString());
    QList<Address> to;
    QList<Address> cc;
    if (isOwn(sender.address, ownAddresses)) {
        // Replying to something this account sent: the conversation
        // continues with its recipients, not with ourselves.
        to = originalTo;
        if (replyAll) {
            cc = originalCc;
        }
    } else {
        to.append(sender);
        if (replyAll) {
            to += originalTo;
            cc = originalCc;
        }
    }
    QSet<QString> seen;
    to = without(to, ownAddresses, &seen);
    cc = without(cc, ownAddresses, &seen);
    if (to.isEmpty() && cc.isEmpty() && !sender.address.isEmpty()) {
        to.append(sender); // a note to self: reply to it anyway
    }
    const QString who = sender.name.isEmpty() ? sender.address : sender.name;
    const QString attribution = tr("On %1, %2 wrote:").arg(localDate(message, locale), who);
    return {
        {QStringLiteral("to"), formatAddressList(to)},
        {QStringLiteral("cc"), formatAddressList(cc)},
        {QStringLiteral("bcc"), QString()},
        {QStringLiteral("subject"), replySubject(message.value(QStringLiteral("subject")).toString())},
        {QStringLiteral("body"), QStringLiteral("\n") + signatureBlock(signature) + QStringLiteral("\n\n")
                                     + attribution + QLatin1Char('\n')
                                     + quoteBody(message.value(QStringLiteral("bodyPlain")).toString())},
        {QStringLiteral("inReplyTo"), bareId(message.value(QStringLiteral("internetMessageId")).toString())},
        {QStringLiteral("references"), referencesFor(message)},
        {QStringLiteral("replyToMessageId"), message.value(QStringLiteral("id"))},
        {QStringLiteral("attachments"), QVariantList()},
    };
}

QVariantMap forward(const QVariantMap &message, const QString &signature, const QLocale &locale)
{
    QStringList header{tr("---------- Forwarded message ----------"),
                       tr("From: %1").arg(formatAddress(senderOf(message))),
                       tr("Date: %1").arg(localDate(message, locale)),
                       tr("Subject: %1").arg(message.value(QStringLiteral("subject")).toString()),
                       tr("To: %1").arg(message.value(QStringLiteral("to")).toString())};
    const QString cc = message.value(QStringLiteral("cc")).toString();
    if (!cc.isEmpty()) {
        header.append(tr("Cc: %1").arg(cc));
    }
    return {
        {QStringLiteral("to"), QString()},
        {QStringLiteral("cc"), QString()},
        {QStringLiteral("bcc"), QString()},
        {QStringLiteral("subject"), forwardSubject(message.value(QStringLiteral("subject")).toString())},
        {QStringLiteral("body"), QStringLiteral("\n") + signatureBlock(signature) + QStringLiteral("\n\n")
                                     + header.join(QLatin1Char('\n')) + QStringLiteral("\n\n")
                                     + message.value(QStringLiteral("bodyPlain")).toString()},
        {QStringLiteral("inReplyTo"), QString()},
        {QStringLiteral("references"), QString()},
        {QStringLiteral("forwardOfMessageId"), message.value(QStringLiteral("id"))},
        {QStringLiteral("attachments"), QVariantList()},
    };
}

QVariantMap parseMailto(const QString &url)
{
    QVariantMap result{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QString()}};
    QString text = url.trimmed();
    if (!text.startsWith(QStringLiteral("mailto:"), Qt::CaseInsensitive)) {
        result.insert(QStringLiteral("error"), tr("Not a mailto: link"));
        return result;
    }
    text = text.mid(7);
    const qsizetype hash = text.indexOf(QLatin1Char('#'));
    if (hash >= 0) {
        text.truncate(hash);
    }
    const qsizetype question = text.indexOf(QLatin1Char('?'));
    const QString path = question < 0 ? text : text.left(question);
    const QString query = question < 0 ? QString() : text.mid(question + 1);

    QStringList to;
    QStringList cc;
    QStringList bcc;
    QString subject;
    QString body;
    QString inReplyTo;
    // AGENT-GUARD (RFC 6068 §2): split the RAW text first, then decode each
    // piece — "%2C" in an address and "%26" in a value are data, not syntax.
    for (const QString &raw : path.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        appendList(&to, decode(raw));
    }
    for (const QString &pair : query.split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
        const qsizetype eq = pair.indexOf(QLatin1Char('='));
        const QString name = decode(eq < 0 ? pair : pair.left(eq)).trimmed().toLower();
        const QString value = eq < 0 ? QString() : decode(pair.mid(eq + 1));
        if (name == QLatin1String("to")) {
            appendList(&to, value);
        } else if (name == QLatin1String("cc")) {
            appendList(&cc, value);
        } else if (name == QLatin1String("bcc")) {
            appendList(&bcc, value);
        } else if (name == QLatin1String("subject")) {
            subject = value;
        } else if (name == QLatin1String("body")) {
            body = value;
            body.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            body.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        } else if (name == QLatin1String("in-reply-to")) {
            inReplyTo = bareId(value);
        }
    }
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("to"), to.join(QStringLiteral(", ")));
    result.insert(QStringLiteral("cc"), cc.join(QStringLiteral(", ")));
    result.insert(QStringLiteral("bcc"), bcc.join(QStringLiteral(", ")));
    result.insert(QStringLiteral("subject"), subject);
    result.insert(QStringLiteral("body"), body);
    result.insert(QStringLiteral("inReplyTo"), inReplyTo);
    return result;
}

} // namespace QindaOffice::Apps::Mail::Drafts
