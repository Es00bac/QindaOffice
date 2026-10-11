// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Schema validation of Office Open XML parts for the format tests (D-068).
//
// validatePackage() walks every XML part of a saved package, picks the
// ECMA-376 Transitional schema its root element belongs to, applies
// Markup Compatibility preprocessing the way Office does (drops content in
// namespaces the root declares mc:Ignorable, resolves mc:AlternateContent to
// its Fallback, strips mc: attributes), and validates the result with
// `xmllint --schema`. It returns a list of "part: error" strings — empty when
// everything validates.
//
// The schemas are not shipped with the repository: point
// QQO_OOXML_SCHEMA_DIR at a directory holding the Transitional XSDs
// (wml.xsd, pml.xsd, sml.xsd, dml-*.xsd, shared-*.xsd, plus an xml.xsd for
// the XML namespace). schemaValidationAvailable() is false without them (or
// without xmllint) and callers QSKIP that check only.
//
// AGENT-NOTE: Microsoft extension vocabularies (w14, w15, wp14, a14, …) are
// not in ECMA-376. Output that uses them must declare them mc:Ignorable —
// exactly what Office requires of a consumer that does not know them — and
// this preprocessing is what makes such output validate.

#include "ooxml_namespaces.h"
#include "ooxml_package.h"
#include "ooxml_xml.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace QindaOffice::Test {

inline QString ooxmlSchemaDir()
{
    return qEnvironmentVariable("QQO_OOXML_SCHEMA_DIR");
}

inline bool schemaValidationAvailable()
{
    const QString dir = ooxmlSchemaDir();
    return !dir.isEmpty() && QFileInfo::exists(dir + QStringLiteral("/wml.xsd"))
        && !QStandardPaths::findExecutable(QStringLiteral("xmllint")).isEmpty();
}

// The schema file for a part's root namespace, or empty when the part is
// not schema-checked (package parts, extension parts, custom XML).
inline QString schemaForRoot(const Ooxml::XmlElement &root)
{
    using namespace Ooxml;
    if (root.ns == QStringView(Ns::W)) {
        return QStringLiteral("wml.xsd");
    }
    if (root.ns == QStringView(Ns::P)) {
        return QStringLiteral("pml.xsd");
    }
    if (root.ns == QStringView(Ns::S)) {
        return QStringLiteral("sml.xsd");
    }
    if (root.ns == QStringView(Ns::C)) {
        return QStringLiteral("dml-chart.xsd");
    }
    if (root.ns == QStringView(Ns::A)) {
        return QStringLiteral("dml-main.xsd");
    }
    if (root.ns == u"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing") {
        return QStringLiteral("dml-spreadsheetDrawing.xsd");
    }
    if (root.ns == QStringView(Ns::DGM)) {
        return QStringLiteral("dml-diagram.xsd");
    }
    if (root.ns == u"http://schemas.openxmlformats.org/officeDocument/2006/bibliography") return QStringLiteral("shared-bibliography.xsd");
    if (root.ns == QStringView(Ns::EP)) {
        return QStringLiteral("shared-documentPropertiesExtended.xsd");
    }
    return {};
}

namespace detail {

inline const QSet<QString> &ecmaNamespaces()
{
    using namespace Ooxml;
    static const QSet<QString> set{
        QString::fromUtf16(Ns::W), QString::fromUtf16(Ns::R), QString::fromUtf16(Ns::WP),
        QString::fromUtf16(Ns::A), QString::fromUtf16(Ns::PIC), QString::fromUtf16(Ns::C),
        QString::fromUtf16(Ns::DGM), QString::fromUtf16(Ns::P), QString::fromUtf16(Ns::S),
        QString::fromUtf16(Ns::M), QString::fromUtf16(Ns::V), QString::fromUtf16(Ns::O),
        QString::fromUtf16(Ns::W10), QString::fromUtf16(Ns::XML), QString::fromUtf16(Ns::EP),
        QString::fromUtf16(Ns::VT),
        QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/bibliography"),
        QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing"),
        QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chartDrawing"),
        QStringLiteral("urn:schemas-microsoft-com:office:excel"),
        QStringLiteral("urn:schemas-microsoft-com:office:powerpoint"),
    };
    return set;
}

inline QSet<QString> uriTokens(const QString &value)
{
    QSet<QString> out;
    for (const QString &token : value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (token.startsWith(QLatin1Char('{')) && token.endsWith(QLatin1Char('}'))) {
            out.insert(token.mid(1, token.size() - 2));
        }
    }
    return out;
}

// Markup Compatibility preprocessing (ECMA-376 Part 3) of `e`'s children.
inline void preprocess(Ooxml::XmlElement &e, QSet<QString> ignorable)
{
    using namespace Ooxml;
    const QString mc = QString::fromUtf16(Ns::MC);
    ignorable.unite(uriTokens(e.attribute(mc, u"Ignorable")));
    e.attributes.removeIf([&](const XmlAttribute &a) { return a.ns == mc || ignorable.contains(a.ns); });
    QList<XmlChild> out;
    for (XmlChild &child : e.children) {
        if (!child.element) {
            out.append(child);
            continue;
        }
        XmlElement &c = *child.element;
        if (c.is(mc, u"AlternateContent")) {
            const XmlElement *chosen = c.child(mc, u"Fallback");
            for (const XmlElement *choice : c.elements(mc, u"Choice")) {
                const QSet<QString> needs = uriTokens(choice->attr(u"Requires"));
                bool understood = true;
                for (const QString &uri : needs) {
                    understood = understood && ecmaNamespaces().contains(uri);
                }
                if (understood) {
                    chosen = choice;
                    break;
                }
            }
            if (chosen != nullptr) {
                XmlElementPtr copy = chosen->clone();
                preprocess(*copy, ignorable);
                for (const XmlChild &grand : std::as_const(copy->children)) {
                    out.append(grand);
                }
            }
            continue;
        }
        if (ignorable.contains(c.ns)) {
            continue;
        }
        preprocess(c, ignorable);
        out.append(child);
    }
    e.children = out;
}

} // namespace detail

inline QString validateXmlPart(const QByteArray &xml, const QString &xsdFile, int maxLines = 5)
{
    QTemporaryFile file;
    if (!file.open()) {
        return QStringLiteral("cannot write a temporary file");
    }
    file.write(xml);
    file.flush();
    QProcess lint;
    lint.start(QStringLiteral("xmllint"),
               {QStringLiteral("--noout"), QStringLiteral("--nonet"), QStringLiteral("--schema"),
                QDir(ooxmlSchemaDir()).filePath(xsdFile), file.fileName()});
    if (!lint.waitForFinished(60000)) {
        return QStringLiteral("xmllint did not finish");
    }
    if (lint.exitCode() == 0) {
        return {};
    }
    QStringList lines = QString::fromUtf8(lint.readAllStandardError()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    lines.removeIf([](const QString &l) { return l.endsWith(QLatin1String("fails to validate")); });
    for (QString &line : lines) {
        line.replace(file.fileName(), QStringLiteral("line"));
    }
    return lines.mid(0, maxLines).join(QLatin1Char('\n'));
}

// Validates every schema-checked XML part of a package. Empty = valid.
// Each entry is "<part>: <xmllint lines>", at most `maxLines` per part.
inline QStringList validatePackage(const Ooxml::Package &package, int maxLines = 5)
{
    QStringList errors;
    for (const QString &name : package.partNames()) {
        if (!name.endsWith(QLatin1String(".xml"))) {
            continue;
        }
        const Ooxml::XmlElementPtr root = package.xml(name);
        if (!root) {
            errors.append(name + QStringLiteral(": not well-formed"));
            continue;
        }
        const QString xsd = schemaForRoot(*root);
        if (xsd.isEmpty()) {
            continue;
        }
        detail::preprocess(*root, {});
        const QString error = validateXmlPart(Ooxml::serializeDocument(*root), xsd, maxLines);
        if (!error.isEmpty()) {
            errors.append(name + QStringLiteral(": ") + error);
        }
    }
    return errors;
}

} // namespace QindaOffice::Test
