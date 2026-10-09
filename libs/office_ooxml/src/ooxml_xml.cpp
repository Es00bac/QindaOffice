// SPDX-License-Identifier: GPL-3.0-or-later
#include "ooxml_xml.h"

#include "ooxml_namespaces.h"
#include "ooxml_xml_p.h"

#include <QHash>
#include <QXmlStreamReader>

namespace QindaOffice::Ooxml {

namespace {

using Scope = QList<QHash<QString, QString>>; // prefix → uri, innermost last

QString lookupPrefix(const Scope &scope, const QString &prefix)
{
    for (auto it = scope.crbegin(); it != scope.crend(); ++it) {
        const auto found = it->constFind(prefix);
        if (found != it->constEnd()) {
            return found.value();
        }
    }
    return {};
}

// "w14 wp14" → "{uri-of-w14} {uri-of-wp14}"; unknown prefixes stay as-is.
QString prefixesToUris(const QString &value, const Scope &scope)
{
    QStringList out;
    for (const QString &token : value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QString uri = lookupPrefix(scope, token);
        out.append(uri.isEmpty() ? token : QLatin1Char('{') + Ns::normalizeNamespace(uri) + QLatin1Char('}'));
    }
    return out.join(QLatin1Char(' '));
}

bool hasElementChild(const XmlElement &element)
{
    for (const XmlChild &child : element.children) {
        if (child.element) {
            return true;
        }
    }
    return false;
}

// Drops whitespace-only text between child elements (see AGENT-GUARD).
void pruneIndentation(XmlElement &element)
{
    if (!hasElementChild(element)) {
        return;
    }
    element.children.removeIf([](const XmlChild &child) {
        return !child.element && child.text.trimmed().isEmpty();
    });
}

} // namespace

// ---- queries ------------------------------------------------------------------------

const XmlElement *XmlElement::child(QStringView nsUri, QStringView localName) const
{
    for (const XmlChild &c : children) {
        if (c.element && c.element->is(nsUri, localName)) {
            return c.element.get();
        }
    }
    return nullptr;
}

XmlElement *XmlElement::child(QStringView nsUri, QStringView localName)
{
    for (XmlChild &c : children) {
        if (c.element && c.element->is(nsUri, localName)) {
            return c.element.get();
        }
    }
    return nullptr;
}

QList<const XmlElement *> XmlElement::elements() const
{
    QList<const XmlElement *> out;
    for (const XmlChild &c : children) {
        if (c.element) {
            out.append(c.element.get());
        }
    }
    return out;
}

QList<const XmlElement *> XmlElement::elements(QStringView nsUri, QStringView localName) const
{
    QList<const XmlElement *> out;
    for (const XmlChild &c : children) {
        if (c.element && c.element->is(nsUri, localName)) {
            out.append(c.element.get());
        }
    }
    return out;
}

const XmlElement *XmlElement::descendant(QStringView nsUri, QStringView localName) const
{
    for (const XmlChild &c : children) {
        if (!c.element) {
            continue;
        }
        if (c.element->is(nsUri, localName)) {
            return c.element.get();
        }
        if (const XmlElement *found = c.element->descendant(nsUri, localName)) {
            return found;
        }
    }
    return nullptr;
}

bool XmlElement::hasAttribute(QStringView nsUri, QStringView localName) const
{
    for (const XmlAttribute &a : attributes) {
        if (a.name == localName && a.ns == nsUri) {
            return true;
        }
    }
    return false;
}

QString XmlElement::attribute(QStringView nsUri, QStringView localName, const QString &fallback) const
{
    for (const XmlAttribute &a : attributes) {
        if (a.name == localName && a.ns == nsUri) {
            return a.value;
        }
    }
    return fallback;
}

QString XmlElement::attr(QStringView localName, const QString &fallback) const
{
    // Own namespace first (w:val on w:jc), then unqualified (x on a:off).
    for (const XmlAttribute &a : attributes) {
        if (a.name == localName && (a.ns == ns || a.ns.isEmpty())) {
            return a.value;
        }
    }
    return fallback;
}

QString XmlElement::text() const
{
    QString out;
    for (const XmlChild &c : children) {
        out += c.element ? c.element->text() : c.text;
    }
    return out;
}

// ---- builders -----------------------------------------------------------------------

XmlElement &XmlElement::setAttribute(const QString &nsUri, const QString &localName, const QString &value)
{
    for (XmlAttribute &a : attributes) {
        if (a.name == localName && a.ns == nsUri) {
            a.value = value;
            return *this;
        }
    }
    attributes.append({nsUri, localName, value});
    return *this;
}

XmlElement &XmlElement::removeAttribute(QStringView nsUri, QStringView localName)
{
    attributes.removeIf([&](const XmlAttribute &a) { return a.name == localName && a.ns == nsUri; });
    return *this;
}

XmlElement &XmlElement::appendElement(const QString &nsUri, const QString &localName)
{
    return appendElement(std::make_shared<XmlElement>(nsUri, localName));
}

XmlElement &XmlElement::appendElement(XmlElementPtr element)
{
    XmlElement &ref = *element;
    children.append({std::move(element), QString()});
    return ref;
}

void XmlElement::appendText(const QString &text)
{
    if (!children.isEmpty() && !children.last().element) {
        children.last().text += text;
        return;
    }
    children.append({nullptr, text});
}

void XmlElement::removeChildren(QStringView nsUri, QStringView localName)
{
    children.removeIf([&](const XmlChild &c) { return c.element && c.element->is(nsUri, localName); });
}

XmlElement &XmlElement::insertElementBefore(XmlElementPtr element, const QList<QStringView> &before)
{
    XmlElement &ref = *element;
    for (qsizetype i = 0; i < children.size(); ++i) {
        const XmlChild &c = children.at(i);
        if (c.element && before.contains(QStringView(c.element->name))) {
            children.insert(i, {std::move(element), QString()});
            return ref;
        }
    }
    children.append({std::move(element), QString()});
    return ref;
}

XmlElementPtr XmlElement::clone() const
{
    auto copy = std::make_shared<XmlElement>(ns, name);
    copy->attributes = attributes;
    copy->children.reserve(children.size());
    for (const XmlChild &c : children) {
        copy->children.append({c.element ? c.element->clone() : nullptr, c.text});
    }
    return copy;
}

// ---- parsing ------------------------------------------------------------------------

XmlElementPtr parseXml(const QByteArray &data, QString *error)
{
    QXmlStreamReader reader(data);
    reader.setNamespaceProcessing(true);
    Scope scope;
    QList<XmlElement *> stack;
    QList<bool> preserve; // xml:space="preserve" in scope, per open element (see the header)
    XmlElementPtr root;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        switch (token) {
        case QXmlStreamReader::DTD:
            // AGENT-GUARD: OOXML never carries a DTD; refusing one rules out
            // entity-expansion attacks from hostile documents.
            if (error != nullptr) {
                *error = QStringLiteral("XML part declares a DTD");
            }
            return nullptr;
        case QXmlStreamReader::StartElement: {
            QHash<QString, QString> declared;
            for (const QXmlStreamNamespaceDeclaration &d : reader.namespaceDeclarations()) {
                declared.insert(d.prefix().toString(), d.namespaceUri().toString());
            }
            scope.append(declared);
            auto element = std::make_shared<XmlElement>(Ns::normalizeNamespace(reader.namespaceUri()),
                                                        reader.name().toString());
            const QXmlStreamAttributes attrs = reader.attributes();
            element->attributes.reserve(attrs.size());
            for (const QXmlStreamAttribute &a : attrs) {
                XmlAttribute attr{Ns::normalizeNamespace(a.namespaceUri()), a.name().toString(),
                                  a.value().toString()};
                const bool requiresList = attr.ns.isEmpty() && attr.name == u"Requires"
                    && element->ns == QStringView(Ns::MC) && element->name == u"Choice";
                if (requiresList || isPrefixListAttribute(attr.ns, attr.name)) {
                    attr.value = prefixesToUris(attr.value, scope);
                }
                element->attributes.append(std::move(attr));
            }
            XmlElement *raw = element.get();
            const QString space = raw->attribute(Ns::XML, u"space");
            preserve.append(space.isEmpty() ? (!preserve.isEmpty() && preserve.last()) : space == u"preserve");
            if (stack.isEmpty()) {
                root = std::move(element);
            } else {
                stack.last()->appendElement(std::move(element));
            }
            stack.append(raw);
            break;
        }
        case QXmlStreamReader::EndElement:
            if (!stack.isEmpty()) {
                if (!preserve.takeLast()) {
                    pruneIndentation(*stack.last());
                }
                stack.removeLast();
            }
            scope.removeLast();
            break;
        case QXmlStreamReader::Characters:
            if (!stack.isEmpty()) {
                stack.last()->appendText(reader.text().toString());
            }
            break;
        case QXmlStreamReader::EntityReference:
            // Undeclared entity: keep the text Qt resolved (none) — report.
            if (error != nullptr) {
                *error = QStringLiteral("unresolved entity &%1;").arg(reader.name());
            }
            return nullptr;
        default:
            break;
        }
    }
    if (reader.hasError() || !root) {
        if (error != nullptr) {
            *error = reader.hasError()
                ? QStringLiteral("XML error at line %1: %2").arg(reader.lineNumber()).arg(reader.errorString())
                : QStringLiteral("empty XML part");
        }
        return nullptr;
    }
    return root;
}

XmlElementPtr parseFragment(const QString &fragment)
{
    if (fragment.isEmpty()) {
        return nullptr;
    }
    return parseXml(fragment.toUtf8());
}

} // namespace QindaOffice::Ooxml
