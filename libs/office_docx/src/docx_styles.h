// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// WordprocessingML styles (ECMA-376 Part 1 §17.7): document defaults, the
// style hierarchy (w:basedOn), and table styles with their conditional
// formats (w:tblStylePr).
//
// Effective formatting of a paragraph is a stack, lowest first:
//   document defaults → [table style] → paragraph style chain →
//   numbering level → direct formatting;
// of a run: paragraph's effective run properties → character style chain →
// direct run properties. Each layer is a w:pPr / w:rPr element; layers are
// combined with mergeProperties().
//
// AGENT-NOTE (Word behaviour, not the letter of the spec): a table style's
// paragraph and run properties beat the DEFAULT paragraph style ("Normal")
// but lose to any other paragraph style. That is why "Table Grid" cells have
// no space after even though Normal asks for 8 pt — documents rely on it
// (tableStyleBeatsParagraphStyle()).

#include "ooxml_xml.h"

#include <QHash>
#include <QString>

namespace QindaOffice::Docx {

// One w:tblStylePr (or the whole-table layer).
struct TableStyleLayer {
    Ooxml::XmlElementPtr pPr;
    Ooxml::XmlElementPtr rPr;
    Ooxml::XmlElementPtr tblPr;
    Ooxml::XmlElementPtr trPr;
    Ooxml::XmlElementPtr tcPr;
};

struct Style {
    QString id;
    QString name;
    QString type; // paragraph | character | table | numbering
    QString basedOn;
    QString next;
    bool isDefault = false;
    Ooxml::XmlElementPtr pPr;
    Ooxml::XmlElementPtr rPr;
    TableStyleLayer table;                    // tblPr/trPr/tcPr of a table style
    QHash<QString, TableStyleLayer> conditions; // w:tblStylePr w:type → layer
};

// `over` laid on top of `base`. Children with the same name merge
// recursively (attributes of the newer element win); children only one side
// has are kept; w:tab stops merge by position and w:tab w:val="clear"
// removes the inherited stop. Either side may be null.
[[nodiscard]] Ooxml::XmlElementPtr mergeProperties(const Ooxml::XmlElement *base,
                                                   const Ooxml::XmlElement *over);

class StyleSheet {
public:
    // Parses a w:styles root; a null root yields an empty sheet (Word's
    // built-in defaults: 10 pt Times New Roman, no spacing).
    [[nodiscard]] static StyleSheet parse(const Ooxml::XmlElement *root);

    [[nodiscard]] const Style *style(const QString &id) const;
    [[nodiscard]] QString idForName(const QString &name) const;
    [[nodiscard]] QString defaultParagraphStyle() const { return m_defaultParagraph; }
    [[nodiscard]] QString defaultTableStyle() const { return m_defaultTable; }

    // docDefaults + the paragraph style chain (falls back to the default
    // paragraph style for an empty or unknown id).
    [[nodiscard]] Ooxml::XmlElementPtr paragraphProperties(const QString &styleId) const;
    [[nodiscard]] Ooxml::XmlElementPtr paragraphRunProperties(const QString &styleId) const;
    // The character style chain only (no defaults).
    [[nodiscard]] Ooxml::XmlElementPtr characterRunProperties(const QString &styleId) const;
    // The numbering properties a paragraph style chain carries, if any.
    [[nodiscard]] const Ooxml::XmlElement *styleNumbering(const QString &styleId) const;

    // A table style's whole-table layer and conditional layers, with the
    // w:basedOn chain merged in.
    [[nodiscard]] TableStyleLayer tableLayer(const QString &styleId) const;
    [[nodiscard]] TableStyleLayer tableCondition(const QString &styleId, const QString &type) const;

    // The heading level (1–9) a paragraph style carries through its name
    // ("heading 2") or w:outlineLvl, else 0.
    [[nodiscard]] int headingLevel(const QString &styleId) const;

    [[nodiscard]] bool tableStyleBeatsParagraphStyle(const QString &paragraphStyleId) const;

    [[nodiscard]] bool isEmpty() const { return m_styles.isEmpty(); }
    // Every style id, and a style's w:basedOn chain, root first (M73).
    [[nodiscard]] QStringList styleIds() const { return m_styles.keys(); }
    [[nodiscard]] QStringList chainOf(const QString &id) const;

private:
    // basedOn chain, root ancestor first; cycle- and depth-safe.
    [[nodiscard]] QList<const Style *> chain(const QString &id) const;

    QHash<QString, Style> m_styles;
    QHash<QString, QString> m_idByName; // lower-case name → id
    QString m_defaultParagraph;
    QString m_defaultCharacter;
    QString m_defaultTable;
    Ooxml::XmlElementPtr m_defaultPPr;
    Ooxml::XmlElementPtr m_defaultRPr;
    mutable QHash<QString, Ooxml::XmlElementPtr> m_pPrCache;
    mutable QHash<QString, Ooxml::XmlElementPtr> m_rPrCache;
};

// The text of an element attribute in the W namespace (w:val, …).
[[nodiscard]] QString wAttr(const Ooxml::XmlElement *element, QStringView name,
                            const QString &fallback = QString());
// The w:val of child `name`, or `fallback` when the child is absent.
[[nodiscard]] QString childVal(const Ooxml::XmlElement *parent, QStringView name,
                               const QString &fallback = QString());
// An on/off property: absent → `absent`; present without w:val → true.
[[nodiscard]] bool onOff(const Ooxml::XmlElement *parent, QStringView name, bool absent = false);

} // namespace QindaOffice::Docx
