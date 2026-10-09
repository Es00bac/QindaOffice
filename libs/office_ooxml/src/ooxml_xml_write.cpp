// SPDX-License-Identifier: GPL-3.0-or-later
// The XML tree written out (ooxml_xml.h): namespace prefixes chosen once
// per document, prefix-list values mapped back, text made XML-safe. Split
// from ooxml_xml.cpp (the tree and the parser) in M65 to keep both files
// within the 500-line limit.
#include "ooxml_xml.h"

#include "ooxml_namespaces.h"
#include "ooxml_xml_p.h"

#include <QHash>
#include <QSet>

namespace QindaOffice::Ooxml {

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
            if (namesNamespaces(element, a)) {
                collectUris(a.value);
            }
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
            escape(namesNamespaces(element, a) ? urisToPrefixes(a.value) : a.value, true);
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
    // AGENT-GUARD: only attributes the parser rewrote to "{uri}" tokens map
    // back to prefixes; braced GUIDs in ordinary attributes (a:ext/@uri,
    // a16:creationId/@id) became "ns0" when every attribute was mapped.
    static bool namesNamespaces(const XmlElement &element, const XmlAttribute &a)
    {
        if (isPrefixListAttribute(a.ns, a.name)) {
            return true;
        }
        return a.ns.isEmpty() && a.name == u"Requires" && element.ns == QStringView(Ns::MC)
            && element.name == u"Choice";
    }

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
