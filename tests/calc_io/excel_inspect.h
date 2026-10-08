// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Reading a saved package back the way Excel would see it, for the M61
// gates: each cell's t attribute, cached value and formula text (entities
// decoded), shared strings resolved, defined names with their attributes.
// Built on office_ooxml's tree so assertions compare text, not XML bytes.

#include "ooxml_namespaces.h"
#include "ooxml_package.h"
#include "ooxml_xml.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace QindaOffice::Test {

struct SavedCell {
    QString type;    // the t attribute ("" when absent)
    QString value;   // <v> text, or the inline string's text
    QString formula; // <f> text
    bool hasFormula = false;
};

inline QString sml()
{
    return QString::fromUtf16(Ooxml::Ns::S);
}

inline QStringList savedSharedStrings(const Ooxml::Package &package)
{
    QStringList out;
    const Ooxml::XmlElementPtr root = package.xml(QStringLiteral("xl/sharedStrings.xml"));
    if (!root) {
        return out;
    }
    for (const Ooxml::XmlElement *si : root->elements(sml(), u"si")) {
        QString text;
        for (const Ooxml::XmlElement *child : si->elements()) {
            if (child->name == QLatin1String("t") || child->name == QLatin1String("r")) {
                text += child->text(); // rPh (phonetic) runs are not the cell's text
            }
        }
        out.append(text);
    }
    return out;
}

// Cells keyed by their A1 reference; shared-string cells carry the string.
inline QHash<QString, SavedCell> savedCells(const Ooxml::Package &package, const QString &part)
{
    QHash<QString, SavedCell> out;
    const Ooxml::XmlElementPtr root = package.xml(part);
    const Ooxml::XmlElement *data = root ? root->child(sml(), u"sheetData") : nullptr;
    if (data == nullptr) {
        return out;
    }
    const QStringList strings = savedSharedStrings(package);
    for (const Ooxml::XmlElement *row : data->elements(sml(), u"row")) {
        for (const Ooxml::XmlElement *c : row->elements(sml(), u"c")) {
            SavedCell cell;
            cell.type = c->attr(u"t");
            if (const Ooxml::XmlElement *v = c->child(sml(), u"v")) {
                cell.value = v->text();
            }
            if (const Ooxml::XmlElement *is = c->child(sml(), u"is")) {
                cell.value = is->text();
            }
            if (const Ooxml::XmlElement *f = c->child(sml(), u"f")) {
                cell.hasFormula = true;
                cell.formula = f->text();
            }
            if (cell.type == QLatin1String("s")) {
                cell.value = strings.value(cell.value.toInt());
            }
            out.insert(c->attr(u"r"), cell);
        }
    }
    return out;
}

struct SavedName {
    QString name;
    QString localSheetId; // "" when workbook-scoped
    QString hidden;
    QString comment;
    QString body;
};

inline QList<SavedName> savedDefinedNames(const Ooxml::Package &package)
{
    QList<SavedName> out;
    const Ooxml::XmlElementPtr root = package.xml(QStringLiteral("xl/workbook.xml"));
    const Ooxml::XmlElement *names = root ? root->child(sml(), u"definedNames") : nullptr;
    if (names == nullptr) {
        return out;
    }
    for (const Ooxml::XmlElement *name : names->elements(sml(), u"definedName")) {
        out.append({name->attr(u"name"), name->attr(u"localSheetId"), name->attr(u"hidden"),
                    name->attr(u"comment"), name->text()});
    }
    return out;
}

// Every <f> text of every sheet part, for "no ';' anywhere" style checks.
inline QStringList allSavedFormulas(const Ooxml::Package &package)
{
    QStringList out;
    for (const QString &part : package.partNames()) {
        if (!part.startsWith(QLatin1String("xl/worksheets/sheet"))) {
            continue;
        }
        for (const SavedCell &cell : savedCells(package, part)) {
            if (cell.hasFormula) {
                out.append(cell.formula);
            }
        }
    }
    return out;
}

} // namespace QindaOffice::Test
