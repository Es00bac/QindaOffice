// SPDX-License-Identifier: GPL-3.0-or-later
// Project's task custom fields: the catalog — the 120 fields, their kinds
// and Project's FieldIDs (custom_fields.h).
#include "custom_fields.h"

namespace QindaOffice::Plan {

namespace {

// AGENT-NOTE (source of truth): FieldIDs as Microsoft Project 2019 writes
// them in ExtendedAttributes (MPXJ's task-*-project2019-mspdi.xml files:
// Text1 = 188743731 …); never renumber.
constexpr qint64 kText[30] = {188743731, 188743734, 188743737, 188743740, 188743743, 188743746, 188743747, 188743748,
                              188743749, 188743750, 188743997, 188743998, 188743999, 188744000, 188744001, 188744002,
                              188744003, 188744004, 188744005, 188744006, 188744007, 188744008, 188744009, 188744010,
                              188744011, 188744012, 188744013, 188744014, 188744015, 188744016};
constexpr qint64 kNumber[20] = {188743767, 188743768, 188743769, 188743770, 188743771, 188743982, 188743983,
                                188743984, 188743985, 188743986, 188743987, 188743988, 188743989, 188743990,
                                188743991, 188743992, 188743993, 188743994, 188743995, 188743996};
constexpr qint64 kDate[10] = {188743945, 188743946, 188743947, 188743948, 188743949,
                              188743950, 188743951, 188743952, 188743953, 188743954};
constexpr qint64 kFlag[20] = {188743752, 188743753, 188743754, 188743755, 188743756, 188743757, 188743758,
                              188743759, 188743760, 188743761, 188743972, 188743973, 188743974, 188743975,
                              188743976, 188743977, 188743978, 188743979, 188743980, 188743981};
constexpr qint64 kCost[10] = {188743786, 188743787, 188743788, 188743938, 188743939,
                              188743940, 188743941, 188743942, 188743943, 188743944};
constexpr qint64 kDuration[10] = {188743783, 188743784, 188743785, 188743955, 188743956,
                                  188743957, 188743958, 188743959, 188743960, 188743961};
constexpr qint64 kStart[10] = {188743732, 188743735, 188743738, 188743741, 188743744,
                               188743962, 188743964, 188743966, 188743968, 188743970};
constexpr qint64 kFinish[10] = {188743733, 188743736, 188743739, 188743742, 188743745,
                                188743963, 188743965, 188743967, 188743969, 188743971};

template<std::size_t N>
void addFamily(QList<CustomFieldInfo> &out, CustomFieldKind kind, const qint64 (&ids)[N])
{
    for (std::size_t i = 0; i < N; ++i) {
        out.append({customFieldKindName(kind) + QString::number(int(i) + 1), kind, int(i) + 1, ids[i]});
    }
}

} // namespace

const QList<CustomFieldInfo> &customFieldCatalog()
{
    static const QList<CustomFieldInfo> catalog = [] {
        QList<CustomFieldInfo> out;
        addFamily(out, CustomFieldKind::Text, kText);
        addFamily(out, CustomFieldKind::Number, kNumber);
        addFamily(out, CustomFieldKind::Date, kDate);
        addFamily(out, CustomFieldKind::Flag, kFlag);
        addFamily(out, CustomFieldKind::Cost, kCost);
        addFamily(out, CustomFieldKind::Duration, kDuration);
        addFamily(out, CustomFieldKind::Start, kStart);
        addFamily(out, CustomFieldKind::Finish, kFinish);
        return out;
    }();
    return catalog;
}

std::optional<CustomFieldInfo> customFieldInfo(const QString &field)
{
    for (const CustomFieldInfo &info : customFieldCatalog()) {
        if (info.field.compare(field, Qt::CaseInsensitive) == 0) {
            return info;
        }
    }
    return std::nullopt;
}

std::optional<CustomFieldInfo> customFieldByFieldId(qint64 fieldId)
{
    for (const CustomFieldInfo &info : customFieldCatalog()) {
        if (info.fieldId == fieldId) {
            return info;
        }
    }
    return std::nullopt;
}

bool isCustomField(const QString &field)
{
    // Exact names only: the sheet's own fields are lower camel case.
    for (const CustomFieldInfo &info : customFieldCatalog()) {
        if (info.field == field) {
            return true;
        }
    }
    return false;
}

QString customFieldKindName(CustomFieldKind kind)
{
    switch (kind) {
    case CustomFieldKind::Text: return QStringLiteral("Text");
    case CustomFieldKind::Number: return QStringLiteral("Number");
    case CustomFieldKind::Date: return QStringLiteral("Date");
    case CustomFieldKind::Flag: return QStringLiteral("Flag");
    case CustomFieldKind::Cost: return QStringLiteral("Cost");
    case CustomFieldKind::Duration: return QStringLiteral("Duration");
    case CustomFieldKind::Start: return QStringLiteral("Start");
    case CustomFieldKind::Finish: return QStringLiteral("Finish");
    }
    return QStringLiteral("Text");
}

std::optional<CustomFieldKind> customFieldKindFromName(const QString &name)
{
    for (int k = 0; k <= int(CustomFieldKind::Finish); ++k) {
        if (customFieldKindName(CustomFieldKind(k)).compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
            return CustomFieldKind(k);
        }
    }
    return std::nullopt;
}

QStringList customFieldsOfKind(CustomFieldKind kind)
{
    QStringList out;
    for (const CustomFieldInfo &info : customFieldCatalog()) {
        if (info.kind == kind) {
            out.append(info.field);
        }
    }
    return out;
}

} // namespace QindaOffice::Plan
