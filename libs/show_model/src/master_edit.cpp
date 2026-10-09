// SPDX-License-Identifier: GPL-3.0-or-later
// The Slide Master view's model operations (master_edit.h).

#include "master_edit.h"

#include "pptx_origin.h"

#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace QindaOffice::Show {

namespace {

const QRegularExpression &layoutNumber()
{
    static const QRegularExpression re(QStringLiteral("slideLayout(\\d+)"));
    return re;
}

QString nextLayoutId(const Presentation &deck)
{
    int highest = 0;
    const auto consider = [&highest](const QString &name) {
        const QRegularExpressionMatch match = layoutNumber().match(name);
        if (match.hasMatch()) {
            highest = qMax(highest, match.captured(1).toInt());
        }
    };
    for (const SlideLayout &layout : deck.layouts) {
        consider(layout.id);
    }
    if (deck.origin && deck.origin->package) {
        for (const QString &part : deck.origin->package->partNames()) {
            consider(part);
        }
    }
    return QStringLiteral("slideLayout%1").arg(highest + 1);
}

SlideLayout *findLayout(Presentation &deck, const QString &layoutId)
{
    for (SlideLayout &layout : deck.layouts) {
        if (layout.id == layoutId) {
            return &layout;
        }
    }
    return nullptr;
}

SlideMaster *findMaster(Presentation &deck, const QString &masterId)
{
    for (SlideMaster &master : deck.masters) {
        if (master.id == masterId) {
            return &master;
        }
    }
    return nullptr;
}

bool isFooter(const QString &type)
{
    return type == u"dt" || type == u"ftr" || type == u"sldNum";
}

bool isTitleType(const QString &type)
{
    return type == u"title" || type == u"ctrTitle";
}

// A copy of a master placeholder for a layout: a new shape that writes the
// master's XML (its provenance) with a fresh model id.
Shape copied(const Shape &shape)
{
    Shape copy = shape;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return copy;
}

QList<Shape> masterPlaceholders(const Presentation &deck, const QString &masterId, bool (*wanted)(const QString &))
{
    QList<Shape> out;
    if (const SlideMaster *master = deck.findMaster(masterId)) {
        for (const Shape &shape : master->placeholders) {
            if (wanted(shape.placeholder.type)) {
                out.append(copied(shape));
            }
        }
    }
    return out;
}

QList<TextParagraph> *styleList(SlideMaster &master, StyleList list)
{
    switch (list) {
    case StyleList::Title:
        return &master.titleStyle;
    case StyleList::Body:
        return &master.bodyStyle;
    case StyleList::Other:
        return &master.otherStyle;
    }
    return nullptr;
}

// Placeholders and decorations in z order (`order`; the rest after it,
// placeholders first).
QList<Shape> inOrder(const QStringList &order, const QList<Shape> &placeholders, const QList<Shape> &shapes)
{
    QList<Shape> all = placeholders + shapes;
    std::stable_sort(all.begin(), all.end(), [&order](const Shape &a, const Shape &b) {
        const qsizetype ra = order.contains(a.id) ? order.indexOf(a.id) : order.size();
        const qsizetype rb = order.contains(b.id) ? order.indexOf(b.id) : order.size();
        return ra < rb;
    });
    return all;
}

} // namespace

QList<const SlideLayout *> layoutsOfMaster(const Presentation &deck, const QString &masterId)
{
    QList<const SlideLayout *> out;
    for (const SlideLayout &layout : deck.layouts) {
        if (layout.master == masterId) {
            out.append(&layout);
        }
    }
    return out;
}

int slidesUsingLayout(const Presentation &deck, const QString &layoutId)
{
    int count = 0;
    for (const Slide &slide : deck.slides) {
        count += slide.layoutId == layoutId ? 1 : 0;
    }
    return count;
}

QString insertLayout(Presentation &deck, const QString &masterId, const QString &afterLayoutId)
{
    if (deck.findMaster(masterId) == nullptr) {
        return {};
    }
    SlideLayout layout;
    layout.id = nextLayoutId(deck);
    layout.name = QStringLiteral("Custom Layout");
    layout.type = QStringLiteral("cust");
    layout.master = masterId;
    layout.preserve = true;
    layout.placeholders = masterPlaceholders(deck, masterId, [](const QString &type) { return isTitleType(type) || isFooter(type); });
    qsizetype at = deck.layouts.size();
    for (qsizetype i = 0; i < deck.layouts.size(); ++i) {
        if (deck.layouts.at(i).master == masterId) {
            at = i + 1; // after the master's last, unless a layout is named
        }
        if (!afterLayoutId.isEmpty() && deck.layouts.at(i).id == afterLayoutId) {
            at = i + 1;
            break;
        }
    }
    deck.layouts.insert(at, layout);
    return layout.id;
}

QString duplicateLayout(Presentation &deck, const QString &layoutId)
{
    for (qsizetype i = 0; i < deck.layouts.size(); ++i) {
        if (deck.layouts.at(i).id != layoutId) {
            continue;
        }
        SlideLayout copy = deck.layouts.at(i);
        copy.id = nextLayoutId(deck);
        copy.name = QStringLiteral("1_") + copy.name;
        copy.origin = {}; // a new part; its shapes still write their source XML
        for (QList<Shape> *list : {&copy.placeholders, &copy.shapes}) {
            for (Shape &shape : *list) {
                const QString fresh = QUuid::createUuid().toString(QUuid::WithoutBraces);
                copy.order.replaceInStrings(QRegularExpression(QStringLiteral("^%1$").arg(QRegularExpression::escape(shape.id))), fresh);
                shape.id = fresh;
            }
        }
        deck.layouts.insert(i + 1, copy);
        return copy.id;
    }
    return {};
}

bool deleteLayout(Presentation &deck, const QString &layoutId)
{
    if (slidesUsingLayout(deck, layoutId) > 0) {
        return false;
    }
    const qsizetype before = deck.layouts.size();
    deck.layouts.removeIf([&layoutId](const SlideLayout &layout) { return layout.id == layoutId; });
    return deck.layouts.size() != before;
}

bool renameLayout(Presentation &deck, const QString &layoutId, const QString &name)
{
    SlideLayout *layout = findLayout(deck, layoutId);
    if (layout == nullptr || name.trimmed().isEmpty() || layout->name == name.trimmed()) {
        return false;
    }
    layout->name = name.trimmed();
    return true;
}

bool renameMaster(Presentation &deck, const QString &masterId, const QString &name)
{
    SlideMaster *master = findMaster(deck, masterId);
    if (master == nullptr || name.trimmed().isEmpty() || master->name == name.trimmed()) {
        return false;
    }
    master->name = name.trimmed();
    return true;
}

bool setLayoutPreserved(Presentation &deck, const QString &layoutId, bool preserve)
{
    SlideLayout *layout = findLayout(deck, layoutId);
    if (layout == nullptr || layout->preserve == preserve) {
        return false;
    }
    layout->preserve = preserve;
    return true;
}

bool layoutHasTitle(const SlideLayout &layout)
{
    return std::any_of(layout.placeholders.cbegin(), layout.placeholders.cend(),
                       [](const Shape &shape) { return isTitleType(shape.placeholder.type); });
}

bool layoutHasFooters(const SlideLayout &layout)
{
    return std::any_of(layout.placeholders.cbegin(), layout.placeholders.cend(),
                       [](const Shape &shape) { return isFooter(shape.placeholder.type); });
}

bool setLayoutTitle(Presentation &deck, const QString &layoutId, bool on)
{
    SlideLayout *layout = findLayout(deck, layoutId);
    if (layout == nullptr || layoutHasTitle(*layout) == on) {
        return false;
    }
    if (on) {
        layout->placeholders = masterPlaceholders(deck, layout->master, [](const QString &type) { return isTitleType(type); })
            + layout->placeholders;
    } else {
        layout->placeholders.removeIf([](const Shape &shape) { return isTitleType(shape.placeholder.type); });
    }
    return true;
}

bool setLayoutFooters(Presentation &deck, const QString &layoutId, bool on)
{
    SlideLayout *layout = findLayout(deck, layoutId);
    if (layout == nullptr || layoutHasFooters(*layout) == on) {
        return false;
    }
    if (on) {
        layout->placeholders += masterPlaceholders(deck, layout->master, [](const QString &type) { return isFooter(type); });
    } else {
        layout->placeholders.removeIf([](const Shape &shape) { return isFooter(shape.placeholder.type); });
    }
    return true;
}

QString insertPlaceholder(Presentation &deck, const QString &layoutId, const QString &type, const QRectF &rect)
{
    SlideLayout *layout = findLayout(deck, layoutId);
    if (layout == nullptr) {
        return {};
    }
    // A fresh index above every one the layout and its master use.
    qint64 index = 12;
    for (const Shape &shape : layout->placeholders) {
        index = qMax(index, shape.placeholder.index);
    }
    if (const SlideMaster *master = deck.findMaster(layout->master)) {
        for (const Shape &shape : master->placeholders) {
            index = qMax(index, shape.placeholder.index);
        }
    }
    Shape shape;
    shape.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    shape.kind = ShapeKind::TextBox;
    shape.rect = rect;
    shape.strokeWidth = 0;
    shape.placeholder = {type, index + 1, QString(), QString()};
    shape.name = type == u"pic" ? QStringLiteral("Picture Placeholder") : type == u"body" ? QStringLiteral("Text Placeholder")
                                                                                          : QStringLiteral("Content Placeholder");
    TextRun run;
    run.text = type == u"pic" ? QStringLiteral("Picture") : QStringLiteral("Click to add text");
    if (const SlideMaster *master = deck.findMaster(layout->master); master != nullptr && !master->bodyStyle.isEmpty()) {
        const TextParagraph &level = master->bodyStyle.first();
        if (!level.runs.isEmpty()) {
            run = TextRun{run.text, level.runs.first().bold, level.runs.first().italic, level.runs.first().color,
                          level.runs.first().pointSize, level.runs.first().fontFamily};
        }
    }
    TextParagraph paragraph;
    paragraph.runs = {run};
    shape.paragraphs = {paragraph};
    layout->placeholders.append(shape);
    return shape.id;
}

const TextParagraph *styleLevel(const SlideMaster &master, StyleList list, int level)
{
    const QList<TextParagraph> &levels = list == StyleList::Title ? master.titleStyle
        : list == StyleList::Body                                ? master.bodyStyle
                                                                 : master.otherStyle;
    return level >= 0 && level < levels.size() ? &levels.at(level) : nullptr;
}

bool setStyleLevel(Presentation &deck, const QString &masterId, StyleList list, int level, const TextParagraph &look)
{
    SlideMaster *master = findMaster(deck, masterId);
    QList<TextParagraph> *levels = master != nullptr ? styleList(*master, list) : nullptr;
    if (levels == nullptr || level < 0 || level >= levels->size()) {
        return false;
    }
    TextParagraph next = look;
    next.listLevel = level;
    if (next.runs.isEmpty()) {
        next.runs = levels->at(level).runs;
    }
    for (TextRun &run : next.runs) {
        run.text.clear();
    }
    if ((*levels)[level] == next) {
        return false;
    }
    (*levels)[level] = next;
    return true;
}

Slide templateSlide(const Presentation &deck, const QString &masterId, const QString &layoutId)
{
    Slide slide;
    const SlideMaster *master = deck.findMaster(masterId);
    const SlideLayout *layout = layoutId.isEmpty() ? nullptr : deck.findLayout(layoutId);
    slide.id = layout != nullptr ? layout->id : masterId;
    slide.master = masterId;
    slide.layoutId = layoutId;
    if (layout != nullptr) {
        slide.title = layout->name;
        slide.shapes = inOrder(layout->order, layout->placeholders, layout->shapes);
        slide.background = layout->background;
        slide.showMasterShapes = layout->showMasterShapes;
    } else if (master != nullptr) {
        slide.title = master->name;
        slide.shapes = inOrder(master->order, master->placeholders, master->shapes);
        slide.background = Background{true, master->background, master->backgroundStyle};
    }
    return slide;
}

bool applyTemplateSlide(Presentation &deck, const QString &masterId, const QString &layoutId, const Slide &edited)
{
    QList<Shape> placeholders;
    QList<Shape> shapes;
    for (const Shape &shape : edited.shapes) {
        (shape.placeholder.isPlaceholder() ? placeholders : shapes).append(shape);
    }
    QStringList order;
    for (const Shape &shape : edited.shapes) {
        order.append(shape.id);
    }
    if (!layoutId.isEmpty()) {
        SlideLayout *layout = findLayout(deck, layoutId);
        if (layout == nullptr
            || (layout->placeholders == placeholders && layout->shapes == shapes
                && inOrder(layout->order, layout->placeholders, layout->shapes) == edited.shapes)) {
            return false;
        }
        layout->placeholders = placeholders;
        layout->shapes = shapes;
        layout->order = order;
        return true;
    }
    SlideMaster *master = findMaster(deck, masterId);
    if (master == nullptr
        || (master->placeholders == placeholders && master->shapes == shapes
            && inOrder(master->order, master->placeholders, master->shapes) == edited.shapes)) {
        return false;
    }
    master->placeholders = placeholders;
    master->shapes = shapes;
    master->order = order;
    return true;
}

} // namespace QindaOffice::Show
