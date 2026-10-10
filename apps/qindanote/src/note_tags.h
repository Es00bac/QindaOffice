// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// OneNote's tags (Home ▸ Tags) on note pages (M84, D-093): To Do (a
// checkbox that can be done), Important, Question, Remember for later,
// Definition, Highlight, Contact, Address, Phone number, Idea, Critical,
// Project A and Project B, plus the user's custom tags.
//
// AGENT-CONTRACT (the page format, docs/qindanote.md#tags): a block's
// `tags` array holds one entry per tag: {"p": paragraph id (a rich text
// paragraph; absent for a whole checklist, markdown or image block),
// "tag": id, "done": bool (To Do), and for a custom tag its look —
// "name", "icon", "color" — so a notebook shows its tags on any machine}.
// Built-in ids are stable: todo, important, question, remember,
// definition, highlight, contact, address, phone, idea, critical,
// projectA, projectB; custom ids are "custom:<uuid>".
//
// Painting: icons sit in the margin LEFT of the paragraph or block (as in
// OneNote), drawn from QindaTK's icon paths in the tag's paper colour;
// Remember for later and Definition highlight the paragraph instead, as
// OneNote's do. The screen and the printer place them through place().

#include "note_blocks.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>

class QPainter;

namespace QindaOffice::Apps::Notes::Tags {

inline constexpr qreal kIconSize = 14;
inline constexpr qreal kIconGap = 3;
inline constexpr int kMaxCustomTags = 64;

struct TagDef {
    QString id;
    QString name;
    QString icon;           // QindaTK icon name; empty for a highlight tag
    QColor color;           // paper colour (D-004)
    bool todo = false;      // a checkbox that can be done
    bool highlight = false; // a band behind the text instead of an icon
    [[nodiscard]] bool isValid() const { return !id.isEmpty(); }
};

[[nodiscard]] const QList<TagDef> &builtIn();
// A built-in or one of the user's custom tags; invalid when unknown.
[[nodiscard]] TagDef byId(const QString &id);
// The tag an entry names (a custom one keeps the look it was saved with).
[[nodiscard]] TagDef fromEntry(const QJsonObject &entry);
// A new entry for `def` on `paragraph` ("" for the whole block).
[[nodiscard]] QJsonObject entryFor(const TagDef &def, const QString &paragraph);

// The user's custom tags ($XDG_CONFIG_HOME/<app>/tags.json, at most 64).
[[nodiscard]] QList<TagDef> customTags();
bool addCustomTag(const QString &name, const QString &icon, const QColor &color, TagDef *created = nullptr);
bool removeCustomTag(const QString &id);
// The icons a custom tag may use (all in QindaTK's set).
[[nodiscard]] QStringList customIcons();

// `def` added to every target (paragraph ids; "" for the whole block) when
// one of them lacks it, else removed from all of them (a toggle).
[[nodiscard]] QJsonArray toggled(const QJsonArray &tags, const QStringList &targets, const TagDef &def,
                                 bool *added = nullptr);
// Every tag of the targets removed.
[[nodiscard]] QJsonArray withoutTargets(const QJsonArray &tags, const QStringList &targets);
// The To Do entry `index` marked done or not.
[[nodiscard]] QJsonArray withDone(const QJsonArray &tags, int index, bool done);
[[nodiscard]] bool hasTag(const QJsonArray &tags, const QString &target, const QString &tagId);

// Where block `block`'s tags sit on the page.
struct Placed {
    int entry = -1;        // index in the block's tags
    TagDef def;
    bool done = false;
    QString paragraph;
    QRectF rect;           // the icon, or a band's line
    bool band = false;
};
[[nodiscard]] QList<Placed> place(const PageBlock &block);
// Bands go UNDER the typed text, icons over it (note_page_render.h order).
void paintBands(QPainter *painter, const QList<PageBlock> &blocks);
void paintIcons(QPainter *painter, const QList<PageBlock> &blocks);
// One icon (the To Do box ticked when `done`), for the page and exports.
void paintIcon(QPainter *painter, const QRectF &rect, const TagDef &def, bool done);

// Tagged items of one page, in reading order (Find Tags, search).
struct Item {
    int block = -1;
    int entry = -1;
    QString paragraph;
    TagDef def;
    bool done = false;
    QString text;
};
[[nodiscard]] QList<Item> items(const QByteArray &blocksJson, qreal pageWidth);

} // namespace QindaOffice::Apps::Notes::Tags
