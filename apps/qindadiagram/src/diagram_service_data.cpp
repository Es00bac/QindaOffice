// SPDX-License-Identifier: GPL-3.0-or-later
// DiagramService (M87, D-095): external data — Visio's Link Data to Shapes
// from an Excel workbook or a CSV file, the External Data window's reads,
// linking, refresh and shapes made from rows (data_link.h).
#include "diagram_service.h"

#include "data_link.h"
#include "drawing_document.h"

#include <QFileInfo>
#include <QLocale>

namespace QindaOffice::Apps::Diagram {

using namespace ::QindaOffice::Diagram;

namespace {

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

QVariantList rowsOf(const QVector<QStringList> &rows, int limit)
{
    QVariantList out;
    for (int i = 0; i < rows.size() && i < limit; ++i) {
        out.append(QVariant(rows.at(i)));
    }
    return out;
}

} // namespace

QVariantMap DiagramService::readDataFile(const QUrl &file, const QString &sheet)
{
    DataTable table;
    QString error;
    if (!readDataTable(localPath(file), sheet, &table, &error)) {
        return {{QStringLiteral("error"), error}};
    }
    return {{QStringLiteral("columns"), table.columns},
            {QStringLiteral("rows"), rowsOf(table.rows, 50)},
            {QStringLiteral("rowCount"), int(table.rows.size())},
            {QStringLiteral("sheets"), table.sheets},
            {QStringLiteral("sheet"), table.sheet}};
}

QString DiagramService::linkData(const QUrl &file, const QString &sheet, int keyColumn, int matchColumn,
                                 const QString &shapeField)
{
    const QString path = localPath(file);
    DataTable table;
    QString error;
    if (!readDataTable(path, sheet, &table, &error)) {
        refuse(tr("Link Data to Shapes"), error);
        return {};
    }
    QString id;
    int linked = 0;
    const QString pageId = m_pageId;
    m_document->edit(tr("Link Data to Shapes"), [&](Drawing &d) {
        const QString name = QFileInfo(path).completeBaseName() + (table.sheet.isEmpty() ? QString() : QLatin1Char('$') + table.sheet);
        id = Ops::addRecordset(d, table, name, path, keyColumn, QDateTime::currentDateTimeUtc());
        Page *page = d.page(pageId);
        if (!id.isEmpty() && page != nullptr && matchColumn >= 0) {
            linked = Ops::autoLink(d, *page, id, matchColumn, shapeField);
        }
        return !id.isEmpty();
    });
    if (id.isEmpty()) {
        refuse(tr("Link Data to Shapes"), tr("%1 holds no table.").arg(QFileInfo(path).fileName()));
        return {};
    }
    emit notify(tr("Data linked"),
                tr("%n row(s) from %1; %2 shape(s) linked on this page.", nullptr, int(table.rows.size()))
                    .arg(QFileInfo(path).fileName())
                    .arg(linked),
                QStringLiteral("success"));
    return id;
}

QVariantList DiagramService::recordsetList() const
{
    QVariantList out;
    const QLocale locale;
    for (const DataRecordset &r : drawing().recordsets) {
        int linked = 0;
        for (const Page &page : drawing().pages) {
            for (const Shape &s : page.shapes) {
                linked += s.dataLink.recordset == r.id ? 1 : 0;
            }
        }
        out.append(QVariantMap{{QStringLiteral("id"), r.id},
                               {QStringLiteral("name"), r.name},
                               {QStringLiteral("source"), r.source},
                               {QStringLiteral("sheet"), r.sheet},
                               {QStringLiteral("columns"), r.columns},
                               {QStringLiteral("rowCount"), int(r.rows.size())},
                               {QStringLiteral("keyColumn"), r.keyColumn},
                               {QStringLiteral("refreshed"),
                                r.refreshed.isValid() ? locale.toString(r.refreshed.toLocalTime(), QLocale::ShortFormat) : QString()},
                               {QStringLiteral("linked"), linked}});
    }
    return out;
}

QVariantList DiagramService::recordsetRows(const QString &recordsetId) const
{
    QVariantList out;
    const DataRecordset *r = drawing().recordset(recordsetId);
    if (r == nullptr) {
        return out;
    }
    QHash<QString, int> links;
    for (const Page &page : drawing().pages) {
        for (const Shape &s : page.shapes) {
            if (s.dataLink.recordset == recordsetId) {
                links[s.dataLink.row] += 1;
            }
        }
    }
    for (int i = 0; i < r->rows.size(); ++i) {
        out.append(QVariantMap{{QStringLiteral("row"), i},
                               {QStringLiteral("cells"), QVariant(r->rows.at(i))},
                               {QStringLiteral("linked"), links.value(r->rowKey(i))}});
    }
    return out;
}

bool DiagramService::linkSelectionToRow(const QString &recordsetId, int row)
{
    const QStringList ids = m_selection;
    const bool ok = editPage(tr("Link to row"), [&](Drawing &d, Page &page) {
        return Ops::linkShapes(d, page, ids, recordsetId, row);
    });
    if (!ok && ids.isEmpty()) {
        refuse(tr("Link to row"), tr("Select the shapes to link first."));
    }
    return ok;
}

int DiagramService::autoLinkData(const QString &recordsetId, int column, const QString &shapeField)
{
    int linked = 0;
    const QStringList ids = m_selection;
    editPage(tr("Automatically link"), [&](Drawing &d, Page &page) {
        linked = Ops::autoLink(d, page, recordsetId, column, shapeField, ids);
        return linked > 0;
    });
    emit notify(tr("Automatically link"), tr("%n shape(s) linked.", nullptr, linked),
                linked > 0 ? QStringLiteral("success") : QStringLiteral("info"));
    return linked;
}

bool DiagramService::unlinkSelection()
{
    const QStringList ids = m_selection;
    return editPage(tr("Unlink from row"), [&](Drawing &, Page &page) { return Ops::unlinkShapes(page, ids); });
}

bool DiagramService::refreshData(const QString &recordsetId)
{
    // Every recordset (Refresh All), or the one named; each reads its file again.
    QList<QPair<QString, DataTable>> tables;
    QStringList problems;
    for (const DataRecordset &r : drawing().recordsets) {
        if (!recordsetId.isEmpty() && r.id != recordsetId) {
            continue;
        }
        DataTable table;
        QString error;
        if (readDataTable(r.source, r.sheet, &table, &error)) {
            tables.append({r.id, table});
        } else {
            problems.append(tr("%1: %2").arg(r.name, error));
        }
    }
    int missing = 0;
    const bool ok = !tables.isEmpty() && m_document->edit(tr("Refresh data"), [&](Drawing &d) {
        bool changed = false;
        for (const auto &[id, table] : std::as_const(tables)) {
            int gone = 0;
            changed = Ops::refreshRecordset(d, id, table, QDateTime::currentDateTimeUtc(), &gone) || changed;
            missing += gone;
        }
        return changed;
    });
    if (missing > 0) {
        problems.append(tr("%n linked shape(s) have no row in the data any more; they keep their values.", nullptr, missing));
    }
    emit notify(tr("Refresh data"), problems.isEmpty() ? tr("The data is up to date.") : problems.join(QLatin1Char('\n')),
                problems.isEmpty() ? QStringLiteral("success") : QStringLiteral("warning"));
    return ok;
}

bool DiagramService::removeRecordset(const QString &recordsetId)
{
    return m_document->edit(tr("Remove data"), [&](Drawing &d) { return Ops::removeRecordset(d, recordsetId); });
}

QStringList DiagramService::shapesFromRows(const QString &recordsetId, const QVariantList &rows,
                                           const QString &stencilId, int textColumn)
{
    QList<int> indexes;
    for (const QVariant &v : rows) {
        indexes.append(v.toInt());
    }
    QStringList made;
    // Under what the page already holds (Visio drops them where the rows
    // are dragged; with no drag, below the drawing keeps them clear of it).
    const QRectF content = contentBounds();
    const QPointF origin = content.isNull() ? QPointF(120, 120) : QPointF(content.left() + 72, content.bottom() + 84);
    editPage(tr("Shapes from data"), [&](Drawing &d, Page &p) {
        made = Ops::shapesFromRows(d, p, recordsetId, indexes, stencilId, origin, textColumn);
        return !made.isEmpty();
    });
    if (!made.isEmpty()) {
        selectAndReveal(made);
    }
    return made;
}

} // namespace QindaOffice::Apps::Diagram
