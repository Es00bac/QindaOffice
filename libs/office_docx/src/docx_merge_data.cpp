// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_merge_data.h"

#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QStringDecoder>

#include <algorithm>

namespace QindaOffice::Docx {

namespace {

QChar sniffDelimiter(const QString &text)
{
    const QString firstLine = text.section(QLatin1Char('\n'), 0, 0);
    int best = 0;
    QChar delimiter = QLatin1Char(',');
    for (const QChar candidate : {QLatin1Char(','), QLatin1Char('\t'), QLatin1Char(';'), QLatin1Char('|')}) {
        int count = 0;
        bool quoted = false;
        for (const QChar c : firstLine) {
            if (c == QLatin1Char('"')) {
                quoted = !quoted;
            } else if (c == candidate && !quoted) {
                ++count;
            }
        }
        if (count > best) {
            best = count;
            delimiter = candidate;
        }
    }
    return delimiter;
}

QString decode(const QByteArray &bytes)
{
    QByteArray data = bytes;
    if (data.startsWith("\xEF\xBB\xBF")) {
        data.remove(0, 3);
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8.decode(data);
    if (!utf8.hasError()) {
        return text;
    }
    return QString::fromLatin1(data);
}

bool isNumber(const QString &text, double *value)
{
    bool ok = false;
    QString clean = text.trimmed();
    clean.remove(QLatin1Char(','));
    *value = clean.toDouble(&ok);
    return ok && !clean.isEmpty();
}

int compareValues(const QString &a, const QString &b)
{
    double x = 0;
    double y = 0;
    if (isNumber(a, &x) && isNumber(b, &y)) {
        return x < y ? -1 : x > y ? 1 : 0;
    }
    return QString::compare(a.trimmed(), b.trimmed(), Qt::CaseInsensitive);
}

bool matches(const QString &value, const MergeFilter &f)
{
    if (f.op == u"blank") {
        return value.trimmed().isEmpty();
    }
    if (f.op == u"notBlank") {
        return !value.trimmed().isEmpty();
    }
    if (f.op == u"contains") {
        return value.contains(f.value, Qt::CaseInsensitive);
    }
    if (f.op == u"notContains") {
        return !value.contains(f.value, Qt::CaseInsensitive);
    }
    const int c = compareValues(value, f.value);
    if (f.op == u"notEqual") {
        return c != 0;
    }
    if (f.op == u"less") {
        return c < 0;
    }
    if (f.op == u"greater") {
        return c > 0;
    }
    if (f.op == u"lessEqual") {
        return c <= 0;
    }
    if (f.op == u"greaterEqual") {
        return c >= 0;
    }
    return c == 0;
}

QString csvField(const QString &value)
{
    if (!value.contains(QLatin1Char(',')) && !value.contains(QLatin1Char('"')) && !value.contains(QLatin1Char('\n'))
        && !value.contains(QLatin1Char('\r'))) {
        return value;
    }
    QString out = value;
    return QLatin1Char('"') + out.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
}

} // namespace

int MergeData::fieldIndex(const QString &name) const
{
    for (int i = 0; i < fields.size(); ++i) {
        if (fields.at(i).compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

QString MergeData::value(int record, const QString &field) const
{
    const int column = fieldIndex(field);
    if (record < 0 || record >= records.size() || column < 0) {
        return {};
    }
    return records.at(record).value(column);
}

MergeData parseDelimited(const QByteArray &bytes, QString *error)
{
    MergeData data;
    const QString text = decode(bytes);
    const QChar delimiter = sniffDelimiter(text);
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    bool any = false;
    auto endField = [&] {
        row.append(field);
        field.clear();
    };
    auto endRow = [&] {
        endField();
        const bool blank = row.size() == 1 && row.first().isEmpty();
        if (!blank) {
            rows.append(row);
        }
        row.clear();
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        any = true;
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field.append(c);
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.append(c);
            }
        } else if (c == QLatin1Char('"') && field.isEmpty()) {
            quoted = true;
        } else if (c == delimiter) {
            endField();
        } else if (c == QLatin1Char('\n')) {
            endRow();
            if (rows.size() > kMaxMergeRecords) {
                if (error != nullptr) {
                    *error = QStringLiteral("more than %1 records").arg(kMaxMergeRecords);
                }
                return {};
            }
        } else if (c != QLatin1Char('\r')) {
            field.append(c);
        }
    }
    if (any && (!field.isEmpty() || !row.isEmpty())) {
        endRow();
    }
    if (rows.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("the file has no header row");
        }
        return {};
    }
    data.fields = rows.takeFirst();
    for (QString &name : data.fields) {
        name = name.trimmed();
    }
    if (data.fields.size() > kMaxMergeFields) {
        if (error != nullptr) {
            *error = QStringLiteral("more than %1 columns").arg(kMaxMergeFields);
        }
        return {};
    }
    for (int i = 0; i < data.fields.size(); ++i) {
        if (data.fields.at(i).isEmpty()) {
            data.fields[i] = QStringLiteral("Field%1").arg(i + 1);
        }
    }
    for (QStringList &record : rows) {
        while (record.size() < data.fields.size()) {
            record.append(QString());
        }
        record = record.mid(0, data.fields.size());
        data.records.append(record);
    }
    return data;
}

MergeData readDelimitedFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return {};
    }
    if (file.size() > kMaxMergeFileBytes) {
        if (error != nullptr) {
            *error = QStringLiteral("the file is larger than %1 MiB").arg(kMaxMergeFileBytes / (1024 * 1024));
        }
        return {};
    }
    MergeData data = parseDelimited(file.readAll(), error);
    data.source = path;
    return data;
}

QByteArray csvBytes(const MergeData &data)
{
    QString out;
    auto line = [&out](const QStringList &values) {
        QStringList cells;
        for (const QString &v : values) {
            cells.append(csvField(v));
        }
        out += cells.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    };
    line(data.fields);
    for (const QStringList &record : data.records) {
        line(record);
    }
    return out.toUtf8();
}

bool writeCsvFile(const QString &path, const MergeData &data, QString *error)
{
    QSaveFile file(path);
    const QByteArray bytes = csvBytes(data);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

bool passesFilters(const MergeData &data, int record, const QList<MergeFilter> &filters)
{
    if (filters.isEmpty()) {
        return true;
    }
    // AND binds tighter than OR: a disjunction of conjunctions.
    bool any = false;
    bool group = true;
    for (int i = 0; i < filters.size(); ++i) {
        const MergeFilter &f = filters.at(i);
        if (i > 0 && f.join == u"or") {
            any = any || group;
            group = true;
        }
        group = group && matches(data.value(record, f.field), f);
    }
    return any || group;
}

QList<int> filteredOrder(const MergeData &data, const MailMergeSettings &settings)
{
    QList<int> order;
    for (int i = 0; i < data.records.size(); ++i) {
        if (passesFilters(data, i, settings.filters)) {
            order.append(i);
        }
    }
    if (!settings.sorts.isEmpty()) {
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            for (const MergeSort &s : settings.sorts) {
                const int c = compareValues(data.value(a, s.field), data.value(b, s.field));
                if (c != 0) {
                    return s.descending ? c > 0 : c < 0;
                }
            }
            return false;
        });
    }
    return order;
}

QList<int> recipientOrder(const MergeData &data, const MailMergeSettings &settings)
{
    QList<int> order = filteredOrder(data, settings);
    order.removeIf([&settings](int record) { return settings.excluded.contains(record); });
    return order;
}

QList<QList<int>> duplicateGroups(const MergeData &data)
{
    QHash<QString, QList<int>> byKey;
    QStringList keys;
    for (int i = 0; i < data.records.size(); ++i) {
        QStringList parts;
        for (const QString &value : data.records.at(i)) {
            parts.append(value.trimmed().toCaseFolded());
        }
        const QString key = parts.join(QChar(0x1F));
        if (!byKey.contains(key)) {
            keys.append(key);
        }
        byKey[key].append(i);
    }
    QList<QList<int>> groups;
    for (const QString &key : keys) {
        if (byKey.value(key).size() > 1) {
            groups.append(byKey.value(key));
        }
    }
    return groups;
}

} // namespace QindaOffice::Docx
