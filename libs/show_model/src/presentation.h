// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaShow slide model (plan sections 6.3/7.5). Coordinates are
// points on a 16:9 slide (720x405 pt). Z-order is the shape list order;
// builds are per-shape appear-on-click steps; transitions name the player
// effect. The model is deliberately plain data - the editor and renderer
// interpret it.

#include <QColor>
#include <QRectF>
#include <QUuid>
#include <QVariant>

namespace QindaOffice::Show {

struct TextRun {
    QString text;
    bool bold = false;
    bool italic = false;
    QColor color = Qt::black;
    int pointSize = 18;
    QString fontFamily;
    bool operator==(const TextRun &) const = default;
};

struct TextParagraph {
    QList<TextRun> runs;
    int alignment = 0; // Qt::Alignment
    bool bullet = false;
    int listLevel = 0;
    bool operator==(const TextParagraph &) const = default;
};

enum class ShapeKind { TextBox, Rectangle, Ellipse, Line, Arrow, Image };

// How a shape arrives when its build step is reached. Appear is the M8
// behaviour (the shape is simply there); the rest animate over
// Shape::animationMs. The renderer applies them, so the player, the
// presenter view and any future export all animate identically.
enum class ShapeAnimation {
    Appear,
    Fade,
    FlyInLeft,
    FlyInRight,
    FlyInTop,
    FlyInBottom,
    Zoom,
};

struct Shape {
    QString id;
    ShapeKind kind = ShapeKind::TextBox;
    QRectF rect;            // pt, slide coordinates
    qreal rotation = 0;     // degrees, clockwise
    QColor fill = Qt::transparent;
    QColor stroke = Qt::black;
    qreal strokeWidth = 1.0;
    QList<TextParagraph> paragraphs;
    QByteArray imagePng;    // ShapeKind::Image payload
    int buildStep = 0;      // 0 = visible with the slide; 1+ appear on click
    ShapeAnimation animation = ShapeAnimation::Appear;
    int animationMs = 350;
    // Shapes sharing a non-empty group id move, resize and select as one.
    QString groupId;
    QVariantMap extra;      // qq: extension namespace payload

    [[nodiscard]] QString displayText() const;
    bool operator==(const Shape &) const = default;
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
    bool operator==(const SlideMaster &) const = default;
};

struct Slide {
    QString id;
    QString title;
    QString notes;
    QString layout = QStringLiteral("title+content"); // six layouts
    QString transition = QStringLiteral("cut");       // cut/fade/slide/zoom
    QString master;                                   // SlideMaster id; empty = plain white
    QList<Shape> shapes;

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

    [[nodiscard]] Slide &addSlide(const QString &title);
    [[nodiscard]] Slide *findSlide(const QString &id);
    [[nodiscard]] const SlideMaster *findMaster(const QString &id) const;
    // The master a slide renders with, or nullptr for plain white.
    [[nodiscard]] const SlideMaster *masterFor(const Slide &slide) const;
    bool operator==(const Presentation &) const = default;
};

} // namespace QindaOffice::Show
