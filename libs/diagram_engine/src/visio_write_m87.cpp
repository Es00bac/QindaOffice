// SPDX-License-Identifier: GPL-3.0-or-later
// What M87 (D-095) writes into a Visio package (visio_write_m87.h).
#include "visio_write_m87.h"

#include "ops.h"
#include "stencils.h"

#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Visio {

namespace {

const QString kZero = QStringLiteral("0");
const QString kOne = QStringLiteral("1");

QString flag(bool on)
{
    return on ? kOne : kZero;
}

XmlElement *namedRow(XmlElement &section, const QString &name)
{
    for (Ooxml::XmlChild &c : section.children) {
        if (c.element && c.element->is(kNs, u"Row") && c.element->attribute({}, u"N") == name) {
            return c.element.get();
        }
    }
    return nullptr;
}

// A User row `name` (the shape's own, made when missing) set to `value`.
void setUser(XmlElement &shape, const QString &name, const QString &value, bool text)
{
    XmlElement *user = section(shape, u"User");
    if (user == nullptr) {
        XmlElementPtr fresh = makeElement(QStringLiteral("Section"));
        fresh->setAttribute({}, QStringLiteral("N"), QStringLiteral("User"));
        user = fresh.get();
        shape.insertElementBefore(fresh, {u"Text", u"Data1", u"Data2", u"Data3", u"ForeignData", u"Shapes"});
    }
    XmlElement *row = namedRow(*user, name);
    if (row == nullptr) {
        row = &appendElement(*user, QStringLiteral("Row"));
        row->setAttribute({}, QStringLiteral("N"), name);
    }
    setCell(*row, QStringLiteral("Value"), value, {}, text ? QStringLiteral("STR") : QString());
    if (row->elements(kNs, u"Cell").size() < 2) {
        appendCell(*row, QStringLiteral("Prompt"), QString());
    }
}

QString paperKind(QSizeF paper)
{
    // Windows' DMPAPER (visio_read_m87.cpp): Letter 1, Tabloid 3, Legal 5,
    // A3 8, A4 9, A5 11; anything else is a size Visio's paper list lacks.
    const QString name = paperNameOf(paper);
    static const QHash<QString, QString> kinds{{QStringLiteral("letter"), QStringLiteral("1")},
                                               {QStringLiteral("tabloid"), QStringLiteral("3")},
                                               {QStringLiteral("legal"), QStringLiteral("5")},
                                               {QStringLiteral("a3"), QStringLiteral("8")},
                                               {QStringLiteral("a4"), QStringLiteral("9")},
                                               {QStringLiteral("a5"), QStringLiteral("11")}};
    return kinds.value(name);
}

QString dateText(const QDateTime &when)
{
    return when.isValid() ? when.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) : QString();
}

} // namespace

void writeLayerMember(XmlElement &shape, const QList<int> &layers)
{
    QStringList parts;
    for (const int i : layers) {
        parts.append(QString::number(i));
    }
    setCell(shape, QStringLiteral("LayerMember"), parts.join(QLatin1Char(';')));
}

void writeHyperlinks(XmlElement &shape, const QList<Hyperlink> &links, const Sheet *master)
{
    // Rows a master lends that the new list does not overwrite are deleted.
    QStringList inherited;
    if (const Section *lent = master != nullptr ? master->section(u"Hyperlink") : nullptr) {
        for (const auto &[name, row] : lent->namedRows()) {
            Q_UNUSED(row)
            inherited.append(name);
        }
    }
    removeSections(shape, u"Hyperlink");
    XmlElementPtr s = makeElement(QStringLiteral("Section"));
    s->setAttribute({}, QStringLiteral("N"), QStringLiteral("Hyperlink"));
    QStringList written;
    for (int i = 0; i < links.size(); ++i) {
        const Hyperlink &link = links.at(i);
        XmlElement &row = appendElement(*s, QStringLiteral("Row"));
        const QString name = QStringLiteral("Row_%1").arg(i + 1);
        written.append(name);
        row.setAttribute({}, QStringLiteral("N"), name);
        appendCell(row, QStringLiteral("Description"), link.description);
        appendCell(row, QStringLiteral("Address"), link.address);
        appendCell(row, QStringLiteral("SubAddress"), link.subAddress);
        appendCell(row, QStringLiteral("ExtraInfo"), QString());
        appendCell(row, QStringLiteral("Frame"), QString());
        appendCell(row, QStringLiteral("NewWindow"), kZero);
        appendCell(row, QStringLiteral("Default"), kZero);
        appendCell(row, QStringLiteral("Invisible"), kZero);
        appendCell(row, QStringLiteral("SortKey"), QString());
    }
    for (const QString &name : std::as_const(inherited)) {
        if (!written.contains(name)) {
            XmlElement &del = appendElement(*s, QStringLiteral("Row"));
            del.setAttribute({}, QStringLiteral("N"), name);
            del.setAttribute({}, QStringLiteral("Del"), kOne);
        }
    }
    if (!s->children.isEmpty()) {
        shape.insertElementBefore(s, {u"Text", u"Data1", u"Data2", u"Data3", u"ForeignData", u"Shapes"});
    }
}

void writeContainerCells(XmlElement &shape, const Shape &container, double pxPerInch)
{
    if (!isContainer(container)) {
        return;
    }
    setUser(shape, QStringLiteral("msvStructureType"), QStringLiteral("Container"), true);
    QString category;
    if (Ops::isLane(container)) {
        category = QStringLiteral("Swimlane;DoNotContain");
    } else if (container.type == QLatin1String("phase") || container.structure == QLatin1String("phase")) {
        category = QStringLiteral("Phase;DoNotContain");
    }
    if (!category.isEmpty()) {
        setUser(shape, QStringLiteral("msvShapeCategories"), category, true);
    }
    const ContainerResize resize = Ops::effectiveResize(container);
    setUser(shape, QStringLiteral("msvSDContainerResize"),
            resize == ContainerResize::None ? kZero : resize == ContainerResize::Fit ? QStringLiteral("2") : kOne, false);
    setUser(shape, QStringLiteral("msvSDContainerMargin"),
            numberText(Ops::containerMargin(container) / std::max(1e-9, pxPerInch)), false);
}

void writeRelationships(XmlElement &shape, const QList<int> &members, const QList<int> &containers)
{
    const auto term = [](int code, const QList<int> &ids) {
        QStringList refs{QString::number(code)};
        for (const int id : ids) {
            refs.append(QStringLiteral("Sheet.%1!SheetRef()").arg(id));
        }
        return QStringLiteral("DEPENDSON(%1)").arg(refs.join(QLatin1Char(',')));
    };
    QStringList terms;
    if (!members.isEmpty()) {
        terms.append(term(1, members));
    }
    if (!containers.isEmpty()) {
        terms.append(term(4, containers));
    }
    if (terms.isEmpty()) {
        removeCell(shape, u"Relationships");
        return;
    }
    setCell(shape, QStringLiteral("Relationships"), kZero, QStringLiteral("SUM(%1)").arg(terms.join(QLatin1Char(','))));
}

bool pageSettingsDiffer(const Page &page, const Page &read)
{
    return page.layers != read.layers || page.lineJumps != read.lineJumps || page.jumpOn != read.jumpOn
        || page.jumpStyle != read.jumpStyle || !(page.print == read.print) || page.autoSize != read.autoSize;
}

void writePageSettings(XmlElement &pageSheet, const Page &page, const Page *read)
{
    if (read == nullptr || page.layers != read->layers) {
        removeSections(pageSheet, u"Layer");
        if (!page.layers.isEmpty()) {
            XmlElement &s = appendElement(pageSheet, QStringLiteral("Section"));
            s.setAttribute({}, QStringLiteral("N"), QStringLiteral("Layer"));
            for (int i = 0; i < page.layers.size(); ++i) {
                const Layer &l = page.layers.at(i);
                XmlElement &row = appendElement(s, QStringLiteral("Row"));
                row.setAttribute({}, QStringLiteral("IX"), QString::number(i));
                const bool coloured = l.color.isValid() && l.color.alpha() > 0;
                appendCell(row, QStringLiteral("Name"), l.name);
                appendCell(row, QStringLiteral("Color"), coloured ? colorText(l.color) : QStringLiteral("255"));
                appendCell(row, QStringLiteral("Status"), kZero);
                appendCell(row, QStringLiteral("Visible"), flag(l.visible));
                appendCell(row, QStringLiteral("Print"), flag(l.print));
                appendCell(row, QStringLiteral("Active"), flag(l.active));
                appendCell(row, QStringLiteral("Lock"), flag(l.locked));
                appendCell(row, QStringLiteral("Snap"), flag(l.snap));
                appendCell(row, QStringLiteral("Glue"), flag(l.glue));
                appendCell(row, QStringLiteral("NameUniv"), l.name);
                appendCell(row, QStringLiteral("ColorTrans"), numberText(coloured ? 1.0 - l.color.alphaF() : 0.0));
            }
        }
    }
    if (read == nullptr || page.lineJumps != read->lineJumps || page.jumpOn != read->jumpOn
        || page.jumpStyle != read->jumpStyle) {
        const QString code = !page.lineJumps                     ? kZero
                           : page.jumpOn == JumpOn::Horizontal ? kOne
                           : page.jumpOn == JumpOn::Vertical   ? QStringLiteral("2")
                                                               : QStringLiteral("3");
        setCell(pageSheet, QStringLiteral("LineJumpCode"), code);
        setCell(pageSheet, QStringLiteral("LineJumpStyle"),
                page.jumpStyle == JumpStyle::Gap ? QStringLiteral("2") : page.jumpStyle == JumpStyle::Square ? QStringLiteral("3")
                                                                                                             : kZero);
    }
    if (read == nullptr || page.autoSize != read->autoSize) {
        setCell(pageSheet, QStringLiteral("DrawingResizeType"), page.autoSize ? kOne : QStringLiteral("2"));
    }
    if (read == nullptr || !(page.print == read->print)) {
        const PrintSetup &p = page.print;
        const bool fit = p.zoom <= 0;
        setCell(pageSheet, QStringLiteral("OnPage"), flag(fit));
        setCell(pageSheet, QStringLiteral("PagesX"), QString::number(std::max(1, p.sheetsAcross)));
        setCell(pageSheet, QStringLiteral("PagesY"), QString::number(std::max(1, p.sheetsDown)));
        setCell(pageSheet, QStringLiteral("ScaleX"), numberText(fit ? 1.0 : p.zoom));
        setCell(pageSheet, QStringLiteral("ScaleY"), numberText(fit ? 1.0 : p.zoom));
        setCell(pageSheet, QStringLiteral("CenterX"), flag(p.center));
        setCell(pageSheet, QStringLiteral("CenterY"), flag(p.center));
        const QSizeF paper = p.paper.isEmpty() ? page.size : p.paper;
        setCell(pageSheet, QStringLiteral("PrintPageOrientation"), paper.width() > paper.height() ? QStringLiteral("2") : kOne);
        const QString kind = p.paper.isEmpty() ? QString() : paperKind(p.paper);
        if (kind.isEmpty()) {
            removeCell(pageSheet, u"PaperKind");
        } else {
            setCell(pageSheet, QStringLiteral("PaperKind"), kind);
        }
    }
}

XmlElementPtr commentsElement(const Drawing &drawing, const QHash<QString, QString> &pageIds,
                              const QHash<QString, QHash<QString, int>> &shapeIds)
{
    QList<QPair<QString, QString>> authors; // (name, initials), ID = index + 1
    QSet<int> usedIds;
    int nextId = 1;
    for (const Page &page : drawing.pages) {
        for (const Comment &c : page.comments) {
            const int id = c.extra.value(QLatin1String("vsdx")).toObject().value(QLatin1String("commentId")).toString().toInt();
            nextId = std::max(nextId, id + 1);
        }
    }
    XmlElementPtr root = makeElement(QStringLiteral("Comments"));
    root->setAttribute(QString::fromUtf16(Ooxml::Ns::XML), QStringLiteral("space"), QStringLiteral("preserve"));
    root->setAttribute({}, QStringLiteral("ShowCommentTags"), kOne);
    XmlElement &authorList = appendElement(*root, QStringLiteral("AuthorList"));
    XmlElement &list = appendElement(*root, QStringLiteral("CommentList"));
    int count = 0;
    for (const Page &page : drawing.pages) {
        const QString pageId = pageIds.value(page.id);
        if (pageId.isEmpty()) {
            continue;
        }
        for (const Comment &c : page.comments) {
            const QPair<QString, QString> author{c.author.isEmpty() ? QStringLiteral("QindaDiagram") : c.author, c.initials};
            qsizetype a = authors.indexOf(author);
            if (a < 0) {
                authors.append(author);
                a = authors.size() - 1;
            }
            XmlElement &e = appendElement(list, QStringLiteral("CommentEntry"));
            e.setAttribute({}, QStringLiteral("AuthorID"), QString::number(a + 1));
            e.setAttribute({}, QStringLiteral("PageID"), pageId);
            const int shape = shapeIds.value(page.id).value(c.itemId, 0);
            if (!c.itemId.isEmpty() && shape > 0) {
                e.setAttribute({}, QStringLiteral("ShapeID"), QString::number(shape));
            }
            const QDateTime when = c.date.isValid() ? c.date : QDateTime::currentDateTimeUtc();
            e.setAttribute({}, QStringLiteral("Date"), dateText(when));
            e.setAttribute({}, QStringLiteral("EditDate"), dateText(when));
            e.setAttribute({}, QStringLiteral("Done"), flag(c.done));
            int id = c.extra.value(QLatin1String("vsdx")).toObject().value(QLatin1String("commentId")).toString().toInt();
            if (id <= 0 || usedIds.contains(id)) {
                id = nextId++;
            }
            usedIds.insert(id);
            e.setAttribute({}, QStringLiteral("CommentID"), QString::number(id));
            e.setAttribute({}, QStringLiteral("AutoCommentType"), kZero);
            e.appendText(c.text);
            ++count;
        }
    }
    for (int i = 0; i < authors.size(); ++i) {
        XmlElement &a = appendElement(authorList, QStringLiteral("AuthorEntry"));
        a.setAttribute({}, QStringLiteral("Name"), authors.at(i).first);
        a.setAttribute({}, QStringLiteral("Initials"), authors.at(i).second);
        a.setAttribute({}, QStringLiteral("ID"), QString::number(i + 1));
    }
    return count > 0 ? root : nullptr;
}

} // namespace QindaOffice::Diagram::Visio
