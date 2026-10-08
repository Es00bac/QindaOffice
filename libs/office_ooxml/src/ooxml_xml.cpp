// SPDX-License-Identifier: GPL-3.0-or-later
#include "ooxml_xml.h"

#include "ooxml_namespaces.h"

#include <QHash>
#include <QSet>
#include <QXmlStreamReader>

namespace QindaOffice::Ooxml {

namespace {

// Attributes whose VALUES name namespaces by prefix (see header).
bool isPrefixListAttribute(const QString &ns, const QString &name)
{
    if (ns == QStringView(Ns::MC)) {
        return name == u"Ignorable" || name == u"ProcessContent" || name == u"MustUnderstand";
    }
    return false;
}

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
                pruneIndentation(*stack.last());
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

// ---- serialisation ------------------------------------------------------------------

QString xmlSafeText(QStringView text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        const char16_t u = c.unicode();
        if (u < 0x20 && u != 0x09 && u != 0x0A && u != 0x0D) {
            continue;
        }
        if (u == 0xFFFE || u == 0xFFFF) {
            continue;
        }
        if (c.isHighSurrogate()) {
            if (i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
                out.append(c);
                out.append(text.at(++i));
            }
            continue;
        }
        if (c.isLowSurrogate()) {
            continue; // lone low surrogate
        }
        out.append(c);
    }
    return out;
}

namespace {

class Serializer {
public:
    QString prefixFor(const QString &uri)
    {
        if (uri.isEmpty()) {
            return {};
        }
        const auto found = m_prefixes.constFind(uri);
        if (found != m_prefixes.constEnd()) {
            return found.value();
        }
        if (m_defaultUri.isEmpty() && Ns::isDefaultNamespace(uri)) {
            m_defaultUri = uri;
            m_prefixes.insert(uri, QString());
            m_order.append(uri);
            return {};
        }
        QString prefix = Ns::conventionalPrefix(uri);
        if (prefix.isEmpty() || m_used.contains(prefix)) {
            do {
                prefix = QStringLiteral("ns%1").arg(m_generated++);
            } while (m_used.contains(prefix));
        }
        m_prefixes.insert(uri, prefix);
        m_used.insert(prefix);
        m_order.append(uri);
        return prefix;
    }

    // First pass: assign prefixes in a stable order (root first).
    void collect(const XmlElement &element)
    {
        prefixFor(element.ns);
        for (const XmlAttribute &a : element.attributes) {
            if (!a.ns.isEmpty() && a.ns != QStringView(Ns::XML)) {
                prefixFor(a.ns);
            }
            collectUris(a.value);
        }
        for (const XmlChild &c : element.children) {
            if (c.element) {
                collect(*c.element);
            }
        }
    }

    void write(const XmlElement &element, bool declareNamespaces)
    {
        m_out += QLatin1Char('<');
        writeName(element.ns, element.name);
        if (declareNamespaces) {
            for (const QString &uri : std::as_const(m_order)) {
                if (uri == QStringView(Ns::XML)) {
                    continue;
                }
                const QString prefix = m_prefixes.value(uri);
                m_out += prefix.isEmpty() ? QStringLiteral(" xmlns=\"") : QStringLiteral(" xmlns:") + prefix + QStringLiteral("=\"");
                escape(uri, true);
                m_out += QLatin1Char('"');
            }
        }
        for (const XmlAttribute &a : element.attributes) {
            m_out += QLatin1Char(' ');
            if (!a.ns.isEmpty()) {
                m_out += (a.ns == QStringView(Ns::XML) ? QStringLiteral("xml") : m_prefixes.value(a.ns));
                m_out += QLatin1Char(':');
            }
            m_out += a.name + QStringLiteral("=\"");
            escape(a.value.contains(QLatin1Char('{')) ? urisToPrefixes(a.value) : a.value, true);
            m_out += QLatin1Char('"');
        }
        if (element.children.isEmpty()) {
            m_out += QStringLiteral("/>");
            return;
        }
        m_out += QLatin1Char('>');
        for (const XmlChild &c : element.children) {
            if (c.element) {
                write(*c.element, false);
            } else {
                escape(c.text, false);
            }
        }
        m_out += QStringLiteral("</");
        writeName(element.ns, element.name);
        m_out += QLatin1Char('>');
    }

    QString take() { return std::move(m_out); }

private:
    void writeName(const QString &ns, const QString &name)
    {
        const QString prefix = m_prefixes.value(ns);
        if (!prefix.isEmpty()) {
            m_out += prefix + QLatin1Char(':');
        }
        m_out += name;
    }

    // "{uri}" tokens inside prefix-list attribute values.
    void collectUris(const QString &value)
    {
        if (!value.contains(QLatin1Char('{'))) {
            return;
        }
        for (const QString &token : value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (token.startsWith(QLatin1Char('{')) && token.endsWith(QLatin1Char('}'))) {
                prefixFor(token.mid(1, token.size() - 2));
            }
        }
    }

    QString urisToPrefixes(const QString &value)
    {
        QStringList out;
        for (const QString &token : value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (token.startsWith(QLatin1Char('{')) && token.endsWith(QLatin1Char('}'))) {
                out.append(prefixFor(token.mid(1, token.size() - 2)));
            } else {
                out.append(token);
            }
        }
        return out.join(QLatin1Char(' '));
    }

    void escape(QStringView text, bool attribute)
    {
        const QString safe = xmlSafeText(text);
        for (const QChar c : safe) {
            switch (c.unicode()) {
            case u'&': m_out += QStringLiteral("&amp;"); break;
            case u'<': m_out += QStringLiteral("&lt;"); break;
            case u'>': m_out += QStringLiteral("&gt;"); break;
            case u'"':
                m_out += attribute ? QStringLiteral("&quot;") : QStringLiteral("\"");
                break;
            case u'\n': m_out += attribute ? QStringLiteral("&#10;") : QStringLiteral("\n"); break;
            case u'\r': m_out += QStringLiteral("&#13;"); break;
            case u'\t': m_out += attribute ? QStringLiteral("&#9;") : QStringLiteral("\t"); break;
            default: m_out += c; break;
            }
        }
    }

    QHash<QString, QString> m_prefixes; // uri → prefix
    QSet<QString> m_used;
    QStringList m_order;
    QString m_defaultUri; // the one namespace written without a prefix
    int m_generated = 0;
    QString m_out;
};

} // namespace

QByteArray serializeDocument(const XmlElement &root)
{
    Serializer serializer;
    serializer.collect(root);
    serializer.write(root, true);
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n")
        + serializer.take().toUtf8();
}

QString serializeFragment(const XmlElement &element)
{
    Serializer serializer;
    serializer.collect(element);
    serializer.write(element, true);
    return serializer.take();
}

} // namespace QindaOffice::Ooxml
