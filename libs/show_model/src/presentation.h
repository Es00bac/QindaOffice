// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaShow slide model (plan sections 6.3/7.5; extended by M62,
// D-070). Coordinates are points; a new deck is 16:9 (720x405 pt).
// Z-order is the shape list order; builds are per-shape appear-on-click
// steps; transitions name the player effect. The model is plain data —
// the editor and renderer interpret it, and every value is EFFECTIVE
// (show_text.h): the Office reader resolves masters, layouts, placeholders
// and themes before storing, so nothing downstream chases inheritance.
//
// Types live in show_text.h (runs, paragraphs), show_style.h (fills,
// outlines, text bodies), show_table.h and show_chart.h; this header has
// the shapes, slides, masters and the deck.
//
// AGENT-NOTE: provenance. A deck read from .pptx keeps, per shape and per
// slide, the XML it came from (Provenance<…Source>; the source types live
// in pptx_origin.h). It never takes part in equality — two decks are the
// same when their CONTENT is — and the writer emits it verbatim while the
// content still equals what was read (D-068 rule 3).

#include "show_chart.h"
#include "show_style.h"
#include "show_table.h"
#include "show_text.h"

#include <QColor>
#include <QPair>
#include <QRectF>
#include <QSize>
#include <QUuid>
#include <QVariant>

#include <memory>
#include <vector>

namespace QindaOffice::Show {

enum class ShapeKind {
    TextBox,
    Rectangle,
    Ellipse,
    Line,
    Arrow,
    Image,
    // ---- M62 ----
    AutoShape, // any other preset (Shape::geometry) or custom geometry
    Table,     // Shape::table, in a graphic frame
    Chart,     // Shape::chart drawn from cached values; the frame is preserved (edits: D-076)
    Group,     // Shape::children in the Shape::childRect coordinate space
    Preserved, // Office content QindaShow cannot edit (SmartArt, media, OLE, ink, …)
};

// How a shape arrives when its build step is reached. Appear is the M8
// behaviour (the shape is simply there); the rest animate over
// Shape::animationMs. The renderer applies them, so the player, the
// presenter view and any future export all animate identically.
//
// M67 (D-076) adds PowerPoint's Wipe (from the bottom) and Split
// (vertical out) entrances and two EMPHASIS effects: Spin (one turn) and
// Grow/Shrink (to 150 %, kept until the slide ends). An emphasis shape is
// on the slide from the start; its build step is when it animates.
// AGENT-GUARD: values are stored by number (.fodp qq:animation, the
// clipboard JSON) — append, never reorder.
enum class ShapeAnimation {
    Appear,
    Fade,
    FlyInLeft,
    FlyInRight,
    FlyInTop,
    FlyInBottom,
    Zoom,
    // ---- M67 ----
    Wipe,
    Split,
    Spin,
    GrowShrink,
};
[[nodiscard]] inline bool isEmphasis(ShapeAnimation animation)
{
    return animation == ShapeAnimation::Spin || animation == ShapeAnimation::GrowShrink;
}
inline constexpr ShapeAnimation kLastAnimation = ShapeAnimation::GrowShrink;

// An Office placeholder (p:ph). Empty type = not a placeholder.
struct Placeholder {
    QString type;      // "title", "body", "ctrTitle", "subTitle", "obj", "dt", "ftr", "sldNum", "pic", …
    qint64 index = -1; // p:ph/@idx (-1 = absent)
    QString size;      // p:ph/@sz
    QString orient;    // p:ph/@orient
    bool operator==(const Placeholder &) const = default;
    [[nodiscard]] bool isPlaceholder() const { return !type.isEmpty(); }
};

// Picture details beyond the bytes (Shape::imagePng holds the bytes).
struct ImageInfo {
    QString mime;          // "image/png", "image/jpeg", "image/x-emf", …
    qreal cropLeft = 0;    // fractions of the image cut away (a:srcRect)
    qreal cropTop = 0;
    qreal cropRight = 0;
    qreal cropBottom = 0;
    QSize pixels;          // natural size of the image
    bool operator==(const ImageInfo &) const = default;
    [[nodiscard]] bool isCropped() const { return cropLeft != 0 || cropTop != 0 || cropRight != 0 || cropBottom != 0; }
};

// One end of a connector (a:stCxn / a:endCxn): the shape (its Office id)
// and the connection site on it.
struct Connection {
    quint32 shapeId = 0; // 0 = not connected
    int site = 0;
    bool operator==(const Connection &) const = default;
};

// Office XML kept verbatim because QindaShow cannot model it: the element
// as a self-contained fragment (Ooxml::serializeFragment) and the part it
// came from, which its relationship ids resolve against.
//
// AGENT-NOTE: equality is about CONTENT: the part and the relationship ids
// inside the fragment (r:id, r:embed, …) name where the bytes live in one
// particular package, and a save renumbers them. They are compared out.
struct PreservedXml {
    QString fragment;
    QString part;
    QString label; // what the placeholder box says ("SmartArt", "Video", …)
    bool operator==(const PreservedXml &other) const;
    [[nodiscard]] bool isEmpty() const { return fragment.isEmpty(); }
};

// Where a model object came from (pptx_origin.h holds the source types).
// Provenance never makes two objects differ: equality is about content.
template <typename Source>
struct Provenance {
    std::shared_ptr<const Source> source;
    bool operator==(const Provenance &) const { return true; }
    [[nodiscard]] explicit operator bool() const { return bool(source); }
    [[nodiscard]] const Source *get() const { return source.get(); }
    const Source *operator->() const { return source.get(); }
};

struct ShapeSource;  // pptx_origin.h
struct SlideSource;
struct MasterSource;
struct LayoutSource;
struct DeckSource;

struct Shape {
    QString id;
    ShapeKind kind = ShapeKind::TextBox;
    QRectF rect;            // pt, slide coordinates (a group's child space for children)
    qreal rotation = 0;     // degrees, clockwise
    QColor fill = Qt::transparent;
    QColor stroke = Qt::black;
    qreal strokeWidth = 1.0;
    QList<TextParagraph> paragraphs;
    QByteArray imagePng;    // ShapeKind::Image payload: the file's own bytes, any format
    int buildStep = 0;      // 0 = visible with the slide; 1+ appear on click
    ShapeAnimation animation = ShapeAnimation::Appear;
    int animationMs = 350;
    // Shapes sharing a non-empty group id move, resize and select as one.
    QString groupId;
    QVariantMap extra;      // qq: extension namespace payload

    // ---- M62 ----
    QString name;           // the Office shape name ("Title 1")
    quint32 nvId = 0;       // the Office shape id (0 = assign on write)
    QString description;    // alternative text
    bool hidden = false;
    bool flipH = false;     // for lines: the start is on the right
    bool flipV = false;     // for lines: the start is at the bottom
    QString geometry;       // preset name (a:prstGeom/@prst); empty = the kind's own
    QList<QPair<QString, qint64>> adjust; // preset adjust values (a:avLst gd "val N")
    QString customGeometry; // a:custGeom as a fragment (AutoShape)
    // TextBox and Image kinds draw their outline only when framed (an
    // Office text box or picture can carry one; QindaShow's never did).
    bool framed = false;
    FillStyle fillStyle;
    LineStyle line;
    TextBodyStyle textBody;
    Placeholder placeholder;
    ImageInfo image;
    Table table;
    ChartData chart;
    // Group members; the render-only fallback of Preserved content.
    // AGENT-GUARD: std::vector, not QList — Qt's operator== detection on
    // QList<Shape> recurses into Shape itself and silently deletes
    // Shape::operator== (and with it every model comparison).
    std::vector<Shape> children;
    QRectF childRect;       // the group's child coordinate space (a:chOff/a:chExt)
    bool connector = false; // a p:cxnSp (Line/Arrow/AutoShape kind)
    Connection start;
    Connection end;
    QString hyperlink;      // a click on the shape; same forms as TextRun::hyperlink
    PreservedXml preserved; // Preserved and Chart kinds: the frame as written
    Provenance<ShapeSource> origin;

    [[nodiscard]] QString displayText() const;
    // True when the shape's text is edited in place (not pictures, lines,
    // groups, tables, charts or preserved content; a table's CELLS are,
    // through SlideCanvasItem::beginCellEdit, M67).
    [[nodiscard]] bool holdsText() const;
    bool operator==(const Shape &) const = default;
};

// A slide's own background (p:bg) — when unset the layout's, then the
// master's applies.
struct Background {
    bool set = false;
    QColor color;        // Plain fills (and a representative colour otherwise)
    FillStyle style;
    bool operator==(const Background &) const = default;
};

// A slide master: the background and default type every slide using it
// inherits. Keeping it in the model (rather than copying values into each
// slide) is what makes "restyle the whole deck" one edit.
struct SlideMaster {
    QString id;
    QString name;
    QColor background = Qt::white;
    QColor titleColor = Qt::black;
    QColor bodyColor = Qt::black;
    QString fontFamily; // empty = the run's own family
    // ---- M62 ----
    // AGENT-NOTE: a QindaShow master fills in the colour/family of runs
    // that never chose their own (withMasterDefaults). An Office master's
    // type is already resolved into every run by the reader, so it must
    // not repaint them: typeDefaults is false for masters read from .pptx.
    bool typeDefaults = true;
    FillStyle backgroundStyle; // gradient/picture backgrounds (Plain = `background`)
    QList<Shape> shapes;       // drawn behind every slide (logos, rules; not placeholders)
    Provenance<MasterSource> origin;
    bool operator==(const SlideMaster &) const = default;
};

// An Office slide layout: which master it belongs to, its background
// override and the decorative shapes it adds.
struct SlideLayout {
    QString id;
    QString name;
    QString type;            // ST_SlideLayoutType ("title", "obj", "blank", …)
    QString master;          // SlideMaster id
    Background background;
    QList<Shape> shapes;     // decorative shapes (not placeholders)
    bool showMasterShapes = true;
    Provenance<LayoutSource> origin;
    bool operator==(const SlideLayout &) const = default;
};

struct Slide {
    QString id;
    QString title;
    QString notes;
    QString layout = QStringLiteral("title+content"); // six layouts
    QString transition = QStringLiteral("cut");       // cut/fade/slide/zoom
    QString master;                                   // SlideMaster id; empty = plain white
    QList<Shape> shapes;
    // ---- M62 ----
    bool hidden = false;          // skipped by the show
    Background background;
    QString layoutId;             // SlideLayout id (Office decks)
    bool showMasterShapes = true;
    Provenance<SlideSource> origin;

    [[nodiscard]] Shape *findShape(const QString &id);
    // Every shape sharing `groupId`, in z order. Empty ids match nothing:
    // an ungrouped shape is not "in the group of all ungrouped shapes".
    [[nodiscard]] QList<QString> shapeIdsInGroup(const QString &groupId) const;
    bool operator==(const Slide &) const = default;
};

struct Presentation {
    QSizeF slideSize{720.0, 405.0}; // 16:9 in points
    QString title;
    QList<Slide> slides;
    QList<SlideMaster> masters;
    // ---- M62 ----
    QList<SlideLayout> layouts;
    QSizeF notesSize{540.0, 720.0}; // portrait 7.5 x 10 in
    Provenance<DeckSource> origin;

    [[nodiscard]] Slide &addSlide(const QString &title);
    [[nodiscard]] Slide *findSlide(const QString &id);
    [[nodiscard]] const SlideMaster *findMaster(const QString &id) const;
    [[nodiscard]] const SlideLayout *findLayout(const QString &id) const;
    // The master a slide renders with, or nullptr for plain white.
    [[nodiscard]] const SlideMaster *masterFor(const Slide &slide) const;
    [[nodiscard]] const SlideLayout *layoutFor(const Slide &slide) const;
    bool operator==(const Presentation &) const = default;
};

} // namespace QindaOffice::Show
