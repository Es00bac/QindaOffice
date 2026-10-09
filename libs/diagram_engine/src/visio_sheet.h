// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the Visio reader (M65, D-073): a ShapeSheet with Visio's
// inheritance resolved ([MS-VSDX] §2.2.5.4).
//
// A shape stores only what differs from where it inherits from. Cells,
// section rows and the text come from, in order: the shape itself, then its
// MASTER SHAPE (a shape of a master part, named by Master/MasterShape; the
// children of a master instance name only MasterShape), merged cell by cell
// and row by row — an instance may override one cell of one geometry row.
// What neither has comes from the STYLE chain: LineStyle for line cells,
// FillStyle for fill and shadow cells, TextStyle for the text block and the
// Character/Paragraph sections, each style inheriting from its own parents.
// `Del="1"` on a section or row removes the inherited one.
//
// AGENT-NOTE: V is the cached value Visio computed; F is the formula. Only
// V is read ("formula-only cells use their cached value"); V="Themed" means
// the value comes from the document theme (visio_doc.h resolves colours).

#include "visio_xml.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QStringList>

#include <optional>

namespace QindaOffice::Diagram::Visio {

struct Cell {
    QString value;   // V
    QString formula; // F
    QString unit;    // U
    [[nodiscard]] bool themed() const { return value == QLatin1String("Themed"); }
    [[nodiscard]] std::optional<double> number() const;
};

struct Row {
    QString type; // T (Geometry rows: MoveTo, LineTo, ArcTo …)
    QHash<QString, Cell> cells;
    [[nodiscard]] const Cell *cell(QStringView name) const;
    [[nodiscard]] double number(QStringView name, double fallback = 0) const;
};

struct Section {
    QString name;
    int index = 0;              // IX (several Geometry sections per shape)
    QHash<QString, Cell> cells; // section cells: NoFill, NoLine, NoShow …
    QStringList order;          // row keys: indexed rows by IX, named rows as met
    QHash<QString, Row> rows;
    [[nodiscard]] const Row *indexed(int ix) const;
    [[nodiscard]] QList<const Row *> orderedRows() const;
    // Named rows (Property, User) with their names, in order.
    [[nodiscard]] QList<QPair<QString, const Row *>> namedRows() const;
    [[nodiscard]] bool flag(QStringView cellName) const; // V == 1
};

class Sheet {
public:
    // `own` (a <Shape>, <StyleSheet> or <PageSheet>) merged over `base`.
    [[nodiscard]] static Sheet merge(const XmlElement &own, const Sheet *base);

    [[nodiscard]] const Cell *cell(QStringView name) const;
    [[nodiscard]] const Section *section(QStringView name) const;
    // Every section of that name (Geometry), ordered by IX.
    [[nodiscard]] QList<const Section *> sections(QStringView name) const;
    [[nodiscard]] const XmlElement *text() const { return m_text; }
    [[nodiscard]] const XmlElement *foreignData() const { return m_foreign; }

    int lineStyle = -1;
    int fillStyle = -1;
    int textStyle = -1;
    QString type; // the Type attribute: Shape, Group, Foreign, Guide

private:
    QHash<QString, Cell> m_cells;
    QMap<QString, Section> m_sections; // "Geometry#000000", "Character", …
    const XmlElement *m_text = nullptr;
    const XmlElement *m_foreign = nullptr;
};

enum class Category { Line, Fill, Text, Other };
[[nodiscard]] Category categoryOf(QStringView cellName);

// The document's <StyleSheets> with their inheritance chains.
class Styles {
public:
    void load(const XmlElement *styleSheets);
    // `name` from style `id` or the styles it inherits for `category`.
    [[nodiscard]] const Cell *cell(int id, Category category, QStringView name) const;
    // A cell of row `ix` of a text section (Character, Paragraph) along the
    // text chain.
    [[nodiscard]] const Cell *rowCell(int id, QStringView section, int ix, QStringView name) const;
    [[nodiscard]] bool contains(int id) const { return m_styles.contains(id); }
    // The id of the style whose NameU is `nameU`, or -1.
    [[nodiscard]] int idNamed(QStringView nameU) const;

private:
    struct Entry {
        Sheet sheet;
        int parent[3] = {-1, -1, -1}; // line, fill, text
        QString nameU;
    };
    QHash<int, Entry> m_styles;
};

} // namespace QindaOffice::Diagram::Visio
