// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A small namespace-aware XML tree for Office Open XML parts.
//
// Every OOXML mapper in the suite reads a part into this tree, and every
// writer builds one and serialises it. One tree for both directions is what
// lets a document keep content the suite cannot edit: a reader stores the
// untouched subtree (as a self-contained fragment string, see
// serializeFragment), and the writer parses it back and emits it in place,
// with relationship ids rewritten by RelationshipCopier.
//
// Names are (namespace URI, local name) pairs. Prefixes from the source
// document are NOT kept — on output every known URI gets its conventional
// prefix (ooxml_namespaces.h). The markup-compatibility attributes that
// name namespaces by prefix (mc:Ignorable, mc:ProcessContent,
// mc:MustUnderstand and mc:Choice/@Requires) are rewritten to "{uri}"
// tokens while parsing and back to prefixes while writing, so a document
// whose producer chose odd prefixes stays valid.
//
// AGENT-GUARD: whitespace-only text between child ELEMENTS is dropped while
// parsing (it is indentation, insignificant in every OOXML vocabulary);
// whitespace in an element WITHOUT child elements is kept — that is how
// <w:t xml:space="preserve"> </w:t> survives. Changing this rule either
// loses spaces in documents or bloats every tree with indentation nodes.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringView>

#include <memory>

namespace QindaOffice::Ooxml {

class XmlElement;
using XmlElementPtr = std::shared_ptr<XmlElement>;

struct XmlAttribute {
    QString ns; // namespace URI; empty for an unqualified attribute
    QString name;
    QString value;
};

// One child: an element, or (element == nullptr) a run of character data.
struct XmlChild {
    XmlElementPtr element;
    QString text;
};

class XmlElement {
public:
    XmlElement() = default;
    XmlElement(QString nsUri, QString localName)
        : ns(std::move(nsUri))
        , name(std::move(localName))
    {
    }

    QString ns;
    QString name;
    QList<XmlAttribute> attributes;
    QList<XmlChild> children;

    [[nodiscard]] bool is(QStringView nsUri, QStringView localName) const
    {
        return name == localName && ns == nsUri;
    }

    // First child element with this name, or nullptr.
    [[nodiscard]] const XmlElement *child(QStringView nsUri, QStringView localName) const;
    [[nodiscard]] XmlElement *child(QStringView nsUri, QStringView localName);
    // All child elements (document order), optionally filtered by name.
    [[nodiscard]] QList<const XmlElement *> elements() const;
    [[nodiscard]] QList<const XmlElement *> elements(QStringView nsUri, QStringView localName) const;
    // First descendant (depth-first, document order) with this name.
    [[nodiscard]] const XmlElement *descendant(QStringView nsUri, QStringView localName) const;

    [[nodiscard]] bool hasAttribute(QStringView nsUri, QStringView localName) const;
    [[nodiscard]] QString attribute(QStringView nsUri, QStringView localName,
                                    const QString &fallback = QString()) const;
    // Shorthand for the many OOXML attributes in the element's own
    // namespace (w:val on a w: element, …) or unqualified (a:off/@x).
    [[nodiscard]] QString attr(QStringView localName, const QString &fallback = QString()) const;

    // Concatenated character data of this element and its descendants.
    [[nodiscard]] QString text() const;

    // Builders. Return the element they touched so calls chain.
    XmlElement &setAttribute(const QString &nsUri, const QString &localName, const QString &value);
    XmlElement &removeAttribute(QStringView nsUri, QStringView localName);
    XmlElement &appendElement(const QString &nsUri, const QString &localName);
    XmlElement &appendElement(XmlElementPtr element);
    void appendText(const QString &text);
    void removeChildren(QStringView nsUri, QStringView localName);
    // Inserts before the first child element whose local name is in
    // `before` (schema order); appends when none is present.
    XmlElement &insertElementBefore(XmlElementPtr element, const QList<QStringView> &before);

    [[nodiscard]] XmlElementPtr clone() const;
};

// Parses a complete XML document and returns its root element; nullptr
// (with `error`) on malformed input. DTDs are rejected (no entity
// expansion beyond the predefined five).
[[nodiscard]] XmlElementPtr parseXml(const QByteArray &data, QString *error = nullptr);

// Serialises `root` as a standalone part: XML declaration, then the root
// with every namespace the tree uses declared on it.
[[nodiscard]] QByteArray serializeDocument(const XmlElement &root);

// Serialises `element` as a self-contained fragment (no XML declaration,
// namespaces declared on the fragment root). This is the storage form for
// preserved XML; parseXml() reads it back.
[[nodiscard]] QString serializeFragment(const XmlElement &element);

// Parses a fragment produced by serializeFragment(); nullptr when invalid.
[[nodiscard]] XmlElementPtr parseFragment(const QString &fragment);

// Character data with the characters XML 1.0 cannot carry removed (C0
// controls other than tab/newline/return, lone surrogates, U+FFFE/FFFF).
[[nodiscard]] QString xmlSafeText(QStringView text);

} // namespace QindaOffice::Ooxml
