// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// WordprocessingML numbering (ECMA-376 Part 1 §17.9): abstract numbering
// definitions (nine levels each: format, text such as "%1.%2.", start,
// indentation, bullet font) and the numbering instances (w:num) paragraphs
// point at through w:numPr, with per-level start overrides.

#include "ooxml_xml.h"

#include <QHash>
#include <QList>
#include <QString>

namespace QindaOffice::Docx {

struct NumberingLevel {
    int start = 1;
    QString format = QStringLiteral("decimal"); // w:numFmt (bullet, decimal, lowerLetter, …)
    QString text;                                // w:lvlText ("%1.", "", "(%1)", "%1.%2.")
    QString justification;                       // w:lvlJc
    QString suffix = QStringLiteral("tab");      // w:suff
    int restartAfterLevel = -2;                  // w:lvlRestart (−2 = default rule)
    bool legal = false;                          // w:isLgl
    Ooxml::XmlElementPtr pPr;                    // indentation, tabs
    Ooxml::XmlElementPtr rPr;                    // the bullet's font/colour/size

    [[nodiscard]] bool isBullet() const { return format == u"bullet"; }
};

// One w:lvl.
[[nodiscard]] NumberingLevel parseNumberingLevel(const Ooxml::XmlElement *lvl);

class Numbering {
public:
    [[nodiscard]] static Numbering parse(const Ooxml::XmlElement *root);

    // The level definition for a paragraph's (numId, ilvl), overrides
    // applied; nullptr when numId is 0 ("no numbering") or unknown.
    [[nodiscard]] const NumberingLevel *level(int numId, int ilvl) const;
    // The first number of that level for this instance (startOverride wins).
    [[nodiscard]] int start(int numId, int ilvl) const;
    // The abstract definition an instance uses (instances sharing one keep
    // counting together in Word).
    [[nodiscard]] QString abstractIdOf(int numId) const;
    // The instance's w:startOverride per level (nine entries, −1 none). An
    // instance that overrides a start keeps its own count (M73, D-082).
    [[nodiscard]] QList<int> startOverrides(int numId) const;
    // The instance's definition as one self-contained w:abstractNum
    // fragment: its abstract (a numbering-style link followed) with the
    // instance's own w:lvlOverride/w:lvl levels in place. Exactly the
    // source abstract's markup when the instance overrides no level, so the
    // writer can tell an untouched list from a changed one (docx_lists.h).
    [[nodiscard]] QString definitionXml(int numId) const;
    [[nodiscard]] bool isEmpty() const { return m_instances.isEmpty(); }

private:
    struct Abstract {
        QHash<int, NumberingLevel> levels;
        QString numStyleLink; // defined by a numbering style instead
        Ooxml::XmlElementPtr xml;
    };
    struct Instance {
        QString abstractId;
        QHash<int, int> startOverrides;
        QHash<int, NumberingLevel> levelOverrides;
        QHash<int, Ooxml::XmlElementPtr> levelOverrideXml;
    };
    QHash<QString, Abstract> m_abstracts;
    QHash<int, Instance> m_instances;
    QHash<QString, QString> m_styleLinks; // numbering style id → abstract id
    mutable QHash<int, QString> m_definitions;
};

} // namespace QindaOffice::Docx
