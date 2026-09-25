// SPDX-License-Identifier: GPL-3.0-or-later
#include "ews_soap.h"

namespace QindaOffice::Mail::Ews {
namespace {
int backOffOf(const XmlNode *messageXml)
{
    if (!messageXml) return -1;
    for (const XmlNode &value : messageXml->children)
        if (value.name == u"Value" && value.attribute(QStringLiteral("Name")) == QLatin1String("BackOffMilliseconds"))
            return value.text.trimmed().toInt();
    return -1;
}
} // namespace

void startT(QXmlStreamWriter &w, const QString &name) { w.writeStartElement(kTypesNs, name); }
void startM(QXmlStreamWriter &w, const QString &name) { w.writeStartElement(kMessagesNs, name); }
void textT(QXmlStreamWriter &w, const QString &name, const QString &value) { w.writeTextElement(kTypesNs, name, value); }

void fieldUri(QXmlStreamWriter &w, const QString &uri)
{
    startT(w, QStringLiteral("FieldURI"));
    w.writeAttribute(QStringLiteral("FieldURI"), uri);
    w.writeEndElement();
}

void itemShape(QXmlStreamWriter &w, const QString &baseShape, const QStringList &fieldUris, bool textBody)
{
    startM(w, QStringLiteral("ItemShape"));
    textT(w, QStringLiteral("BaseShape"), baseShape);
    if (textBody) textT(w, QStringLiteral("BodyType"), QStringLiteral("Text"));
    if (!fieldUris.isEmpty()) {
        startT(w, QStringLiteral("AdditionalProperties"));
        for (const QString &uri : fieldUris) fieldUri(w, uri);
        w.writeEndElement();
    }
    w.writeEndElement();
}

QByteArray envelope(const Header &header, const BodyWriter &body)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument();
    w.writeNamespace(kSoapNs, QStringLiteral("soap"));
    w.writeNamespace(kTypesNs, QStringLiteral("t"));
    w.writeNamespace(kMessagesNs, QStringLiteral("m"));
    w.writeStartElement(kSoapNs, QStringLiteral("Envelope"));
    w.writeStartElement(kSoapNs, QStringLiteral("Header"));
    startT(w, QStringLiteral("RequestServerVersion"));
    w.writeAttribute(QStringLiteral("Version"), header.serverVersion);
    w.writeEndElement();
    if (!header.impersonate.isEmpty()) {
        startT(w, QStringLiteral("ExchangeImpersonation"));
        startT(w, QStringLiteral("ConnectingSID"));
        textT(w, QStringLiteral("SmtpAddress"), header.impersonate);
        w.writeEndElement();
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeStartElement(kSoapNs, QStringLiteral("Body"));
    body(w);
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

QByteArray soapAction(const QString &operation)
{
    return "\"http://schemas.microsoft.com/exchange/services/2006/messages/" + operation.toLatin1() + '"';
}

bool parse(const QByteArray &body, Response *out, QString *error)
{
    out->messages.clear();
    out->fault.clear();
    out->faultCode.clear();
    out->backOffMs = -1;
    if (!parseXml(body, &out->root, error)) return false;
    if (out->root.ns != kSoapNs || out->root.name != QLatin1String("Envelope")) {
        *error = QStringLiteral("The server did not answer with a SOAP envelope");
        return false;
    }
    const XmlNode *soapBody = out->root.child(u"Body");
    if (!soapBody || soapBody->children.isEmpty()) {
        *error = QStringLiteral("The SOAP envelope has no body");
        return false;
    }
    if (const XmlNode *fault = soapBody->child(u"Fault")) {
        out->fault = fault->childText(u"faultstring");
        if (const XmlNode *detail = fault->child(u"detail")) {
            out->faultCode = detail->childText(u"ResponseCode");
            out->backOffMs = backOffOf(detail->child(u"MessageXml"));
        }
        if (out->fault.isEmpty()) out->fault = QStringLiteral("SOAP fault");
        return true;
    }
    const XmlNode *operation = &soapBody->children.first();
    const XmlNode *messages = operation->child(u"ResponseMessages");
    if (!messages) {
        *error = QStringLiteral("The EWS response has no ResponseMessages");
        return false;
    }
    for (const XmlNode &node : messages->children) {
        ResponseMessage message;
        message.responseClass = node.attribute(QStringLiteral("ResponseClass"));
        message.code = node.childText(u"ResponseCode");
        message.text = node.childText(u"MessageText");
        message.backOffMs = backOffOf(node.child(u"MessageXml"));
        message.node = &node;
        out->messages.append(message);
    }
    return true;
}

} // namespace QindaOffice::Mail::Ews
