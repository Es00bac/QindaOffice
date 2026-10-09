// SPDX-License-Identifier: GPL-3.0-or-later
// The raw XML of a worksheet's rules (xlsx_rules_internal.h, M78, D-087):
// the <conditionalFormatting> and <dataValidation> elements and Excel 2010's
// x14 rules in the worksheet extLst, as element trees whose nodes know
// where their text lies in the decoded part and which namespaces were in
// scope there — what keeping a rule byte for byte needs.
//
// AGENT-NOTE: QXmlStreamReader reports a token's END offset; the reader's
// offset before readNext() is therefore where the next token — an element's
// '<' — starts (white space between elements is a token of its own). The
// part is decoded to a QString first so the offsets index that string.

#include "xlsx_rules_internal.h"

#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

namespace QindaOffice::Calc::IO::Rules {

namespace {

using Scope = QHash<QString, QString>;

// Reads the element whose StartElement the reader is on (it began at
// `start`) to its end; `scope` holds what its ancestors declared.
RawNode readNode(QXmlStreamReader &reader, qsizetype start, const Scope &scope)
{
    RawNode node;
    node.ns = reader.namespaceUri().toString();
    node.name = reader.name().toString();
    node.qname = reader.qualifiedName().toString();
    node.attributes = reader.attributes();
    node.start = start;
    node.inScope = scope;
    Scope inside = scope;
    for (const QXmlStreamNamespaceDeclaration &declaration : reader.namespaceDeclarations()) {
        node.declared.insert(declaration.prefix().toString(), declaration.namespaceUri().toString());
        inside.insert(declaration.prefix().toString(), declaration.namespaceUri().toString());
    }
    while (!reader.atEnd()) {
        const qsizetype before = reader.characterOffset();
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            node.children.append(readNode(reader, before, inside));
        } else if (token == QXmlStreamReader::EndElement) {
            break;
        } else if (token == QXmlStreamReader::Characters) {
            node.text += reader.text();
        }
    }
    node.end = reader.characterOffset();
    return node;
}

void skipElement(QXmlStreamReader &reader)
{
    reader.skipCurrentElement();
}

Scope pushed(const Scope &scope, const QXmlStreamReader &reader)
{
    Scope inside = scope;
    for (const QXmlStreamNamespaceDeclaration &declaration : reader.namespaceDeclarations()) {
        inside.insert(declaration.prefix().toString(), declaration.namespaceUri().toString());
    }
    return inside;
}

// Inside an x14 container (x14:conditionalFormattings / x14:dataValidations):
// its `wanted` children as nodes.
void readX14Container(QXmlStreamReader &reader, const Scope &scope, QStringView wanted, QList<RawNode> *out)
{
    const Scope inside = pushed(scope, reader);
    while (!reader.atEnd()) {
        const qsizetype before = reader.characterOffset();
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement) {
            return;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        if (reader.name() == wanted && reader.namespaceUri() == QStringView(kX14Ns)) {
            out->append(readNode(reader, before, inside));
        } else {
            skipElement(reader);
        }
    }
}

// The worksheet's extLst: the two entries holding Excel 2010's rules.
void readExtList(QXmlStreamReader &reader, const Scope &scope, RuleXml *out)
{
    const Scope listScope = pushed(scope, reader);
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement) {
            return;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        if (reader.name() != QLatin1String("ext")) {
            skipElement(reader);
            continue;
        }
        const QString uri = reader.attributes().value(QLatin1String("uri")).toString();
        const bool formats = uri.compare(QStringView(kCfExtUri), Qt::CaseInsensitive) == 0;
        const bool validations = uri.compare(QStringView(kDvExtUri), Qt::CaseInsensitive) == 0;
        if (!formats && !validations) {
            skipElement(reader);
            continue;
        }
        const Scope extScope = pushed(listScope, reader);
        while (!reader.atEnd()) {
            const auto inner = reader.readNext();
            if (inner == QXmlStreamReader::EndElement) {
                break;
            }
            if (inner != QXmlStreamReader::StartElement) {
                continue;
            }
            if (formats && reader.name() == QLatin1String("conditionalFormattings")) {
                readX14Container(reader, extScope, u"conditionalFormatting", &out->x14Formattings);
            } else if (validations && reader.name() == QLatin1String("dataValidations")) {
                readX14Container(reader, extScope, u"dataValidation", &out->x14Validations);
            } else {
                skipElement(reader);
            }
        }
    }
}

void collectPrefixes(const RawNode &node, QHash<QString, QString> *used, QSet<QString> *declared)
{
    for (auto it = node.declared.constBegin(); it != node.declared.constEnd(); ++it) {
        declared->insert(it.key());
    }
    if (node.qname.contains(QLatin1Char(':'))) {
        used->insert(node.qname.section(QLatin1Char(':'), 0, 0), node.ns);
    }
    for (const QXmlStreamAttribute &attribute : node.attributes) {
        const QString prefix = attribute.prefix().toString();
        if (!prefix.isEmpty() && prefix != QLatin1String("xml")) {
            used->insert(prefix, attribute.namespaceUri().toString());
        }
    }
    for (const RawNode &child : node.children) {
        collectPrefixes(child, used, declared);
    }
}

bool isEcmaNamespace(const QString &uri)
{
    return uri == QStringView(kMainNs) || uri == QStringView(kRelNs) || uri == QStringView(kMcNs);
}

bool isRevisionNamespace(const QString &uri)
{
    return uri.startsWith(QLatin1String("http://schemas.microsoft.com/office/spreadsheetml/20"))
        && uri.contains(QLatin1String("/revision"));
}

} // namespace

const RawNode *RawNode::child(QStringView localName) const
{
    for (const RawNode &node : children) {
        if (node.name == localName) {
            return &node;
        }
    }
    return nullptr;
}

QList<const RawNode *> RawNode::childrenNamed(QStringView localName) const
{
    QList<const RawNode *> out;
    for (const RawNode &node : children) {
        if (node.name == localName) {
            out.append(&node);
        }
    }
    return out;
}

QString RawNode::attr(QStringView localName) const
{
    for (const QXmlStreamAttribute &attribute : attributes) {
        if (attribute.namespaceUri().isEmpty() && attribute.name() == localName) {
            return attribute.value().toString();
        }
    }
    return {};
}

bool RawNode::has(QStringView localName) const
{
    for (const QXmlStreamAttribute &attribute : attributes) {
        if (attribute.namespaceUri().isEmpty() && attribute.name() == localName) {
            return true;
        }
    }
    return false;
}

// The part as text, without a byte-order mark (offsets index this string).
static QString decoded(const QByteArray &xml)
{
    QString text = QString::fromUtf8(xml);
    if (text.startsWith(QChar(0xFEFF))) {
        text.remove(0, 1);
    }
    return text;
}

RuleXml scanRuleXml(const QByteArray &sheetXml)
{
    RuleXml out;
    out.text = decoded(sheetXml);
    QXmlStreamReader reader(out.text);
    Scope rootScope;
    int depth = 0;
    while (!reader.atEnd()) {
        const qsizetype before = reader.characterOffset();
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement) {
            --depth;
            continue;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        if (depth == 0) {
            rootScope = pushed({}, reader); // <worksheet>
            ++depth;
            continue;
        }
        // A child of <worksheet>.
        const QStringView name = reader.name();
        if (name == QLatin1String("conditionalFormatting")) {
            out.conditionalFormattings.append(readNode(reader, before, rootScope));
        } else if (name == QLatin1String("dataValidations")) {
            const RawNode list = readNode(reader, before, rootScope);
            for (const RawNode &child : list.children) {
                if (child.name == QLatin1String("dataValidation")) {
                    out.dataValidations.append(child);
                }
            }
        } else if (name == QLatin1String("extLst")) {
            readExtList(reader, rootScope, &out);
        } else {
            skipElement(reader);
        }
    }
    return out;
}

QList<RawNode> scanDxfs(const QByteArray &stylesXml, QString *text)
{
    QList<RawNode> out;
    *text = decoded(stylesXml);
    QXmlStreamReader reader(*text);
    Scope rootScope;
    int depth = 0;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement) {
            --depth;
            continue;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        if (depth == 0) {
            rootScope = pushed({}, reader); // <styleSheet>
            ++depth;
            continue;
        }
        if (reader.name() != QLatin1String("dxfs")) {
            skipElement(reader);
            continue;
        }
        const Scope inside = pushed(rootScope, reader);
        while (!reader.atEnd()) {
            const qsizetype before = reader.characterOffset();
            const auto inner = reader.readNext();
            if (inner == QXmlStreamReader::EndElement) {
                break;
            }
            if (inner == QXmlStreamReader::StartElement) {
                if (reader.name() == QLatin1String("dxf")) {
                    out.append(readNode(reader, before, inside));
                } else {
                    skipElement(reader);
                }
            }
        }
    }
    return out;
}

QString startTag(const QString &text, const RawNode &node)
{
    char16_t quote = 0;
    for (qsizetype i = node.start + 1; i < node.end; ++i) {
        const char16_t c = text.at(i).unicode();
        if (quote != 0) {
            quote = c == quote ? 0 : quote;
        } else if (c == u'"' || c == u'\'') {
            quote = c;
        } else if (c == u'>') {
            return text.mid(node.start, i + 1 - node.start);
        }
    }
    return text.mid(node.start, node.end - node.start);
}

QString selfContained(const QString &text, const RawNode &node, const QHash<QString, QString> &provided)
{
    QString slice = text.mid(node.start, node.end - node.start);
    QHash<QString, QString> used;
    QSet<QString> declared;
    collectPrefixes(node, &used, &declared);
    QString inject;
    QStringList foreign;
    for (auto it = used.constBegin(); it != used.constEnd(); ++it) {
        const QString &prefix = it.key();
        if (declared.contains(prefix) || (provided.contains(prefix) && provided.value(prefix) == it.value())) {
            continue;
        }
        if (isRevisionNamespace(it.value())) {
            // xr:uid and its kin: revision ids Excel makes afresh; without
            // their declaration they would make the part malformed.
            static const QString kAttribute = QStringLiteral("\\s+%1:[\\w.-]+\\s*=\\s*(\"[^\"]*\"|'[^']*')");
            slice.remove(QRegularExpression(kAttribute.arg(QRegularExpression::escape(prefix))));
            continue;
        }
        inject += QStringLiteral(" xmlns:%1=\"%2\"").arg(prefix, attrEscaped(it.value()));
        if (!isEcmaNamespace(it.value())) {
            foreign.append(prefix);
        }
    }
    const bool defaultMissing = !node.qname.contains(QLatin1Char(':')) && !declared.contains(QString())
        && provided.value(QString()) != node.ns;
    if (defaultMissing) {
        inject += QStringLiteral(" xmlns=\"%1\"").arg(attrEscaped(node.ns));
    }
    const bool spreadsheetElement = node.ns == QStringView(kMainNs);
    if (!foreign.isEmpty() && spreadsheetElement && !slice.left(slice.indexOf(QLatin1Char('>'))).contains(QLatin1String("Ignorable"))) {
        if (!used.contains(QStringLiteral("mc")) && !provided.contains(QStringLiteral("mc"))) {
            inject += QStringLiteral(" xmlns:mc=\"%1\"").arg(QString::fromUtf16(kMcNs));
        }
        inject += QStringLiteral(" mc:Ignorable=\"%1\"").arg(foreign.join(QLatin1Char(' ')));
    }
    if (!inject.isEmpty()) {
        slice.insert(1 + node.qname.size(), inject);
    }
    return slice;
}

QHash<QString, QString> worksheetScope()
{
    return {{QString(), QString::fromUtf16(kMainNs)}, {QStringLiteral("r"), QString::fromUtf16(kRelNs)}};
}

QHash<QString, QString> x14Scope(bool withXm)
{
    QHash<QString, QString> scope = worksheetScope();
    scope.insert(QStringLiteral("x14"), QString::fromUtf16(kX14Ns));
    if (withXm) {
        scope.insert(QStringLiteral("xm"), QString::fromUtf16(kXmNs));
    }
    return scope;
}

} // namespace QindaOffice::Calc::IO::Rules
