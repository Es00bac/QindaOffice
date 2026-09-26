// SPDX-License-Identifier: GPL-3.0-or-later
// Org chart from CSV (importers.h).
#include "importers.h"

#include "layout.h"
#include "ops.h"
#include "painter.h"
#include "stencils.h"

#include <QCoreApplication>
#include <QHash>

namespace QindaOffice::Diagram {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("OrgChartCsv", text);
}

int column(const QStringList &header, std::initializer_list<const char *> names)
{
    for (int i = 0; i < header.size(); ++i) {
        const QString h = header.at(i).trimmed().toLower();
        for (const char *name : names) {
            if (h == QLatin1String(name)) {
                return i;
            }
        }
    }
    return -1;
}

} // namespace

QVector<QStringList> parseCsv(const QString &text)
{
    QVector<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    bool any = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field.append(QLatin1Char('"'));
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.append(ch);
            }
            continue;
        }
        if (ch == QLatin1Char('"')) {
            quoted = true;
            any = true;
        } else if (ch == QLatin1Char(',')) {
            row.append(field);
            field.clear();
            any = true;
        } else if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')) {
            if (ch == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
            if (any || !field.isEmpty()) {
                row.append(field);
                rows.append(row);
            }
            row.clear();
            field.clear();
            any = false;
        } else {
            field.append(ch);
            any = true;
        }
    }
    if (any || !field.isEmpty()) {
        row.append(field);
        rows.append(row);
    }
    return rows;
}

bool importOrgChartCsv(Drawing &drawing, Page &page, const QByteArray &csv, OrgChartResult *result, QString *error)
{
    QString text = QString::fromUtf8(csv);
    if (text.startsWith(QChar(0xfeff))) {
        text.remove(0, 1);
    }
    const QVector<QStringList> rows = parseCsv(text);
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (rows.size() < 2) {
        return fail(tr("The CSV file needs a header row and at least one person."));
    }
    const QStringList header = rows.first();
    const int name = column(header, {"name", "full name", "fullname", "person", "employee", "volunteer"});
    const int title = column(header, {"title", "position", "role", "job title", "job"});
    const int manager = column(header, {"manager", "reports to", "reportsto", "supervisor", "parent", "boss"});
    if (name < 0) {
        return fail(tr("The CSV file has no Name column (expected Name, Title, Manager)."));
    }
    OrgChartResult out;
    // Start below whatever is on the page already.
    const QRectF existing = itemsBounds(page);
    const double top = existing.isNull() ? 48 : existing.bottom() + 60;
    QHash<QString, QString> byName;
    QVector<QPair<QString, QString>> reports; // (shape id, manager name)
    for (int r = 1; r < rows.size(); ++r) {
        const QStringList &row = rows.at(r);
        const QString person = row.value(name).trimmed();
        if (person.isEmpty()) {
            out.warnings.append(tr("Row %1 has no name and was skipped.").arg(r + 1));
            continue;
        }
        const QString role = title >= 0 ? row.value(title).trimmed() : QString();
        const QString boss = manager >= 0 ? row.value(manager).trimmed() : QString();
        const QString type = boss.isEmpty() ? QStringLiteral("orgExecutive") : QStringLiteral("orgPerson");
        const QString id = Ops::addShape(drawing, page, type, QPointF(160 + r * 10, top + 40),
                                         role.isEmpty() ? person : person + QLatin1Char('\n') + role);
        Shape *shape = page.shape(id);
        for (int c = 0; c < header.size(); ++c) {
            const QString key = header.at(c).trimmed();
            if (!key.isEmpty() && !row.value(c).trimmed().isEmpty()) {
                setDataValue(shape->data, key, row.value(c).trimmed());
            }
        }
        if (!byName.contains(person.toLower())) {
            byName.insert(person.toLower(), id);
        }
        out.shapeIds.append(id);
        if (!boss.isEmpty()) {
            reports.append({id, boss});
        }
    }
    for (const auto &report : std::as_const(reports)) {
        const QString bossId = byName.value(report.second.toLower());
        if (bossId.isEmpty()) {
            out.warnings.append(tr("%1's manager \"%2\" is not in the file.")
                                    .arg(page.shape(report.first)->text.section(QLatin1Char('\n'), 0, 0), report.second));
            continue;
        }
        // Bottom of the manager to the top of the report (ports 2 and 0).
        const QString connector = Ops::addConnector(drawing, page, Endpoint{bossId, 2, {}},
                                                    Endpoint{report.first, 0, {}});
        if (Connector *c = page.connector(connector)) {
            c->endArrow = ArrowStyle::None;
        }
        out.connectorIds.append(connector);
    }
    if (out.shapeIds.isEmpty()) {
        return fail(tr("The CSV file lists nobody."));
    }
    if (out.shapeIds.size() > 1) {
        LayoutOptions options;
        options.kind = LayoutKind::Tree;
        options.nodeGap = 28;
        options.rankGap = 48;
        autoLayout(page, out.shapeIds, options);
    }
    if (result != nullptr) {
        *result = out;
    }
    return true;
}

} // namespace QindaOffice::Diagram
