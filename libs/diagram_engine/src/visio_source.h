// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the Visio interchange (M65, D-073): what the reader keeps of
// the package a drawing came from, so the writer can put back what the
// user did not change (D-068 rules 2 and 3).
//
// Ownership/lifetime: built once by the reader and then immutable, shared
// (std::shared_ptr<const Source>) by the DrawingDocument and every save;
// its XML trees are never modified — writers clone what they change.
//
// AGENT-CONTRACT (visio_read*.cpp ⇄ visio_write*.cpp):
//  * Item keys are "<Visio page ID>/<Visio shape ID>"; the model item's
//    extra["vsdx"]["key"] names them (visio_io.h).
//  * `shape` / `connector` are the model item EXACTLY as the reader made
//    it: the writer compares the current item with it to decide verbatim
//    (equal), merged (source element with the mapped cells rewritten) or
//    fresh (no source).
//  * `element` is the source <Shape>; for a group's members it is the
//    nested element, whose PinX/PinY are in the group's coordinates.

#include "drawing.h"
#include "ooxml_package.h"
#include "ooxml_properties.h"
#include "ooxml_xml.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace QindaOffice::Diagram::Visio {

struct SourceItem {
    QString key;                  // itemKey(page, visioId)
    QString page;                 // Visio page ID
    QString part;                 // the page part (relationship ids resolve there)
    Ooxml::XmlElementPtr element; // the <Shape>
    int visioId = 0;
    QString group;                // key of the top-level group it came from
    bool groupSheet = false;      // made from a group's own sheet (text, geometry)
    bool masterChild = false;     // inherits through a MasterShape of its group's master
    int master = -1;              // the master it inherits from (-1: none) …
    int masterShape = -1;         // … and that master's shape (-1: the first)
    std::optional<Shape> shape;
    std::optional<Connector> connector;
};

struct SourceGroup {
    QString page;
    QString part;
    Ooxml::XmlElementPtr element; // the top-level group <Shape>, children and all
    int visioId = 0;
    QStringList members;          // item keys at every level
    QList<int> ids;               // every Visio ID inside, the group's own first
};

struct SourcePage {
    QString id;
    QString part;
    Ooxml::XmlElementPtr entry;   // the <Page> of pages.xml, PageSheet included
    bool background = false;
    double scale = 1;             // paper inches per drawing inch
    QStringList order;            // top-level keys (items, groups, unmodelled) as in the file
    QHash<QString, Ooxml::XmlElementPtr> unmodelled; // guides, ink, … by key
    QList<Ooxml::XmlElementPtr> connects;
    int maxId = 0;
    QString name;                 // as read, to tell a rename
    QSizeF size;                  // page units as read, to tell a resize
};

struct Source {
    QString token;                // hash of the package bytes
    std::shared_ptr<const Ooxml::Package> package;
    QString documentPart;
    Ooxml::XmlElementPtr document; // <VisioDocument>
    QString mastersPart;
    QHash<int, QString> masterParts; // master ID → its part
    QHash<int, Ooxml::XmlElementPtr> masterRoots; // master ID → <MasterContents>
    QHash<QString, SourceItem> items;
    QHash<QString, SourceGroup> groups;
    QHash<QString, SourcePage> pages;
    Ooxml::CoreProperties properties;
    int lineStyle = -1;           // DocumentSettings defaults
    int fillStyle = -1;
    int textStyle = -1;
    int connectorStyle = -1;      // the style named "Connector", if any
};

[[nodiscard]] inline QString itemKey(const QString &page, int id)
{
    return page + QLatin1Char('/') + QString::number(id);
}

// The <Shape> of master `masterId` an instance inherits from: `shapeId`, or
// the master's first top-level shape for -1. Null when there is none.
[[nodiscard]] const Ooxml::XmlElement *masterShape(const Source &source, int masterId, int shapeId);

} // namespace QindaOffice::Diagram::Visio
