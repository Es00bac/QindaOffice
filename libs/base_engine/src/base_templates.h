// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Starter templates (M49, D-049 §9): Contacts, Donors & Donations,
// Volunteers & Shifts, Inventory, Projects & Tasks — each a set of tables
// with relationships and lookups, saved queries, a form and a report, for
// the records a community centre or a small studio keeps. Templates carry
// no sample records.

#include "base_file.h"

#include <QVariantMap>

namespace QindaOffice::Base {

struct TemplateInfo {
    QString id;
    QString name;
    QString description;
    QString iconName; // a QindaTK Lucide name
    [[nodiscard]] QVariantMap toMap() const;
};

[[nodiscard]] QList<TemplateInfo> templates();
// Creates the template's tables and objects in an (empty) database.
Result applyTemplate(BaseFile &file, const QString &id);

} // namespace QindaOffice::Base
