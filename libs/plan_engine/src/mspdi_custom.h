// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to mspdi_*.cpp (M83, D-092): task custom fields in Microsoft
// Project XML — the project's definitions (<ExtendedAttributes>) and each
// task's values (<ExtendedAttribute>, FieldID + Value) — and the elements
// M83 models that an M63–M82 .qplan kept verbatim.
//
// AGENT-CONTRACT (byte-faithful, as baselines since M79): every definition
// and every task value is read into the model AND kept as written, in
// source order — ProjectInfo::extra "mspdiExtendedAttributes" and Task::extra
// "mspdiCustom", [{id: FieldID, xml}] — so a resource's or an unknown
// field's travel untouched among them. The writer replays a kept element
// while the model still says what it says; a value or definition changed
// here is written from the model (a definition keeps its unmodelled
// children — Guid, SecondaryPID … — in the schema's order), a cleared value
// is dropped, and a new one is added after the kept ones. A formula field's
// value is what the formula gives here (the file's goes back while equal;
// one QindaPlan cannot evaluate keeps the file's). A custom field with
// values but no definition gets a bare one (FieldID, FieldName), as
// Project writes for every field in use.

#include "mspdi_keep.h"
#include "mspdi_p.h"

#include <QXmlStreamWriter>

namespace QindaOffice::Plan::Mspdi {

inline const QString kCustomValuesKey = QStringLiteral("mspdiCustom");
inline const QString kCustomDefsKey = QStringLiteral("mspdiExtendedAttributes");

// One task <ExtendedAttribute>: its FieldID and its value in the spelling
// custom_fields.h stores (a Duration as "PT…|<DurationFormat>").
struct CustomValueXml {
    qint64 fieldId = 0;
    QString wire;
};
[[nodiscard]] CustomValueXml parseCustomValue(const QString &fragment);
// One project <ExtendedAttribute> definition as the model reads it
// (nullopt for a field that is not a task custom field) and its FieldID.
[[nodiscard]] std::optional<CustomFieldDef> parseCustomDefinition(const QString &fragment, qint64 *fieldId);

// The reader: a task's value fragment / the project's definitions element.
void readCustomValue(Task &task, const QString &fragment);
void readCustomDefinitions(const QString &extendedAttributes, ProjectInfo &info, QList<CustomFieldDef> *defs);

// The writer (mspdi_writer.cpp) at their places in Project's order.
void writeCustomValues(Children &c, QXmlStreamWriter &xml, const Project &project, int taskIndex);
void writeCustomDefinitions(Children &c, QXmlStreamWriter &xml, const Project &project);

// What an M63–M82 .qplan kept verbatim and M83 models: the definitions and
// task values above, task Type / EffortDriven / Recurring and the project's
// DefaultTaskType / NewTasksEffortDriven / EditableActualCosts. Returns how
// many elements it lifted (mspdi_adopt.cpp calls it).
int adoptM83Elements(Project &project);

} // namespace QindaOffice::Plan::Mspdi
