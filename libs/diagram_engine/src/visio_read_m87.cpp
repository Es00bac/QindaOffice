// SPDX-License-Identifier: GPL-3.0-or-later
// What M87 (D-095) reads from a Visio package (visio_read_p.h): layers,
// line jumps, print setup and Auto Size from the PageSheet; an item's
// layers and hyperlinks; Visio's containers and swimlanes; comments.
//
// AGENT-NOTE (cells, [MS-VSDX] and Visio-written files; D-095 lists the
// sources): Layer rows are indexed (IX from 0) — Name, Color (255 = none,
// 0–23 a palette index, else #rrggbb), Status, Visible, Print, Active, Lock,
// Snap, Glue, NameUniv, ColorTrans; LayerMember is "0;2". Hyperlink rows
// are named (Row_1 …). LineJumpCode 0 none / 1 horizontal / 2 vertical /
// 3–5 by routing or stacking order; LineJumpStyle 0–1 arc, 2 gap, 3 square,
// 4–9 many-sided. OnPage 1 = fit to PagesX × PagesY sheets, else ScaleX is
// the print zoom; PaperKind is Windows' DMPAPER; DrawingResizeType 1 = Auto
// Size on. A container is User.msvStructureType "Container", its kind in
// User.msvShapeCategories (Swimlane, Phase, CFF Container).
// A cell the page sheet does not hold keeps QindaDiagram's default.
#include "visio_read_p.h"

#include "ooxml_namespaces.h"
#include "visio_io.h"

#include <QHash>
#include <QJsonObject>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Visio {

namespace {

inline constexpr char16_t kCommentsRel[] = u"http://schemas.microsoft.com/visio/2010/relationships/comments";

double cellNumber(const Sheet &sheet, QStringView name, double fallback)
{
    const Cell *c = sheet.cell(name);
    return c != nullptr ? c->number().value_or(fallback) : fallback;
}

bool on(const Row &row, QStringView name, bool fallback)
{
    const Cell *c = row.cell(name);
    return c != nullptr ? c->number().value_or(fallback ? 1 : 0) != 0 : fallback;
}

QString text(const Row &row, QStringView name)
{
    const Cell *c = row.cell(name);
    return c != nullptr ? c->value : QString();
}

// Windows' DMPAPER sizes in inches (portrait).
QSizeF paperOfKind(int kind)
{
    switch (kind) {
    case 1: return {8.5, 11};             // Letter
    case 3: return {11, 17};              // Tabloid
    case 4: return {17, 11};              // Ledger
    case 5: return {8.5, 14};             // Legal
    case 8: return {297 / 25.4, 420 / 25.4}; // A3
    case 9: return {210 / 25.4, 297 / 25.4}; // A4
    case 11: return {148 / 25.4, 210 / 25.4}; // A5
    default: return {};
    }
}

QColor layerColor(const DocumentInfo &doc, const Row &row)
{
    const Cell *c = row.cell(u"Color");
    if (c == nullptr || c->value.isEmpty() || c->themed()) {
        return {};
    }
    QColor color;
    if (c->value.startsWith(QLatin1Char('#'))) {
        color = QColor(c->value);
    } else {
        bool ok = false;
        const int index = c->value.toInt(&ok);
        if (!ok || index == 255 || index < 0) {
            return {};
        }
        color = doc.paletteColor(index);
    }
    const Cell *trans = row.cell(u"ColorTrans");
    if (color.isValid() && trans != nullptr) {
        color.setAlphaF(float(std::clamp(1.0 - trans->number().value_or(0), 0.0, 1.0)));
    }
    return color;
}

QString userValue(const Sheet &sheet, QStringView name)
{
    const Section *user = sheet.section(u"User");
    if (user == nullptr) {
        return {};
    }
    for (const auto &[rowName, row] : user->namedRows()) {
        if (rowName == name) {
            const Cell *v = row->cell(u"Value");
            return v != nullptr ? v->value : QString();
        }
    }
    return {};
}

} // namespace

void readPageSettings(const DocumentInfo &doc, const Sheet &pageSheet, Page &page)
{
    if (const Section *layers = pageSheet.section(u"Layer")) {
        for (const Row *row : layers->orderedRows()) {
            Layer l;
            l.name = text(*row, u"Name");
            if (l.name.isEmpty()) {
                l.name = text(*row, u"NameUniv");
            }
            l.visible = on(*row, u"Visible", true);
            l.print = on(*row, u"Print", true);
            l.locked = on(*row, u"Lock", false);
            l.active = on(*row, u"Active", false);
            l.snap = on(*row, u"Snap", true);
            l.glue = on(*row, u"Glue", true);
            l.color = layerColor(doc, *row);
            page.layers.append(l);
        }
    }
    if (const Cell *code = pageSheet.cell(u"LineJumpCode")) {
        const int c = int(code->number().value_or(0));
        page.lineJumps = c != 0;
        page.jumpOn = c == 1 ? JumpOn::Horizontal : c == 2 ? JumpOn::Vertical : JumpOn::LastRouted;
    }
    if (const Cell *style = pageSheet.cell(u"LineJumpStyle")) {
        const int s = int(style->number().value_or(0));
        page.jumpStyle = s == 2 ? JumpStyle::Gap : s >= 3 ? JumpStyle::Square : JumpStyle::Arc;
    }
    if (const Cell *resize = pageSheet.cell(u"DrawingResizeType")) {
        page.autoSize = int(resize->number().value_or(0)) == 1;
    }
    // Print setup: only what the page sheet states explicitly.
    const Cell *onPage = pageSheet.cell(u"OnPage");
    const Cell *scale = pageSheet.cell(u"ScaleX");
    const Cell *kind = pageSheet.cell(u"PaperKind");
    if (onPage == nullptr && scale == nullptr && kind == nullptr) {
        return;
    }
    PrintSetup p;
    const QSizeF inches = paperOfKind(kind != nullptr ? int(kind->number().value_or(0)) : 0);
    if (!inches.isEmpty()) {
        const int orientation = int(cellNumber(pageSheet, u"PrintPageOrientation", 0));
        const bool landscape = orientation == 2 || (orientation == 0 && page.size.width() > page.size.height());
        QSizeF paper(inches.width() * kPx, inches.height() * kPx);
        if (landscape != (paper.width() > paper.height())) {
            paper.transpose();
        }
        p.paper = paper;
    }
    if (onPage != nullptr && onPage->number().value_or(0) != 0) {
        p.sheetsAcross = std::clamp(int(cellNumber(pageSheet, u"PagesX", 1)), 1, 50);
        p.sheetsDown = std::clamp(int(cellNumber(pageSheet, u"PagesY", 1)), 1, 50);
    } else if (scale != nullptr && scale->number().value_or(0) > 0) {
        p.zoom = std::clamp(scale->number().value_or(1), 0.01, 10.0);
    }
    p.center = cellNumber(pageSheet, u"CenterX", 0) != 0 || cellNumber(pageSheet, u"CenterY", 0) != 0;
    page.print = p;
}

void readItemExtras(const Sheet &sheet, QList<int> &layers, QList<Hyperlink> &links)
{
    if (const Cell *member = sheet.cell(u"LayerMember")) {
        for (const QString &part : member->value.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            bool ok = false;
            const int index = part.trimmed().toInt(&ok);
            if (ok && index >= 0 && !layers.contains(index)) {
                layers.append(index);
            }
        }
    }
    if (const Section *section = sheet.section(u"Hyperlink")) {
        for (const auto &[name, row] : section->namedRows()) {
            Q_UNUSED(name)
            Hyperlink link{text(*row, u"Description"), text(*row, u"Address"), text(*row, u"SubAddress")};
            if (!link.address.isEmpty() || !link.subAddress.isEmpty()) {
                links.append(link);
            }
        }
    }
}

void readContainer(const Sheet &sheet, double pxPerInch, Shape &shape)
{
    if (userValue(sheet, u"msvStructureType").compare(QLatin1String("Container"), Qt::CaseInsensitive) != 0) {
        return;
    }
    const QString categories = userValue(sheet, u"msvShapeCategories");
    const QStringList kinds = categories.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    const auto has = [&](const char *kind) {
        return std::any_of(kinds.cbegin(), kinds.cend(),
                           [&](const QString &k) { return k.trimmed().compare(QLatin1String(kind), Qt::CaseInsensitive) == 0; });
    };
    shape.structure = has("Swimlane") ? QStringLiteral("lane") : has("Phase") ? QStringLiteral("phase")
                                                                               : QStringLiteral("container");
    bool ok = false;
    const int resize = userValue(sheet, u"msvSDContainerResize").toInt(&ok);
    if (ok) {
        shape.containerResize = resize == 0 ? ContainerResize::None : resize == 2 ? ContainerResize::Fit
                                                                                  : ContainerResize::Expand;
    }
    const double margin = userValue(sheet, u"msvSDContainerMargin").toDouble(&ok);
    if (ok && std::isfinite(margin) && margin >= 0) {
        shape.containerMargin = std::min(margin * pxPerInch, 480.0);
    }
}

int readComments(ReadContext &ctx, const QString &documentPart)
{
    const auto rel = ctx.package->firstOfType(documentPart, QString::fromUtf16(kCommentsRel));
    if (!rel) {
        return 0;
    }
    const QString part = ctx.package->targetPart(documentPart, *rel);
    const XmlElementPtr root = ctx.package->xml(part);
    if (!root) {
        return 0;
    }
    ctx.source->commentsPart = part;
    ctx.source->comments = root;
    QHash<QString, QPair<QString, QString>> authors; // ID → (name, initials)
    if (const XmlElement *list = child(*root, u"AuthorList")) {
        for (const XmlElement *a : children(*list, u"AuthorEntry")) {
            authors.insert(a->attribute({}, u"ID"), {a->attribute({}, u"Name"), a->attribute({}, u"Initials")});
        }
    }
    QHash<QString, int> pageIndex; // Visio page ID → model page
    for (int i = 0; i < ctx.drawing->pages.size(); ++i) {
        const QJsonObject vsdx = ctx.drawing->pages.at(i).extra.value(QLatin1String("vsdx")).toObject();
        pageIndex.insert(vsdx.value(QLatin1String("page")).toString(), i);
    }
    int count = 0;
    const XmlElement *list = child(*root, u"CommentList");
    for (const XmlElement *e : list != nullptr ? children(*list, u"CommentEntry") : QList<const XmlElement *>()) {
        const QString pageId = e->attribute({}, u"PageID");
        const auto at = pageIndex.constFind(pageId);
        if (at == pageIndex.constEnd()) {
            continue;
        }
        Page &page = ctx.drawing->pages[*at];
        Comment c;
        c.id = ctx.drawing->newId(QStringLiteral("m"));
        const auto author = authors.value(e->attribute({}, u"AuthorID"));
        c.author = author.first;
        c.initials = author.second;
        c.date = QDateTime::fromString(e->attribute({}, u"Date"), Qt::ISODate);
        c.date.setTimeZone(QTimeZone::UTC);
        c.text = e->text();
        c.done = e->attribute({}, u"Done") == QLatin1String("1") || e->attribute({}, u"Done") == QLatin1String("true");
        if (e->hasAttribute({}, u"ShapeID")) {
            const auto item = ctx.source->items.constFind(itemKey(pageId, e->attribute({}, u"ShapeID").toInt()));
            if (item != ctx.source->items.constEnd()) {
                c.itemId = item->shape ? item->shape->id : item->connector ? item->connector->id : QString();
            }
        }
        c.extra.insert(QStringLiteral("vsdx"), QJsonObject{{QStringLiteral("commentId"), e->attribute({}, u"CommentID")},
                                                           {QStringLiteral("editDate"), e->attribute({}, u"EditDate")}});
        page.comments.append(c);
        ++count;
    }
    // The pages' snapshots carry their comments as read.
    for (Page &page : ctx.drawing->pages) {
        const QString id = page.extra.value(QLatin1String("vsdx")).toObject().value(QLatin1String("page")).toString();
        if (ctx.source->pages.contains(id)) {
            ctx.source->pages[id].read.comments = page.comments;
        }
    }
    return count;
}

} // namespace QindaOffice::Diagram::Visio
