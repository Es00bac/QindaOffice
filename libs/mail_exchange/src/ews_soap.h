// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Exchange Web Services SOAP 1.1 framing: the request envelope (with
// RequestServerVersion and optional ExchangeImpersonation headers), written
// with QXmlStreamWriter so every value is escaped, and the response
// envelope: per-item ResponseMessages (ResponseClass/ResponseCode) or a
// SOAP Fault, including the server's BackOffMilliseconds when throttled.
// Reference: "EWS XML elements in Exchange" and "EWS throttling in
// Exchange" (Microsoft Learn).

#include "xml_tree.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QXmlStreamWriter>
#include <functional>

namespace QindaOffice::Mail::Ews {

inline const QString kSoapNs = QStringLiteral("http://schemas.xmlsoap.org/soap/envelope/");
inline const QString kTypesNs = QStringLiteral("http://schemas.microsoft.com/exchange/services/2006/types");
inline const QString kMessagesNs = QStringLiteral("http://schemas.microsoft.com/exchange/services/2006/messages");

struct Header {
    QString serverVersion = QStringLiteral("Exchange2013_SP1");
    QString impersonate;            // SMTP address; empty = the signed-in mailbox
};

using BodyWriter = std::function<void(QXmlStreamWriter &)>;
QByteArray envelope(const Header &header, const BodyWriter &body);
QByteArray soapAction(const QString &operation);

// Writer shorthands: elements in the types (t:) and messages (m:) namespaces.
void startT(QXmlStreamWriter &w, const QString &name);
void startM(QXmlStreamWriter &w, const QString &name);
void textT(QXmlStreamWriter &w, const QString &name, const QString &value);
void fieldUri(QXmlStreamWriter &w, const QString &uri);
// <m:ItemShape><t:BaseShape>…</t:BaseShape>[<t:BodyType>Text</t:BodyType>]<t:AdditionalProperties>…
void itemShape(QXmlStreamWriter &w, const QString &baseShape, const QStringList &fieldUris, bool textBody = false);

struct ResponseMessage {
    QString responseClass;          // Success | Warning | Error
    QString code;                   // NoError, ErrorItemNotFound, ...
    QString text;                   // MessageText
    int backOffMs = -1;
    const XmlNode *node = nullptr;  // the *ResponseMessage element (owned by Response::root)
    bool success() const { return responseClass == QLatin1String("Success"); }
};

struct Response {
    XmlNode root;
    QList<ResponseMessage> messages;
    QString fault, faultCode;       // SOAP Fault faultstring and detail ResponseCode
    int backOffMs = -1;
};

// Parses a response envelope. False with `error` when the body is not SOAP.
// AGENT-GUARD: Response is not copyable in practice: `messages[i].node`
// points into `root`; keep the Response alive while using them.
bool parse(const QByteArray &body, Response *out, QString *error);

} // namespace QindaOffice::Mail::Ews
