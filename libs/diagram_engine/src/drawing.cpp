// SPDX-License-Identifier: GPL-3.0-or-later
#include "drawing.h"

#include <QCoreApplication>

#include <cmath>

namespace QindaOffice::Diagram {

int Page::shapeIndex(const QString &shapeId) const
{
    for (int i = 0; i < shapes.size(); ++i) {
        if (shapes.at(i).id == shapeId) {
            return i;
        }
    }
    return -1;
}

int Page::connectorIndex(const QString &connectorId) const
{
    for (int i = 0; i < connectors.size(); ++i) {
        if (connectors.at(i).id == connectorId) {
            return i;
        }
    }
    return -1;
}

const Shape *Page::shape(const QString &shapeId) const
{
    const int i = shapeIndex(shapeId);
    return i < 0 ? nullptr : &shapes.at(i);
}

Shape *Page::shape(const QString &shapeId)
{
    const int i = shapeIndex(shapeId);
    return i < 0 ? nullptr : &shapes[i];
}

const Connector *Page::connector(const QString &connectorId) const
{
    const int i = connectorIndex(connectorId);
    return i < 0 ? nullptr : &connectors.at(i);
}

Connector *Page::connector(const QString &connectorId)
{
    const int i = connectorIndex(connectorId);
    return i < 0 ? nullptr : &connectors[i];
}

bool Page::contains(const QString &itemId) const
{
    return shapeIndex(itemId) >= 0 || connectorIndex(itemId) >= 0;
}

int Drawing::pageIndex(const QString &pageId) const
{
    for (int i = 0; i < pages.size(); ++i) {
        if (pages.at(i).id == pageId) {
            return i;
        }
    }
    return -1;
}

const Page *Drawing::page(const QString &pageId) const
{
    const int i = pageIndex(pageId);
    return i < 0 ? nullptr : &pages.at(i);
}

Page *Drawing::page(const QString &pageId)
{
    const int i = pageIndex(pageId);
    return i < 0 ? nullptr : &pages[i];
}

const DataRecordset *Drawing::recordset(const QString &recordsetId) const
{
    for (const DataRecordset &r : recordsets) {
        if (r.id == recordsetId) {
            return &r;
        }
    }
    return nullptr;
}

DataRecordset *Drawing::recordset(const QString &recordsetId)
{
    for (DataRecordset &r : recordsets) {
        if (r.id == recordsetId) {
            return &r;
        }
    }
    return nullptr;
}

QString DataRecordset::rowKey(int row) const
{
    if (row < 0 || row >= rows.size()) {
        return {};
    }
    if (keyColumn >= 0 && keyColumn < rows.at(row).size()) {
        return rows.at(row).at(keyColumn);
    }
    return QLatin1Char('#') + QString::number(row + 1);
}

int DataRecordset::rowOf(const QString &key) const
{
    if (key.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < rows.size(); ++i) {
        if (rowKey(i) == key) {
            return i;
        }
    }
    return -1;
}

QString Drawing::newId(const QString &prefix)
{
    // AGENT-GUARD: a file edited by hand (or a paste from an older window)
    // may already use the next number; skip until the id is free so no two
    // items ever share one.
    for (;;) {
        const QString id = prefix + QString::number(++serial);
        bool used = pageIndex(id) >= 0 || recordset(id) != nullptr;
        for (const Page &page : std::as_const(pages)) {
            used = used || page.contains(id);
            for (const Comment &comment : page.comments) {
                used = used || comment.id == id;
            }
            for (const Shape &shape : page.shapes) {
                used = used || shape.groupId == id;
            }
            for (const Connector &connector : page.connectors) {
                used = used || connector.groupId == id;
            }
        }
        if (!used) {
            return id;
        }
    }
}

Drawing newDrawing()
{
    Drawing drawing;
    Page page;
    page.id = drawing.newId(QStringLiteral("p"));
    page.name = QCoreApplication::translate("Diagram", "Page-1");
    drawing.pages.append(page);
    return drawing;
}

namespace {

struct Paper {
    const char *name;
    double widthIn;
    double heightIn;
};

// Portrait sizes in inches (ISO sizes from millimetres).
constexpr Paper kPapers[] = {
    {"letter", 8.5, 11.0},
    {"legal", 8.5, 14.0},
    {"tabloid", 11.0, 17.0},
    {"a5", 148.0 / 25.4, 210.0 / 25.4},
    {"a4", 210.0 / 25.4, 297.0 / 25.4},
    {"a3", 297.0 / 25.4, 420.0 / 25.4},
};

} // namespace

QSizeF paperSize(const QString &name)
{
    for (const Paper &p : kPapers) {
        if (name == QLatin1String(p.name)) {
            return {std::round(p.widthIn * 96), std::round(p.heightIn * 96)};
        }
    }
    return {816, 1056};
}

QStringList paperNames()
{
    QStringList names;
    for (const Paper &p : kPapers) {
        names.append(QString::fromLatin1(p.name));
    }
    return names;
}

QString paperNameOf(QSizeF size)
{
    for (const Paper &p : kPapers) {
        const QSizeF paper = paperSize(QString::fromLatin1(p.name));
        const auto near = [](QSizeF a, QSizeF b) {
            return std::abs(a.width() - b.width()) < 1.5 && std::abs(a.height() - b.height()) < 1.5;
        };
        if (near(size, paper) || near(size, paper.transposed())) {
            return QString::fromLatin1(p.name);
        }
    }
    return QStringLiteral("custom");
}

QString dataValue(const QList<DataField> &data, const QString &key, const QString &fallback)
{
    for (const DataField &field : data) {
        if (field.key.compare(key, Qt::CaseInsensitive) == 0) {
            return field.value;
        }
    }
    return fallback;
}

bool setDataValue(QList<DataField> &data, const QString &key, const QString &value)
{
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    for (DataField &field : data) {
        if (field.key.compare(trimmed, Qt::CaseInsensitive) == 0) {
            field.value = value;
            return true;
        }
    }
    data.append({trimmed, value});
    return true;
}

} // namespace QindaOffice::Diagram
