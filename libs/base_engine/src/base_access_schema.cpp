// SPDX-License-Identifier: GPL-3.0-or-later
// mdb-schema's postgres DDL → AccessSchema (base_access_schema.h).
#include "base_access_schema.h"

#include <QRegularExpression>

namespace QindaOffice::Base {

namespace {

// A double-quoted identifier ("" inside is a quote), captured.
const QString kId = QStringLiteral("\"((?:[^\"]|\"\")*)\"");

QString unquote(const QString &captured)
{
    QString out = captured;
    out.replace(QStringLiteral("\"\""), QStringLiteral("\""));
    return out;
}

QString unquoteText(const QString &captured)
{
    QString out = captured;
    out.replace(QStringLiteral("''"), QStringLiteral("'"));
    return out;
}

QRegularExpression pattern(const QString &text)
{
    return QRegularExpression(QStringLiteral("^") + text + QStringLiteral("$"),
                              QRegularExpression::DotMatchesEverythingOption);
}

struct Split {
    QStringList statements; // whitespace outside quotes collapsed to one space
    QStringList comments;   // "-- …" lines, without the dashes
};

// Statements end at ';' outside quotes; "--" starts a comment to the end of
// the line. Whitespace outside quotes collapses, so "NUMERIC(15,2)" and
// "NUMERIC (18, 0)" stay apart.
Split splitDdl(const QString &ddl)
{
    Split out;
    QString current;
    bool space = false;
    const auto flush = [&] {
        const QString s = current.trimmed();
        if (!s.isEmpty()) {
            out.statements.append(s);
        }
        current.clear();
        space = false;
    };
    for (qsizetype i = 0; i < ddl.size(); ++i) {
        const QChar ch = ddl.at(i);
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            if (space) {
                current += QLatin1Char(' ');
                space = false;
            }
            // Copy the quoted run verbatim (a doubled quote stays inside).
            current += ch;
            qsizetype j = i + 1;
            for (; j < ddl.size(); ++j) {
                current += ddl.at(j);
                if (ddl.at(j) == ch) {
                    if (j + 1 < ddl.size() && ddl.at(j + 1) == ch) {
                        current += ddl.at(++j);
                        continue;
                    }
                    break;
                }
            }
            i = j;
            continue;
        }
        if (ch == QLatin1Char('-') && i + 1 < ddl.size() && ddl.at(i + 1) == QLatin1Char('-')) {
            const qsizetype end = ddl.indexOf(QLatin1Char('\n'), i);
            out.comments.append(ddl.mid(i + 2, end < 0 ? -1 : end - i - 2).trimmed());
            i = end < 0 ? ddl.size() : end;
            space = true;
            continue;
        }
        if (ch == QLatin1Char(';')) {
            flush();
            continue;
        }
        if (ch.isSpace()) {
            space = !current.isEmpty();
            continue;
        }
        if (space) {
            current += QLatin1Char(' ');
            space = false;
        }
        current += ch;
    }
    flush();
    return out;
}

// Splits at commas outside quotes and parentheses.
QStringList topLevel(const QString &text)
{
    QStringList parts;
    QString current;
    int depth = 0;
    QChar quote;
    for (const QChar ch : text) {
        if (!quote.isNull()) {
            current += ch;
            if (ch == quote) {
                quote = QChar(); // a doubled quote re-opens on the next character
            }
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
        } else if (ch == QLatin1Char('(')) {
            ++depth;
        } else if (ch == QLatin1Char(')')) {
            --depth;
        } else if (ch == QLatin1Char(',') && depth == 0) {
            parts.append(current.trimmed());
            current.clear();
            continue;
        }
        current += ch;
    }
    if (!current.trimmed().isEmpty()) {
        parts.append(current.trimmed());
    }
    return parts;
}

AccessTable *findTable(QList<AccessTable> &tables, const QString &name)
{
    for (AccessTable &t : tables) {
        if (t.name.compare(name, Qt::CaseInsensitive) == 0) {
            return &t;
        }
    }
    return nullptr;
}

void parseCreate(const QRegularExpressionMatch &m, AccessSchema *schema)
{
    AccessTable table;
    table.name = unquote(m.captured(1));
    static const QRegularExpression columnLine = pattern(kId + QStringLiteral(" (.*)"));
    for (const QString &part : topLevel(m.captured(2))) {
        const QRegularExpressionMatch c = columnLine.match(part);
        if (!c.hasMatch()) {
            continue;
        }
        AccessColumn column;
        column.name = unquote(c.captured(1));
        QString type = c.captured(2).trimmed();
        if (const qsizetype d = type.indexOf(QLatin1String(" DEFAULT ")); d >= 0) {
            type = type.left(d); // only with --default-values, which the import does not pass
        }
        if (type.endsWith(QLatin1String(" NOT NULL"))) {
            column.required = true;
            type.chop(9);
        }
        column.type = type.trimmed();
        table.columns.append(column);
    }
    schema->tables.append(table);
}

void parseKeyList(const QString &list, AccessIndex *index)
{
    static const QRegularExpression item = pattern(kId + QStringLiteral("( DESC)?"));
    for (const QString &part : topLevel(list)) {
        const QRegularExpressionMatch m = item.match(part);
        if (m.hasMatch()) {
            index->columns.append(unquote(m.captured(1)));
            index->descending.append(!m.captured(2).isEmpty());
        }
    }
}

} // namespace

const AccessColumn *AccessTable::column(const QString &wanted) const
{
    for (const AccessColumn &c : columns) {
        if (c.name.compare(wanted, Qt::CaseInsensitive) == 0) {
            return &c;
        }
    }
    return nullptr;
}

QStringList AccessTable::primaryKey() const
{
    for (const AccessIndex &i : indexes) {
        if (i.primary) {
            return i.columns;
        }
    }
    return {};
}

const AccessTable *AccessSchema::table(const QString &name) const
{
    for (const AccessTable &t : tables) {
        if (t.name.compare(name, Qt::CaseInsensitive) == 0) {
            return &t;
        }
    }
    return nullptr;
}

AccessSchema parseAccessSchema(const QString &ddl)
{
    static const QRegularExpression create =
        pattern(QStringLiteral("CREATE TABLE (?:IF NOT EXISTS )?") + kId + QStringLiteral(" ?\\((.*)\\)"));
    static const QRegularExpression primary = pattern(QStringLiteral("ALTER TABLE ") + kId + QStringLiteral(" ADD CONSTRAINT ")
                                                      + kId + QStringLiteral(" PRIMARY KEY ?\\((.*)\\)"));
    static const QRegularExpression index =
        pattern(QStringLiteral("CREATE (UNIQUE )?INDEX ") + kId + QStringLiteral(" ON ") + kId + QStringLiteral(" ?\\((.*)\\)"));
    static const QRegularExpression foreign =
        pattern(QStringLiteral("ALTER TABLE ") + kId + QStringLiteral(" ADD CONSTRAINT ") + kId + QStringLiteral(" FOREIGN KEY ?\\(")
                + kId + QStringLiteral("\\) REFERENCES ") + kId + QStringLiteral(" ?\\(") + kId + QStringLiteral("\\)(.*)"));
    static const QRegularExpression columnComment = pattern(QStringLiteral("COMMENT ON COLUMN ") + kId + QStringLiteral("\\.")
                                                            + kId + QStringLiteral(" IS '((?:[^']|'')*)'"));
    static const QRegularExpression tableComment =
        pattern(QStringLiteral("COMMENT ON TABLE ") + kId + QStringLiteral(" IS '((?:[^']|'')*)'"));
    static const QRegularExpression loose =
        pattern(QStringLiteral("Relationship from ") + kId + QStringLiteral(" ?\\(") + kId + QStringLiteral("\\) to ") + kId
                + QStringLiteral(" ?\\(") + kId + QStringLiteral("\\) does not enforce integrity\\.?"));

    AccessSchema schema;
    const Split split = splitDdl(ddl);
    for (const QString &s : split.statements) {
        QRegularExpressionMatch m;
        if ((m = create.match(s)).hasMatch()) {
            parseCreate(m, &schema);
        } else if ((m = primary.match(s)).hasMatch()) {
            if (AccessTable *t = findTable(schema.tables, unquote(m.captured(1)))) {
                AccessIndex pk;
                pk.name = unquote(m.captured(2));
                pk.primary = true;
                pk.unique = true;
                parseKeyList(m.captured(3), &pk);
                t->indexes.append(pk);
            }
        } else if ((m = index.match(s)).hasMatch()) {
            if (AccessTable *t = findTable(schema.tables, unquote(m.captured(3)))) {
                AccessIndex i;
                i.name = unquote(m.captured(2));
                i.unique = !m.captured(1).isEmpty();
                parseKeyList(m.captured(4), &i);
                t->indexes.append(i);
            }
        } else if ((m = foreign.match(s)).hasMatch()) {
            AccessRelationship r;
            r.childTable = unquote(m.captured(1));
            r.childColumn = unquote(m.captured(3));
            r.parentTable = unquote(m.captured(4));
            r.parentColumn = unquote(m.captured(5));
            r.cascadeUpdate = m.captured(6).contains(QLatin1String("ON UPDATE CASCADE"));
            r.cascadeDelete = m.captured(6).contains(QLatin1String("ON DELETE CASCADE"));
            schema.relationships.append(r);
        } else if ((m = columnComment.match(s)).hasMatch()) {
            if (AccessTable *t = findTable(schema.tables, unquote(m.captured(1)))) {
                for (AccessColumn &c : t->columns) {
                    if (c.name.compare(unquote(m.captured(2)), Qt::CaseInsensitive) == 0) {
                        c.description = unquoteText(m.captured(3));
                    }
                }
            }
        } else if ((m = tableComment.match(s)).hasMatch()) {
            if (AccessTable *t = findTable(schema.tables, unquote(m.captured(1)))) {
                t->description = unquoteText(m.captured(2));
            }
        }
    }
    for (const QString &comment : split.comments) {
        const QRegularExpressionMatch m = loose.match(comment);
        if (m.hasMatch()) {
            AccessRelationship r;
            r.childTable = unquote(m.captured(1));
            r.childColumn = unquote(m.captured(2));
            r.parentTable = unquote(m.captured(3));
            r.parentColumn = unquote(m.captured(4));
            r.enforced = false;
            schema.relationships.append(r);
        }
    }
    return schema;
}

std::optional<FieldType> accessFieldType(const AccessColumn &column)
{
    const QString t = column.type.toUpper();
    const auto starts = [&](const char *prefix) { return t.startsWith(QLatin1String(prefix)); };
    if (starts("SERIAL")) {
        return FieldType::AutoNumber;
    }
    if (starts("BOOL")) {
        return FieldType::YesNo;
    }
    // M86 (D-094): the XML import's words — QindaBase's own type ("QB:date",
    // from its qb:type annotation) and Access's hyperlink — and BIGINT as
    // Access's Large Number.
    if (starts("QB:")) {
        return typeFromId(column.type.mid(3).trimmed());
    }
    if (starts("HYPERLINK")) {
        return FieldType::Hyperlink;
    }
    if (starts("BIGINT")) {
        return FieldType::LargeNumber;
    }
    if (starts("SMALLINT") || starts("INTEGER")) {
        return FieldType::Integer;
    }
    if (t == QLatin1String("NUMERIC(15,2)")) {
        return FieldType::Currency; // the money type prints with no space; a Decimal field as "NUMERIC (p, s)"
    }
    if (starts("REAL") || starts("DOUBLE") || starts("NUMERIC")) {
        return FieldType::Number;
    }
    if (starts("DATE")) {
        return FieldType::Date;
    }
    if (starts("TIMESTAMP")) {
        return FieldType::DateTime;
    }
    if (starts("VARCHAR") || starts("UUID")) {
        return FieldType::Text;
    }
    if (starts("TEXT")) {
        return FieldType::LongText;
    }
    if (starts("BYTEA")) {
        return FieldType::File;
    }
    return std::nullopt; // Unknown_0012 (attachment, multi-valued) and anything newer
}

} // namespace QindaOffice::Base
