// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The M61 package gate (D-069): every package a calc_io test writes must
// validate against the ECMA-376 Transitional schemas (support/ooxml_schema.h
// does the Markup Compatibility preprocessing and runs xmllint). Only the
// schema STEP is skipped when QQO_OOXML_SCHEMA_DIR or xmllint is missing —
// the test that called it still runs and asserts everything else.
//
// AGENT-NOTE: call QQO_VERIFY_XLSX_SCHEMA(path) right after every
// writeXlsxFile() in these tests. A writer change that emits one element out
// of schema order is exactly the kind of defect Excel answers with "We found
// a problem with some content" while LibreOffice opens the file silently.

#include "support/ooxml_schema.h"

#include <QtTest>

namespace QindaOffice::Test {

// AGENT-NOTE: the one allowance this gate makes over validatePackage().
// Excel writes <t xml:space="preserve"> in shared strings, inline strings
// and comments whenever the text starts or ends with white space — and
// needs it to keep those spaces — while ECMA-376's sml.xsd types t as a
// bare ST_Xstring with no attributes at all. Exactly that attribute, on
// exactly SpreadsheetML t elements, is dropped before validation; nothing
// else is relaxed.
inline void dropExcelTextSpace(Ooxml::XmlElement &element)
{
    if (element.ns == QStringView(Ooxml::Ns::S) && element.name == QLatin1String("t")) {
        element.removeAttribute(QStringView(Ooxml::Ns::XML), u"space");
    }
    for (Ooxml::XmlChild &child : element.children) {
        if (child.element) {
            dropExcelTextSpace(*child.element);
        }
    }
}

// validatePackage() for a spreadsheet package, with the allowance above.
inline QStringList validateSpreadsheetPackage(const Ooxml::Package &package)
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
        dropExcelTextSpace(*root);
        const QString error = validateXmlPart(Ooxml::serializeDocument(*root), xsd);
        if (!error.isEmpty()) {
            errors.append(name + QStringLiteral(": ") + error);
        }
    }
    return errors;
}

// Empty when the package validates or validation is unavailable; otherwise
// every schema error, one per line ("part: message").
inline QString xlsxSchemaErrors(const QString &path)
{
    if (!schemaValidationAvailable()) {
        static bool told = false;
        if (!told) {
            told = true;
            qInfo("ECMA-376 schema step skipped: set QQO_OOXML_SCHEMA_DIR and install xmllint");
        }
        return {};
    }
    QString error;
    const std::shared_ptr<const Ooxml::Package> package = Ooxml::Package::load(path, &error);
    if (!package) {
        return QStringLiteral("cannot load %1 as a package: %2").arg(path, error);
    }
    return validateSpreadsheetPackage(*package).join(QLatin1Char('\n'));
}

} // namespace QindaOffice::Test

#define QQO_VERIFY_XLSX_SCHEMA(path)                                                     \
    do {                                                                                 \
        const QString qqoSchemaErrors = QindaOffice::Test::xlsxSchemaErrors(path);       \
        QVERIFY2(qqoSchemaErrors.isEmpty(), qPrintable(qqoSchemaErrors));                \
    } while (false)
