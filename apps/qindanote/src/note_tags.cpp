// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_tags.h"

#include "note_rich_render.h"

#include "json_settings.h"
#include "paths.h"

#include <qindatk/icon.h>

#include <QCoreApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QUuid>

#include <algorithm>

namespace QindaOffice::Apps::Notes::Tags {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaOffice::Apps::Notes::Tags", text);
}

Core::JsonSettings customStore()
{
    Core::JsonSettings store(Core::configDirFor(Core::appLeaf()) + QStringLiteral("/tags.json"));
    static_cast<void>(store.load());
    return store;
}

TagDef customFrom(const QVariantMap &map)
{
    TagDef def;
    def.id = map.value(QStringLiteral("id")).toString();
    def.name = map.value(QStringLiteral("name")).toString();
    def.icon = map.value(QStringLiteral("icon")).toString();
    def.color = QColor(map.value(QStringLiteral("color")).toString());
    if (!def.color.isValid()) {
        def.color = QColor(0x1a, 0x5f, 0xb4);
    }
    if (def.icon.isEmpty()) {
        def.icon = QStringLiteral("tag");
    }
    return def;
}

QString blockLabel(const PageBlock &block)
{
    switch (block.kind) {
    case PageBlock::Kind::Image:
        return tr("Picture");
    case PageBlock::Kind::Markdown:
        for (const QString &line : block.text.split(QLatin1Char('\n'))) {
            QString text = line.trimmed();
            while (text.startsWith(QLatin1Char('#')) || text.startsWith(QLatin1Char('>'))) {
                text = text.mid(1).trimmed();
            }
            if (!text.isEmpty()) {
                return text;
            }
        }
        return {};
    default:
        return block.text.section(QLatin1Char('\n'), 0, 0);
    }
}

} // namespace

const QList<TagDef> &builtIn()
{
    // OneNote's default tags, in its order, with its shortcuts (Ctrl+1 …
    // Ctrl+9 for the first nine; note_host_tags.cpp). Colours are paper
    // colours (D-004).
    static const QList<TagDef> tags{
        {QStringLiteral("todo"), tr("To Do"), QStringLiteral("square"), QColor(0x1a, 0x5f, 0xb4), true, false},
        {QStringLiteral("important"), tr("Important"), QStringLiteral("star"), QColor(0xd8, 0x9b, 0x00), false, false},
        {QStringLiteral("question"), tr("Question"), QStringLiteral("circle-help"), QColor(0x81, 0x3d, 0x9c), false, false},
        {QStringLiteral("remember"), tr("Remember for later"), QString(), QColor(0xff, 0xe1, 0x4d), false, true},
        {QStringLiteral("definition"), tr("Definition"), QString(), QColor(0x8f, 0xf0, 0xa4), false, true},
        {QStringLiteral("highlight"), tr("Highlight"), QStringLiteral("highlighter"), QColor(0xc6, 0x95, 0x00), false, false},
        {QStringLiteral("contact"), tr("Contact"), QStringLiteral("contact"), QColor(0x1a, 0x5f, 0xb4), false, false},
        {QStringLiteral("address"), tr("Address"), QStringLiteral("house"), QColor(0x26, 0xa2, 0x69), false, false},
        {QStringLiteral("phone"), tr("Phone number"), QStringLiteral("smartphone"), QColor(0x1a, 0x5f, 0xb4), false, false},
        {QStringLiteral("idea"), tr("Idea"), QStringLiteral("zap"), QColor(0xd8, 0x9b, 0x00), false, false},
        {QStringLiteral("critical"), tr("Critical"), QStringLiteral("circle-alert"), QColor(0xc0, 0x1c, 0x28), false, false},
        {QStringLiteral("projectA"), tr("Project A"), QStringLiteral("flag"), QColor(0xc0, 0x1c, 0x28), false, false},
        {QStringLiteral("projectB"), tr("Project B"), QStringLiteral("flag"), QColor(0xe6, 0x61, 0x00), false, false},
    };
    return tags;
}

QList<TagDef> customTags()
{
    QList<TagDef> out;
    for (const QVariant &value : customStore().value(QStringLiteral("custom")).toList()) {
        const TagDef def = customFrom(value.toMap());
        if (def.id.startsWith(QLatin1String("custom:")) && !def.name.isEmpty()) {
            out.append(def);
        }
    }
    return out.mid(0, kMaxCustomTags);
}

bool addCustomTag(const QString &rawName, const QString &icon, const QColor &color, TagDef *created)
{
    const QString name = rawName.simplified().left(64);
    if (name.isEmpty()) {
        return false;
    }
    Core::JsonSettings store = customStore();
    QVariantList list = store.value(QStringLiteral("custom")).toList();
    if (list.size() >= kMaxCustomTags) {
        return false;
    }
    TagDef def;
    def.id = QStringLiteral("custom:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    def.name = name;
    def.icon = customIcons().contains(icon) ? icon : QStringLiteral("tag");
    def.color = color.isValid() ? color : QColor(0x1a, 0x5f, 0xb4);
    list.append(QVariantMap{{QStringLiteral("id"), def.id},
                            {QStringLiteral("name"), def.name},
                            {QStringLiteral("icon"), def.icon},
                            {QStringLiteral("color"), def.color.name(QColor::HexRgb)}});
    store.setValue(QStringLiteral("custom"), list);
    if (!store.save()) {
        return false;
    }
    if (created != nullptr) {
        *created = def;
    }
    return true;
}

bool removeCustomTag(const QString &id)
{
    Core::JsonSettings store = customStore();
    QVariantList list = store.value(QStringLiteral("custom")).toList();
    const auto gone = std::remove_if(list.begin(), list.end(), [&id](const QVariant &value) {
        return value.toMap().value(QStringLiteral("id")).toString() == id;
    });
    if (gone == list.end()) {
        return false;
    }
    list.erase(gone, list.end());
    store.setValue(QStringLiteral("custom"), list);
    return store.save();
}

QStringList customIcons()
{
    return {QStringLiteral("tag"), QStringLiteral("star"), QStringLiteral("flag"), QStringLiteral("bookmark"),
            QStringLiteral("heart"), QStringLiteral("bell"), QStringLiteral("pin"), QStringLiteral("key-round"),
            QStringLiteral("globe"), QStringLiteral("book-text"), QStringLiteral("music"), QStringLiteral("film"),
            QStringLiteral("user"), QStringLiteral("calendar"), QStringLiteral("clock"), QStringLiteral("check-check"),
            QStringLiteral("alert-triangle"), QStringLiteral("info"), QStringLiteral("lock"), QStringLiteral("mail"),
            QStringLiteral("camera"), QStringLiteral("briefcase"), QStringLiteral("target")};
}

TagDef byId(const QString &id)
{
    for (const TagDef &def : builtIn()) {
        if (def.id == id) {
            return def;
        }
    }
    for (const TagDef &def : customTags()) {
        if (def.id == id) {
            return def;
        }
    }
    return {};
}

TagDef fromEntry(const QJsonObject &entry)
{
    const QString id = entry.value(QStringLiteral("tag")).toString();
    if (!id.startsWith(QLatin1String("custom:"))) {
        return byId(id);
    }
    QVariantMap look = entry.toVariantMap();
    look.insert(QStringLiteral("id"), id);
    return customFrom(look);
}

QJsonObject entryFor(const TagDef &def, const QString &paragraph)
{
    QJsonObject entry{{QStringLiteral("tag"), def.id}};
    if (!paragraph.isEmpty()) {
        entry.insert(QStringLiteral("p"), paragraph);
    }
    if (def.todo) {
        entry.insert(QStringLiteral("done"), false);
    }
    if (def.id.startsWith(QLatin1String("custom:"))) {
        entry.insert(QStringLiteral("name"), def.name);
        entry.insert(QStringLiteral("icon"), def.icon);
        entry.insert(QStringLiteral("color"), def.color.name(QColor::HexRgb));
    }
    return entry;
}

bool hasTag(const QJsonArray &tags, const QString &target, const QString &tagId)
{
    return std::any_of(tags.begin(), tags.end(), [&](const QJsonValue &value) {
        const QJsonObject entry = value.toObject();
        return entry.value(QStringLiteral("p")).toString() == target
            && entry.value(QStringLiteral("tag")).toString() == tagId;
    });
}

QJsonArray toggled(const QJsonArray &tags, const QStringList &targets, const TagDef &def, bool *added)
{
    const bool all = !targets.isEmpty() && std::all_of(targets.begin(), targets.end(), [&](const QString &target) {
        return hasTag(tags, target, def.id);
    });
    QJsonArray out;
    if (all) {
        for (const QJsonValue &value : tags) {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("tag")).toString() == def.id
                && targets.contains(entry.value(QStringLiteral("p")).toString())) {
                continue;
            }
            out.append(value);
        }
    } else {
        out = tags;
        for (const QString &target : targets) {
            if (!hasTag(tags, target, def.id)) {
                out.append(entryFor(def, target));
            }
        }
    }
    if (added != nullptr) {
        *added = !all;
    }
    return out;
}

QJsonArray withoutTargets(const QJsonArray &tags, const QStringList &targets)
{
    QJsonArray out;
    for (const QJsonValue &value : tags) {
        if (!targets.contains(value.toObject().value(QStringLiteral("p")).toString())) {
            out.append(value);
        }
    }
    return out;
}

QJsonArray withDone(const QJsonArray &tags, int index, bool done)
{
    QJsonArray out = tags;
    if (index >= 0 && index < out.size()) {
        QJsonObject entry = out.at(index).toObject();
        entry.insert(QStringLiteral("done"), done);
        out.replace(index, entry);
    }
    return out;
}

QList<Placed> place(const PageBlock &block)
{
    QList<Placed> out;
    if (block.tags.isEmpty()) {
        return out;
    }
    // The margin column left of the block, one icon per tag (OneNote hangs
    // its tag icons there); a paragraph's icons sit on its first line.
    QHash<QString, int> iconsOn; // paragraph → icons placed so far
    const auto iconRect = [&](const QString &paragraph, const QRectF &line) {
        const int n = iconsOn.value(paragraph);
        iconsOn.insert(paragraph, n + 1);
        const qreal x = block.rect.left() - (n + 1) * (kIconSize + kIconGap);
        const qreal y = block.rect.top() + line.top() + (line.height() - kIconSize) / 2;
        return QRectF(x, y, kIconSize, kIconSize);
    };
    const QList<RichRender::LineBox> lines =
        block.rich.isEmpty() ? QList<RichRender::LineBox>{} : RichRender::lines(block.rich, block.rect.width());
    const QRectF firstLine(0, kTextPadding, block.rect.width(), qMin<qreal>(block.rect.height(), 20));
    for (int i = 0; i < block.tags.size(); ++i) {
        const QJsonObject entry = block.tags.at(i).toObject();
        const TagDef def = fromEntry(entry);
        if (!def.isValid()) {
            continue;
        }
        const QString paragraph = entry.value(QStringLiteral("p")).toString();
        const bool done = entry.value(QStringLiteral("done")).toBool();
        QList<QRectF> paragraphLines;
        if (paragraph.isEmpty()) {
            paragraphLines.append(firstLine);
        } else {
            for (const RichRender::LineBox &line : lines) {
                if (line.paragraph == paragraph) {
                    paragraphLines.append(line.rect);
                }
            }
        }
        if (paragraphLines.isEmpty()) {
            continue; // the paragraph is gone (the next write prunes it)
        }
        if (def.highlight) {
            const QList<QRectF> covered = paragraph.isEmpty() ? QList<QRectF>{QRectF(0, 0, block.rect.width(),
                                                                              block.rect.height())}
                                                              : paragraphLines;
            for (const QRectF &line : covered) {
                out.append({i, def, done, paragraph, line.translated(block.rect.topLeft()), true});
            }
            continue;
        }
        out.append({i, def, done, paragraph, iconRect(paragraph, paragraphLines.first()), false});
    }
    return out;
}

void paintIcon(QPainter *painter, const QRectF &rect, const TagDef &def, bool done)
{
    const QString name = def.todo ? (done ? QStringLiteral("square-check") : QStringLiteral("square")) : def.icon;
    const QVector<QPainterPath> *paths = QindaTK::IconRegistry::instance()->paths(name);
    if (paths == nullptr) {
        painter->save();
        painter->setPen(QPen(def.color, 1.4));
        painter->drawEllipse(rect.adjusted(2, 2, -2, -2));
        painter->restore();
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(rect.topLeft());
    painter->scale(rect.width() / 24.0, rect.height() / 24.0);
    QPen pen(def.color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    for (const QPainterPath &path : *paths) {
        painter->drawPath(path);
    }
    painter->restore();
}

void paintBands(QPainter *painter, const QList<PageBlock> &blocks)
{
    painter->save();
    for (const PageBlock &block : blocks) {
        for (const Placed &placed : place(block)) {
            if (placed.band) {
                // The highlighter's own see-through (ink_persistence.cpp).
                QColor color = placed.def.color;
                color.setAlpha(110);
                painter->fillRect(placed.rect, color);
            }
        }
    }
    painter->restore();
}

void paintIcons(QPainter *painter, const QList<PageBlock> &blocks)
{
    for (const PageBlock &block : blocks) {
        for (const Placed &placed : place(block)) {
            if (!placed.band) {
                paintIcon(painter, placed.rect, placed.def, placed.done);
            }
        }
    }
}

QList<Item> items(const QByteArray &blocksJson, qreal pageWidth)
{
    QList<PageBlock> blocks = layoutBlocks(blocksJson, pageWidth);
    std::stable_sort(blocks.begin(), blocks.end(), [](const PageBlock &a, const PageBlock &b) {
        return qAbs(a.rect.top() - b.rect.top()) > 4 ? a.rect.top() < b.rect.top() : a.rect.left() < b.rect.left();
    });
    QList<Item> out;
    for (const PageBlock &block : std::as_const(blocks)) {
        if (block.tags.isEmpty()) {
            continue;
        }
        // Paragraph order within the block: the order its lines are laid out.
        QStringList order;
        if (!block.rich.isEmpty()) {
            for (const RichRender::LineBox &line : RichRender::lines(block.rich, block.rect.width())) {
                if (line.first) {
                    order.append(line.paragraph);
                }
            }
        }
        QList<Item> found;
        for (int i = 0; i < block.tags.size(); ++i) {
            const QJsonObject entry = block.tags.at(i).toObject();
            const TagDef def = fromEntry(entry);
            const QString paragraph = entry.value(QStringLiteral("p")).toString();
            if (!def.isValid() || (!paragraph.isEmpty() && !order.contains(paragraph))) {
                continue;
            }
            const QString text = paragraph.isEmpty() ? blockLabel(block)
                                                     : RichRender::paragraphText(block.rich, paragraph);
            found.append({block.index, i, paragraph, def, entry.value(QStringLiteral("done")).toBool(), text});
        }
        std::stable_sort(found.begin(), found.end(), [&order](const Item &a, const Item &b) {
            return order.indexOf(a.paragraph) < order.indexOf(b.paragraph);
        });
        out.append(found);
    }
    return out;
}

} // namespace QindaOffice::Apps::Notes::Tags
