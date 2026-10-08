// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to mspdi_*.cpp (M63, D-071): what QindaPlan does not model in a
// Microsoft Project XML file is KEPT, verbatim, and written back in the
// order Project writes it — custom fields (ExtendedAttribute, and the
// project's ExtendedAttributes / OutlineCodes definitions), task type,
// hyperlinks, cost rate tables, baselines 3–10, recurring calendar
// exceptions, views and tables, and every element a later Project adds.
//
// AGENT-CONTRACT (storage): each object's kept children are a JSON array in
// its `extra` under "mspdi" — [{name, after, xml, derived}] — so they also
// survive a .qplan save (unknown keys round-trip there, plan_json.cpp). The
// project summary task's are in ProjectInfo::extra "mspdiSummaryTask"; a
// calendar's unsupported exceptions in its extra "mspdiExceptions" (the
// fragments, re-emitted inside <Exceptions>).
//
// AGENT-CONTRACT (derived values, isDerived): what Project computes from the
// dates, work and progress QindaPlan edits (timephased data, actual and
// remaining work, variances, …) is kept with a STAMP — the object's UID,
// dates, duration/work and progress as read, in the writer's spelling
// ("mspdiStamp" in `extra`, {element: text}). The writer emits those
// elements only while the object writes the same values, so a task the user
// rescheduled never carries stale timephased data, and a pasted copy (new
// UID) neither. A resource's cost rate tables (Rates) depend on its rate
// alone: they travel while UID, StandardRate and CostPerUse are as read.

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <functional>

namespace QindaOffice::Plan::Mspdi {

enum class Kind { Project, Calendar, Task, Resource, Assignment };

inline const QString kKeptKey = QStringLiteral("mspdi");
inline const QString kStampKey = QStringLiteral("mspdiStamp");
inline const QString kSummaryKey = QStringLiteral("mspdiSummaryTask");
inline const QString kSummaryStampKey = QStringLiteral("mspdiSummaryStamp");
inline const QString kExceptionsKey = QStringLiteral("mspdiExceptions");

// Position of `element` among the children of `kind` in the order
// Microsoft Project writes them (mspdi_order.cpp); -1 when not listed.
[[nodiscard]] int schemaRank(Kind kind, const QString &element);
// Values Project derives from what QindaPlan edits (see above).
[[nodiscard]] bool isDerived(Kind kind, const QString &element);

struct Kept {
    QString name;  // local element name
    QString after; // the element before it in the source (orders unknown names)
    QString xml;   // the element, a self-contained fragment
    bool derived = false;
};

[[nodiscard]] QJsonArray keptToJson(const QList<Kept> &kept);
[[nodiscard]] QList<Kept> keptFromJson(const QJsonValue &value);

// The current element of `in` (positioned on its StartElement) and its
// subtree as a fragment; `in` ends on the element's EndElement, as after
// skipCurrentElement(). Whitespace between elements is dropped (the writer
// indents); text, CDATA, comments and foreign namespaces are kept.
[[nodiscard]] QString capture(QXmlStreamReader &in);
// Writes a fragment's tokens into `out` at its current position.
void replay(QXmlStreamWriter &out, const QString &fragment);

// The stamp's spelling of one modelled value, so the reader (the file's
// text) and the writer (its own text) agree: dates and durations through
// the wire converters, numbers with two decimals.
[[nodiscard]] QString stampPart(const QString &element, const QString &text);
// The elements an object's stamp is made of, in order.
[[nodiscard]] const QStringList &stampElements(Kind kind);

// One object's children, collected from the model and the kept list and
// written in Project's order: modelled elements first among equals, kept
// ones after them in source order; an unknown kept element sits right
// after the element it followed in the file.
class Children {
public:
    Children(Kind kind, const QJsonObject &extra, const QString &stampKey = kStampKey,
             const QString &keptKey = kKeptKey);
    // A modelled element.
    void add(const QString &name, std::function<void()> write);
    void text(QXmlStreamWriter &out, const QString &name, const QString &value);
    // True when the file had this element (a default is then not written).
    [[nodiscard]] bool kept(const QString &name) const;
    void write(QXmlStreamWriter &out);

private:
    struct Slot {
        int rank = 0;
        int group = 0; // 0 modelled, 1 kept
        int seq = 0;
        std::function<void()> write;
    };
    [[nodiscard]] int rankOf(const QString &name, int depth = 0) const;

    Kind m_kind;
    // Whether the values `element` was derived from are as read.
    [[nodiscard]] bool stampHolds(const QString &element) const;

    QList<Kept> m_kept;
    QJsonObject m_stampRead;
    QHash<QString, QString> m_stampNow;
    QList<Slot> m_slots;
    int m_seq = 0;
};

} // namespace QindaOffice::Plan::Mspdi
