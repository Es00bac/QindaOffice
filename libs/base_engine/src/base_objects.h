// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Saved objects (M49, D-049 §1): queries, forms and reports stored as JSON
// in qb_objects. Every save, rename and delete is a captured Change (one
// undo step). Definitions are whole maps: unknown members a newer version
// wrote survive a load–modify–save round trip because callers merge into
// the loaded map instead of rebuilding it.
//
// AGENT-CONTRACT (definition shapes, read by the app and the templates):
//  query  QueryDesign::toMap() + {"description"}
//  form   {"version":1, "source":{"kind":"table","name":…}, "title":…,
//          "fields":[{"name","label","hidden"}], "design": <QindaTK
//          local-data-design map, once the designer has saved it>}
//  report {"version":1, "source":{"kind":"table"|"query","name":…},
//          "title":…, "columns":[{"field","label"}], "groups":[{"field",
//          "label","pageBreakBefore","totals":[{"field","function"}]}],
//          "totals":[…], "detail":bool, "footer":…, "orientation":…,
//          "design": <QindaTK design map, once saved>}

#include "base_file.h"

#include <QVariantMap>

#include <optional>

namespace QindaOffice::Base {

struct SavedObject {
    qint64 id = 0;
    QString kind; // query | form | report
    QString name;
    QVariantMap definition;
    QString description;
    QString created;
    QString modified;
    [[nodiscard]] QVariantMap toMap() const;
};

class ObjectStore {
public:
    [[nodiscard]] static bool validKind(const QString &kind);
    [[nodiscard]] static QList<SavedObject> list(const BaseFile &file, const QString &kind = {});
    [[nodiscard]] static std::optional<SavedObject> load(const BaseFile &file, const QString &kind, const QString &name);
    // Creates or replaces (when `replace`); refuses a clash otherwise.
    static Result save(BaseFile &file, const QString &kind, const QString &name, const QVariantMap &definition,
                       bool replace, Change *change);
    static Result rename(BaseFile &file, const QString &kind, const QString &from, const QString &to, Change *change);
    static Result remove(BaseFile &file, const QString &kind, const QString &name, Change *change);
    // A name not used yet: "Donors Form", "Donors Form 2", …
    [[nodiscard]] static QString uniqueName(const BaseFile &file, const QString &kind, const QString &base);
    // Objects whose definition names `table` (forms/reports by source,
    // queries by table list or SQL text).
    [[nodiscard]] static QStringList referencesTo(const BaseFile &file, const QString &table);
    // Statements rewriting table references after a table rename (appended
    // to that schema plan so both change in one step).
    [[nodiscard]] static QStringList tableRenameStatements(const BaseFile &file, const QString &from, const QString &to);
};

} // namespace QindaOffice::Base
