// SPDX-License-Identifier: GPL-3.0-or-later
// WebDAV XML for CardDAV (carddav_xml.h).

#include "carddav_xml.h"

#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace QindaOffice::Mail::CardDav {
namespace {

constexpr int kMaxDepth = 32;
constexpr const char *kDav = "DAV:";
constexpr const char *kCard = "urn:ietf:params:xml:ns:carddav";
constexpr const char *kServer = "http://calendarserver.org/ns/";

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

void merge(DavResponse *into, const DavResponse &from)
{
    into->resourceTypes += from.resourceTypes;
    into->reports += from.reports;
    for (const auto &[target, value] : {std::pair<QString *, QString>(&into->displayName, from.displayName),
                                        {&into->etag, from.etag}, {&into->ctag, from.ctag},
                                        {&into->syncToken, from.syncToken}, {&into->principal, from.principal},
                                        {&into->homeSet, from.homeSet}}) {
        if (!value.isEmpty()) *target = value;
    }
    if (!from.addressData.isEmpty()) into->addressData = from.addressData;
}

// <?xml …?><root xmlns…> with `body` writing the children.
template <typename Body>
QByteArray document(const char *root, const char *rootNs, Body body)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeNamespace(QString::fromLatin1(kDav), QStringLiteral("d"));
    w.writeNamespace(QString::fromLatin1(kCard), QStringLiteral("card"));
    w.writeNamespace(QString::fromLatin1(kServer), QStringLiteral("cs"));
    w.writeStartElement(QString::fromLatin1(rootNs), QString::fromLatin1(root));
    body(w);
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

QByteArray propfind(std::initializer_list<std::pair<const char *, const char *>> props)
{
    return document("propfind", kDav, [&](QXmlStreamWriter &w) {
        w.writeStartElement(QString::fromLatin1(kDav), QStringLiteral("prop"));
        for (const auto &[ns, name] : props) w.writeEmptyElement(QString::fromLatin1(ns), QString::fromLatin1(name));
        w.writeEndElement();
    });
}

} // namespace

int statusCode(const QString &line)
{
    const QStringList parts = line.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return parts.size() >= 2 ? parts.at(1).toInt() : 0;
}

bool parseMultistatus(const QByteArray &xml, Multistatus *out, QString *error)
{
    *out = Multistatus();
    QXmlStreamReader r(xml);
    QStringList stack;   // open elements (local names)
    DavResponse current, pending;
    QString propstatStatus;
    bool inResponse = false, inPropstat = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.tokenType() == QXmlStreamReader::DTD) return fail(error, QStringLiteral("The server's answer has a DTD"));
        if (r.isStartElement()) {
            const QString name = r.name().toString();
            const QString parent = stack.isEmpty() ? QString() : stack.last();
            const QString grand = stack.size() >= 2 ? stack.at(stack.size() - 2) : QString();
            // Text-only elements are read whole here (readElementText stops on their end).
            if (name == QLatin1String("href") && parent == QLatin1String("response")) {
                current.href = r.readElementText().trimmed();
            } else if (name == QLatin1String("status") && (parent == QLatin1String("propstat") || parent == QLatin1String("response"))) {
                const QString text = r.readElementText();
                if (parent == QLatin1String("propstat")) propstatStatus = text;
                else current.status = statusCode(text);
            } else if (name == QLatin1String("sync-token") && parent == QLatin1String("multistatus")) {
                out->syncToken = r.readElementText().trimmed();
            } else if (inPropstat && parent == QLatin1String("prop")
                       && (name == QLatin1String("displayname") || name == QLatin1String("getetag")
                           || name == QLatin1String("getctag") || name == QLatin1String("sync-token")
                           || name == QLatin1String("address-data"))) {
                const QString text = r.readElementText(QXmlStreamReader::SkipChildElements);
                if (name == QLatin1String("displayname")) pending.displayName = text.trimmed();
                else if (name == QLatin1String("getetag")) pending.etag = text.trimmed();
                else if (name == QLatin1String("getctag")) pending.ctag = text.trimmed();
                else if (name == QLatin1String("sync-token")) pending.syncToken = text.trimmed();
                else pending.addressData = text.toUtf8();
            } else if (inPropstat && name == QLatin1String("href")
                       && (parent == QLatin1String("current-user-principal") || parent == QLatin1String("addressbook-home-set"))) {
                const QString text = r.readElementText().trimmed();
                if (parent == QLatin1String("current-user-principal")) pending.principal = text;
                else if (pending.homeSet.isEmpty()) pending.homeSet = text;
            } else {
                if (stack.size() >= kMaxDepth) return fail(error, QStringLiteral("The server's answer is nested too deeply"));
                stack.append(name);
                if (name == QLatin1String("response")) {
                    current = DavResponse();
                    inResponse = true;
                } else if (name == QLatin1String("propstat") && inResponse) {
                    pending = DavResponse();
                    propstatStatus.clear();
                    inPropstat = true;
                } else if (inPropstat && parent == QLatin1String("resourcetype")) {
                    pending.resourceTypes.append(name);
                } else if (inPropstat && parent == QLatin1String("report") && grand == QLatin1String("supported-report")) {
                    pending.reports.append(name);
                }
            }
        } else if (r.isEndElement()) {
            const QString name = r.name().toString();
            if (!stack.isEmpty()) stack.removeLast();
            if (name == QLatin1String("propstat") && inPropstat) {
                inPropstat = false;
                const int code = statusCode(propstatStatus);
                if (propstatStatus.isEmpty() || (code >= 200 && code < 300)) merge(&current, pending);
            } else if (name == QLatin1String("response") && inResponse) {
                inResponse = false;
                if (!current.href.isEmpty()) {
                    if (out->responses.size() >= kMaxResponses)
                        return fail(error, QStringLiteral("The server listed more than %1 entries").arg(kMaxResponses));
                    out->responses.append(current);
                }
            }
        }
    }
    if (r.hasError()) return fail(error, QStringLiteral("The server's answer is not valid XML: %1").arg(r.errorString()));
    return true;
}

QByteArray propfindDiscovery()
{
    return propfind({{kDav, "current-user-principal"}, {kCard, "addressbook-home-set"}, {kDav, "resourcetype"},
                     {kDav, "displayname"}});
}

QByteArray propfindBooks()
{
    return propfind({{kDav, "resourcetype"}, {kDav, "displayname"}, {kServer, "getctag"}, {kDav, "sync-token"},
                     {kDav, "supported-report-set"}});
}

QByteArray propfindEtags()
{
    return propfind({{kDav, "resourcetype"}, {kDav, "getetag"}});
}

QByteArray syncCollection(const QString &token)
{
    return document("sync-collection", kDav, [&](QXmlStreamWriter &w) {
        w.writeTextElement(QString::fromLatin1(kDav), QStringLiteral("sync-token"), token);
        w.writeTextElement(QString::fromLatin1(kDav), QStringLiteral("sync-level"), QStringLiteral("1"));
        w.writeStartElement(QString::fromLatin1(kDav), QStringLiteral("prop"));
        w.writeEmptyElement(QString::fromLatin1(kDav), QStringLiteral("getetag"));
        w.writeEndElement();
    });
}

QByteArray multiget(const QStringList &hrefs)
{
    return document("addressbook-multiget", kCard, [&](QXmlStreamWriter &w) {
        w.writeStartElement(QString::fromLatin1(kDav), QStringLiteral("prop"));
        w.writeEmptyElement(QString::fromLatin1(kDav), QStringLiteral("getetag"));
        w.writeEmptyElement(QString::fromLatin1(kCard), QStringLiteral("address-data"));
        w.writeEndElement();
        for (const QString &href : hrefs) w.writeTextElement(QString::fromLatin1(kDav), QStringLiteral("href"), href);
    });
}

} // namespace QindaOffice::Mail::CardDav
