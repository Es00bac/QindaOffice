// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Document properties (ECMA-376 Part 2 §11 core properties, Part 1 §22.2
// extended properties): title, author, dates — what File ▸ Info shows in
// Office and what file managers index.
//
// readCoreProperties() never fails; a package without docProps/core.xml
// yields empty fields. writeProperties() adds docProps/core.xml and
// docProps/app.xml plus their package relationships.

#include "ooxml_package.h"

#include <QDateTime>
#include <QString>

namespace QindaOffice::Ooxml {

struct CoreProperties {
    QString title;
    QString subject;
    QString creator;
    QString keywords;
    QString description;
    QString lastModifiedBy;
    QString category;
    QString revision;
    QDateTime created;
    QDateTime modified;
};

[[nodiscard]] CoreProperties readCoreProperties(const Package &package);

// `application` is the producing app's name ("QindaWrite"); `pages` and
// `words` feed app.xml statistics when known (0 = omit).
void writeProperties(PackageWriter &writer, const CoreProperties &properties, const QString &application,
                     int pages = 0, int words = 0);

} // namespace QindaOffice::Ooxml
