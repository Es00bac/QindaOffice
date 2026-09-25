// SPDX-License-Identifier: GPL-3.0-or-later
#include "xml_tree.h"

#include <QXmlStreamReader>

namespace QindaOffice::Mail {
namespace {
constexpr int kMaxDepth = 64;
constexpr int kMaxNodes = 200000;
} // namespace

const XmlNode *XmlNode::child(QStringView local) const
{
    for (const XmlNode &node : children)
        if (node.name == local) return &node;
    return nullptr;
}

QList<const XmlNode *> XmlNode::childrenNamed(QStringView local) const
{
    QList<const XmlNode *> out;
    for (const XmlNode &node : children)
        if (node.name == local) out.append(&node);
    return out;
}

QString XmlNode::childText(QStringView local) const
{
    const XmlNode *node = child(local);
    return node ? node->text.trimmed() : QString();
}

const XmlNode *XmlNode::find(QStringView local) const
{
    for (const XmlNode &node : children) {
        if (node.name == local) return &node;
        if (const XmlNode *deeper = node.find(local)) return deeper;
    }
    return nullptr;
}

void XmlNode::findAll(QStringView local, QList<const XmlNode *> *out) const
{
    for (const XmlNode &node : children) {
        if (node.name == local) out->append(&node);
        node.findAll(local, out);
    }
}

bool parseXml(const QByteArray &bytes, XmlNode *root, QString *error)
{
    QXmlStreamReader xml(bytes);
    xml.setNamespaceProcessing(true);
    QList<XmlNode> stack;
    int nodes = 0;
    bool done = false;
    while (!xml.atEnd() && !done) {
        switch (xml.readNext()) {
        case QXmlStreamReader::DTD:
            *error = QStringLiteral("Unexpected DTD in the server response");
            return false;
        case QXmlStreamReader::StartElement: {
            if (stack.size() >= kMaxDepth || ++nodes > kMaxNodes) {
                *error = QStringLiteral("The server response is nested too deeply or too large");
                return false;
            }
            XmlNode node;
            node.ns = xml.namespaceUri().toString();
            node.name = xml.name().toString();
            for (const QXmlStreamAttribute &attribute : xml.attributes())
                node.attributes.insert(attribute.name().toString(), attribute.value().toString());
            stack.append(node);
            break;
        }
        case QXmlStreamReader::Characters:
            if (!stack.isEmpty()) stack.last().text += xml.text();
            break;
        case QXmlStreamReader::EndElement: {
            XmlNode node = stack.takeLast();
            if (stack.isEmpty()) {
                *root = std::move(node);
                done = true;
            } else {
                stack.last().children.append(std::move(node));
            }
            break;
        }
        default:
            break;
        }
    }
    if (xml.hasError() && !done) {
        *error = QStringLiteral("Malformed XML: %1").arg(xml.errorString());
        return false;
    }
    if (!done) {
        *error = QStringLiteral("Empty or truncated XML response");
        return false;
    }
    return true;
}

} // namespace QindaOffice::Mail
