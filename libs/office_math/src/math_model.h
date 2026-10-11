// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The equation model of office_math (M92, D-100): Office Math Markup
// Language (OMML, ECMA-376 Part 1 §22.1) as the tree that the OMML reader
// and writer (math_omml.h), the layout (math_layout.h), the linear formats
// (math_linear.h, math_latex.h) and the editor (math_editor.h) share.
//
// AGENT-NOTE: a structure's PROPERTIES stay XML. Each node keeps its
// property element (m:fPr, m:naryPr, m:rPr, …) as the Ooxml::XmlElement it
// was read from, and math_props.h reads and writes typed values THROUGH
// it. Everything the model does not interpret — m:ctrlPr with the Word or
// DrawingML run properties of a structure's control character, attributes
// and elements a later Office writes — therefore stays in place on save
// without a snapshot to compare against (D-068 rule 2). The ARGUMENTS
// (m:e, m:num, m:sub, …) are modelled, because the layout and the editor
// walk them.
//
// Ownership: nodes are held by shared pointers, so a plain copy of an
// Equation or Argument SHARES its nodes (cheap for reading). clone() is the
// deep copy; mutate only a tree you built or cloned (the editor clones for
// every undo snapshot).

#include "ooxml_xml.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <memory>
#include <optional>

namespace QindaOffice::Math {

// One value per OMML structure, plus Kept for anything else.
enum class NodeKind : quint8 {
    Run,         // m:r          text
    Fraction,    // m:f          num, den
    Superscript, // m:sSup       e, sup
    Subscript,   // m:sSub       e, sub
    SubSup,      // m:sSubSup    e, sub, sup
    PreScript,   // m:sPre       sub, sup, e
    Radical,     // m:rad        deg, e
    Nary,        // m:nary       sub, sup, e
    Delimiter,   // m:d          e+ (separated by sepChr)
    Function,    // m:func       fName, e
    Accent,      // m:acc        e
    Bar,         // m:bar        e
    Box,         // m:box        e
    BorderBox,   // m:borderBox  e
    EqArray,     // m:eqArr      e+ (one per row)
    Matrix,      // m:m          m:mr rows of m:e (args row-major, rowSizes)
    GroupChar,   // m:groupChr   e
    LimLow,      // m:limLow     e, lim
    LimUpp,      // m:limUpp     e, lim
    Phantom,     // m:phant      e
    Kept,        // any other child of a math argument, verbatim
};

struct Node;
using NodePtr = std::shared_ptr<Node>;

// Content of a run in document order: m:t text, or another run child kept
// verbatim (w:br, w:tab, w:sym, …) when `element` is set.
struct RunPiece {
    QString text;
    QList<Ooxml::XmlAttribute> attributes; // on the m:t (xml:space, …)
    Ooxml::XmlElementPtr element;
};

// A child the reader did not expect where it stood, kept verbatim and
// written after the first `after` arguments (Matrix: rows; Equation: lines).
struct KeptChild {
    int after = 0;
    Ooxml::XmlElementPtr element;
};

// CT_OMathArg — m:e, m:num, m:den, m:sub, m:sup, m:deg, m:lim, m:fName —
// and CT_OMath (one line of an equation, which has no argPr/ctrlPr).
struct Argument {
    QList<NodePtr> items;
    Ooxml::XmlElementPtr argPr;  // m:argPr (m:argSz), verbatim
    Ooxml::XmlElementPtr ctrlPr; // the trailing m:ctrlPr, verbatim
    QList<Ooxml::XmlAttribute> attributes;

    [[nodiscard]] bool isEmpty() const { return items.isEmpty(); }
    [[nodiscard]] Argument clone() const;
};

struct Node {
    NodeKind kind = NodeKind::Run;
    Ooxml::XmlElementPtr props;            // the kind's m:*Pr element; null = every default
    QList<Ooxml::XmlAttribute> attributes; // on the structure element itself
    QList<Argument> args;                  // schema order (argumentNames)
    QList<int> rowSizes;                   // Matrix: cells per m:mr
    QList<KeptChild> extras;
    // Run: m:rPr is `props`; the Word (w:rPr) or DrawingML (a:rPr) run
    // properties are `textProps`, verbatim.
    Ooxml::XmlElementPtr textProps;
    QList<RunPiece> pieces;
    // Kept: the element as read.
    Ooxml::XmlElementPtr kept;

    [[nodiscard]] bool isRun() const { return kind == NodeKind::Run; }
    // Run: the m:t text, concatenated.
    [[nodiscard]] QString text() const;
    // Run: replaces the text, keeping the first m:t's attributes and every
    // non-text piece (after the text).
    void setText(const QString &text);
    [[nodiscard]] NodePtr clone() const;
};

struct Equation {
    bool display = false;                  // m:oMathPara (display) or a lone m:oMath (inline)
    Ooxml::XmlElementPtr paraProps;        // m:oMathParaPr (m:jc)
    QList<Ooxml::XmlAttribute> attributes; // on m:oMathPara
    QList<Argument> lines;                 // the m:oMath children; an inline equation has one
    QList<KeptChild> extras;               // other m:oMathPara children

    [[nodiscard]] Equation clone() const;
    [[nodiscard]] bool isEmpty() const;
};

// Builders. A structure gets its kind's EMPTY arguments: fixed-arity kinds
// their slots, Delimiter and EqArray `count` elements or rows, Matrix
// `count` rows of `columns` cells.
[[nodiscard]] NodePtr makeRun(const QString &text, Ooxml::XmlElementPtr mathProps = nullptr);
[[nodiscard]] NodePtr makeNode(NodeKind kind, int count = 1, int columns = 1);
[[nodiscard]] Equation makeEquation(bool display);

// OMML vocabulary per kind (empty views for Kept).
[[nodiscard]] QStringView elementName(NodeKind kind);
[[nodiscard]] QStringView propertyName(NodeKind kind);
// Fixed-arity kinds: their argument elements in schema order. Delimiter,
// EqArray and Matrix: {"e"}. Run and Kept: none.
[[nodiscard]] QList<QStringView> argumentNames(NodeKind kind);
[[nodiscard]] bool hasFixedArity(NodeKind kind);
[[nodiscard]] std::optional<NodeKind> kindForElement(QStringView localName);

// The characters of a subtree in reading order (runs' text, structures
// flattened) — a search key and the last-resort accessible text.
[[nodiscard]] QString plainText(const Argument &argument);

// A run holding `text` is what a typed character joins: true for runs
// without kept pieces.
[[nodiscard]] bool isPlainRun(const Node &node);

} // namespace QindaOffice::Math
