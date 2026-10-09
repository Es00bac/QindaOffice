// SPDX-License-Identifier: GPL-3.0-or-later
#include "base_csv.h"

#include "base_file_field.h"

#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>

namespace QindaOffice::Base {

namespace {

QString decode(const QByteArray &bytes)
{
    QByteArray data = bytes;
    if (data.startsWith("\xEF\xBB\xBF")) {
        data.remove(0, 3);
    }
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = utf8.decode(data);
    if (!utf8.hasError()) {
        return text;
    }
    // Spreadsheets on other systems still write Windows-1252.
    QStringDecoder legacy("windows-1252");
    return legacy.isValid() ? QString(legacy.decode(data)) : QString::fromLatin1(data);
}

QList<QStringList> split(const QString &text, QChar delimiter)
{
    QList<QStringList> rows;
    QStringList row;
    QString cell;
    bool quoted = false;
    bool fieldStart = true;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    cell.append(ch);
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell.append(ch);
            }
        } else if (ch == QLatin1Char('"') && fieldStart) {
            quoted = true;
            fieldStart = false;
        } else if (ch == delimiter) {
            row.append(cell);
            cell.clear();
            fieldStart = true;
        } else if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')) {
            if (ch == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
            row.append(cell);
            if (!(row.size() == 1 && row.first().isEmpty())) {
                rows.append(row);
            }
            row.clear();
            cell.clear();
            fieldStart = true;
        } else {
            cell.append(ch);
            fieldStart = false;
        }
    }
    if (!cell.isEmpty() || !row.isEmpty()) {
        row.append(cell);
        rows.append(row);
    }
    return rows;
}

// A file field's cell: the bytes as hexadecimal digits (CsvTable).
Parsed parseHexFile(const QString &text)
{
    const QString digits = text.trimmed();
    if (digits.isEmpty()) {
        return {true, {}, {}};
    }
    static const QRegularExpression hex(QStringLiteral("^(?:[0-9A-Fa-f]{2})+$"));
    if (!hex.match(digits).hasMatch()) {
        return {false, {}, QStringLiteral("needs the file's bytes as hexadecimal digits")};
    }
    const QByteArray bytes = QByteArray::fromHex(digits.toLatin1());
    if (bytes.size() > kFileFieldLimit) {
        return {false, {}, QStringLiteral("holds files up to %1").arg(formatByteSize(kFileFieldLimit))};
    }
    return {true, bytes, {}};
}

QString cleanName(QString name, int index)
{
    name = name.trimmed();
    name.remove(QRegularExpression(QStringLiteral("[\"\\[\\]`\\x00-\\x1f]")));
    if (name.isEmpty()) {
        name = QStringLiteral("Field%1").arg(index + 1);
    }
    if (BaseFile::isSystemName(name)) {
        name.prepend(QLatin1Char('_'));
    }
    return name.left(64).trimmed();
}

} // namespace

QVariantMap CsvImportPlan::toMap() const
{
    QVariantList cols;
    for (const CsvColumnPlan &c : columns) {
        cols.append(QVariantMap{{QStringLiteral("source"), c.source},
                                {QStringLiteral("name"), c.name},
                                {QStringLiteral("type"), typeId(c.type)},
                                {QStringLiteral("include"), c.include},
                                {QStringLiteral("primaryKey"), c.primaryKey}});
    }
    return {{QStringLiteral("table"), table},
            {QStringLiteral("createNew"), createNew},
            {QStringLiteral("addId"), addId},
            {QStringLiteral("skipBadRows"), skipBadRows},
            {QStringLiteral("columns"), cols}};
}

CsvImportPlan CsvImportPlan::fromMap(const QVariantMap &map)
{
    CsvImportPlan p;
    p.table = map.value(QStringLiteral("table")).toString().trimmed();
    p.createNew = map.value(QStringLiteral("createNew"), true).toBool();
    p.addId = map.value(QStringLiteral("addId"), true).toBool();
    p.skipBadRows = map.value(QStringLiteral("skipBadRows")).toBool();
    for (const QVariant &v : map.value(QStringLiteral("columns")).toList()) {
        const QVariantMap m = v.toMap();
        p.columns.append({m.value(QStringLiteral("source")).toInt(), m.value(QStringLiteral("name")).toString().trimmed(),
                          typeFromId(m.value(QStringLiteral("type")).toString()).value_or(FieldType::Text),
                          m.value(QStringLiteral("include"), true).toBool(),
                          m.value(QStringLiteral("primaryKey")).toBool()});
    }
    return p;
}

CsvTable parseCsv(const QByteArray &bytes, QChar delimiter, bool headerRow)
{
    CsvTable table;
    const QString text = decode(bytes);
    if (delimiter.isNull()) {
        // The candidate that splits the first lines most consistently.
        const QString sample = text.left(8192);
        int best = -1;
        delimiter = QLatin1Char(',');
        for (const QChar candidate : {QLatin1Char(','), QLatin1Char(';'), QLatin1Char('\t'), QLatin1Char('|')}) {
            const QList<QStringList> rows = split(sample, candidate).mid(0, 10);
            if (rows.isEmpty() || rows.first().size() < 2) {
                continue;
            }
            int consistent = 0;
            for (const QStringList &r : rows) {
                consistent += r.size() == rows.first().size() ? 1 : 0;
            }
            const int score = consistent * 100 + int(rows.first().size());
            if (score > best) {
                best = score;
                delimiter = candidate;
            }
        }
    }
    table.delimiter = delimiter;
    table.headerRow = headerRow;
    QList<QStringList> rows = split(text, delimiter);
    int width = 0;
    for (const QStringList &r : rows) {
        width = std::max(width, int(r.size()));
    }
    if (headerRow && !rows.isEmpty()) {
        table.header = rows.takeFirst();
    }
    while (table.header.size() < width) {
        table.header.append(QStringLiteral("Field%1").arg(table.header.size() + 1));
    }
    for (QStringList &r : rows) {
        while (r.size() < width) {
            r.append(QString());
        }
    }
    table.rows = rows;
    return table;
}

FieldType detectType(const QStringList &values, const QString &header)
{
    QStringList filled;
    for (const QString &v : values) {
        if (!v.trimmed().isEmpty()) {
            filled.append(v.trimmed());
        }
    }
    if (filled.isEmpty()) {
        return FieldType::Text;
    }
    const auto all = [&](auto &&test) { return std::all_of(filled.cbegin(), filled.cend(), test); };
    static const QSet<QString> yesNo{QStringLiteral("yes"), QStringLiteral("no"), QStringLiteral("true"),
                                     QStringLiteral("false"), QStringLiteral("y"), QStringLiteral("n")};
    if (all([&](const QString &v) { return yesNo.contains(v.toLower()); })) {
        return FieldType::YesNo;
    }
    static const QRegularExpression integer(QStringLiteral("^-?\\d{1,3}(,\\d{3})*$|^-?\\d{1,18}$"));
    const bool leadingZero = std::any_of(filled.cbegin(), filled.cend(), [](const QString &v) {
        return v.size() > 1 && v.startsWith(QLatin1Char('0')) && !v.startsWith(QStringLiteral("0."));
    });
    if (!leadingZero && all([&](const QString &v) { return integer.match(v).hasMatch(); })) {
        return FieldType::Integer;
    }
    static const QRegularExpression moneySign(QStringLiteral("[$€£¥]|^\\(.*\\)$"));
    static const QRegularExpression moneyWord(
        QStringLiteral("amount|price|total|cost|fee|donation|paid|balance|salary|rate|dues|value"),
        QRegularExpression::CaseInsensitiveOption);
    const bool money = all([](const QString &v) { return parseMoney(v, 2).has_value(); });
    if (money && (std::any_of(filled.cbegin(), filled.cend(), [](const QString &v) { return moneySign.match(v).hasMatch(); })
                  || moneyWord.match(header).hasMatch())) {
        return FieldType::Currency;
    }
    if (!leadingZero && all([](const QString &v) { return parseInput(FieldType::Number, v).ok; })) {
        return FieldType::Number;
    }
    static const QRegularExpression hasTime(QStringLiteral("\\d:\\d\\d"));
    if (all([](const QString &v) { return !hasTime.match(v).hasMatch() && parseInput(FieldType::Date, v).ok; })) {
        return FieldType::Date;
    }
    if (all([](const QString &v) { return parseInput(FieldType::DateTime, v).ok; })) {
        return FieldType::DateTime;
    }
    return std::any_of(filled.cbegin(), filled.cend(), [](const QString &v) { return v.size() > kShortTextLimit; })
               ? FieldType::LongText
               : FieldType::Text;
}

CsvImportPlan proposeImport(const BaseFile &file, const CsvTable &csv, const QString &table)
{
    CsvImportPlan plan;
    plan.table = table;
    const std::optional<TableDef> existing = file.tableExists(table) ? readTable(file, table) : std::nullopt;
    plan.createNew = !existing;
    QSet<QString> used;
    for (int c = 0; c < csv.header.size(); ++c) {
        QStringList values;
        values.reserve(csv.rows.size());
        for (const QStringList &row : csv.rows) {
            values.append(row.value(c));
        }
        CsvColumnPlan col;
        col.source = c;
        col.name = cleanName(csv.header.at(c), c);
        if (existing) {
            const FieldDef *f = existing->field(col.name);
            col.include = f != nullptr && f->type != FieldType::AutoNumber;
            col.type = f != nullptr ? f->type : FieldType::Text;
            if (f != nullptr) {
                col.name = f->name;
            }
        } else {
            QString unique = col.name;
            for (int n = 2; used.contains(unique.toLower()); ++n) {
                unique = QStringLiteral("%1_%2").arg(col.name).arg(n);
            }
            col.name = unique;
            const std::optional<FieldType> stated = csv.types.value(c);
            col.type = stated ? *stated : detectType(values, csv.header.at(c));
            // An "id" of unique whole numbers keeps its values as the key.
            if (col.name.compare(QLatin1String("id"), Qt::CaseInsensitive) == 0 && col.type == FieldType::Integer) {
                QSet<QString> seen(values.cbegin(), values.cend());
                if (seen.size() == values.size() && !seen.contains(QString())) {
                    col.primaryKey = true;
                    col.type = FieldType::AutoNumber;
                    plan.addId = false;
                }
            }
        }
        used.insert(col.name.toLower());
        plan.columns.append(col);
    }
    if (plan.addId && used.contains(QStringLiteral("id"))) {
        plan.addId = false; // an "id" column that is not a key stays data; no second id
    }
    return plan;
}

ImportResult importCsv(BaseFile &file, const CsvTable &csv, const CsvImportPlan &plan, Change *schemaChange,
                       Change *dataChange)
{
    ImportResult result;
    QList<CsvColumnPlan> included;
    for (const CsvColumnPlan &c : plan.columns) {
        if (c.include && c.source >= 0 && c.source < csv.header.size()) {
            included.append(c);
        }
    }
    if (included.isEmpty()) {
        result.error = QStringLiteral("Choose at least one column to import.");
        return result;
    }
    bool created = false;
    if (plan.createNew) {
        TableDef def;
        def.name = plan.table;
        def.autoIncrement = true;
        if (plan.addId) {
            FieldDef id;
            id.name = QStringLiteral("id");
            id.type = FieldType::AutoNumber;
            id.primaryKey = true;
            def.fields.append(id);
        }
        for (const CsvColumnPlan &c : included) {
            FieldDef f;
            f.name = c.name;
            f.type = c.type;
            f.primaryKey = c.primaryKey;
            def.fields.append(f);
        }
        const SchemaPlan schema = planCreateTable(file, def);
        const Result made = file.applyPlan(schema, schemaChange);
        if (!made.ok) {
            result.error = made.error;
            return result;
        }
        created = true;
    }
    const std::optional<TableDef> target = readTable(file, plan.table);
    if (!target) {
        result.error = QStringLiteral("There is no table named %1.").arg(plan.table);
        return result;
    }
    QStringList names;
    QList<FieldType> types;
    for (const CsvColumnPlan &c : included) {
        const FieldDef *f = target->field(c.name);
        if (f == nullptr) {
            result.error = QStringLiteral("%1 has no field %2.").arg(plan.table, c.name);
            return result;
        }
        names.append(quoteIdentifier(f->name));
        types.append(f->type == FieldType::AutoNumber ? FieldType::Integer : f->type);
    }
    const CurrencyFormat currency = file.currency();
    const Result r = file.capture(QStringLiteral("Import into %1").arg(plan.table), [&]() -> Result {
        QString error;
        Statement insert = file.db().prepare(QStringLiteral("INSERT INTO %1(%2) VALUES(%3)")
                                                 .arg(quoteIdentifier(plan.table), names.join(QStringLiteral(", ")),
                                                      QStringList(names.size(), QStringLiteral("?")).join(QStringLiteral(", "))),
                                             &error);
        if (!insert.isValid()) {
            return Result::failure(error);
        }
        const int headerLines = csv.headerRow ? 1 : 0;
        for (int r = 0; r < csv.rows.size(); ++r) {
            const QStringList &row = csv.rows.at(r);
            QString problem;
            QVariantList values;
            for (int c = 0; c < included.size() && problem.isEmpty(); ++c) {
                const Parsed p = types.at(c) == FieldType::File ? parseHexFile(row.value(included.at(c).source))
                                                                : parseInput(types.at(c), row.value(included.at(c).source),
                                                                             currency);
                if (!p.ok) {
                    problem = QStringLiteral("Line %1, %2 %3 (\"%4\").")
                                  .arg(r + 1 + headerLines)
                                  .arg(included.at(c).name, p.error, row.value(included.at(c).source).left(40));
                }
                values.append(p.value);
            }
            if (problem.isEmpty()) {
                insert.reset();
                insert.bindAll(values);
                if (insert.step() == Statement::Step::Error) {
                    problem = QStringLiteral("Line %1: %2").arg(r + 1 + headerLines).arg(insert.errorText());
                }
            }
            if (!problem.isEmpty()) {
                if (!plan.skipBadRows) {
                    return Result::failure(problem);
                }
                ++result.skipped;
                if (result.problems.size() < 20) {
                    result.problems.append(problem);
                }
                continue;
            }
            ++result.imported;
        }
        return Result::success();
    }, dataChange);
    if (!r.ok) {
        if (created && schemaChange != nullptr) {
            file.applyChange(*schemaChange, true); // never leave a half-imported table
            *schemaChange = Change();
        }
        result.error = r.error;
        result.imported = 0;
        return result;
    }
    result.ok = true;
    return result;
}

QByteArray csvBytes(const QStringList &header, const QList<QStringList> &rows, QChar delimiter)
{
    const auto line = [delimiter](const QStringList &cells) {
        QStringList out;
        for (const QString &cell : cells) {
            if (cell.contains(delimiter) || cell.contains(QLatin1Char('"')) || cell.contains(QLatin1Char('\n'))
                || cell.contains(QLatin1Char('\r'))) {
                QString escaped = cell;
                escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
                out.append(QLatin1Char('"') + escaped + QLatin1Char('"'));
            } else {
                out.append(cell);
            }
        }
        return out.join(delimiter);
    };
    QString text = line(header) + QStringLiteral("\r\n");
    for (const QStringList &row : rows) {
        text += line(row) + QStringLiteral("\r\n");
    }
    return text.toUtf8();
}

Result writeFileAtomically(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return Result::failure(file.errorString());
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        return Result::failure(file.errorString());
    }
    return Result::success();
}

} // namespace QindaOffice::Base
