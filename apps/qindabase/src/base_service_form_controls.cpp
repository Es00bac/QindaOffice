// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService (M80, D-089 §4): a lookup field's choices and the linked
// datasheets a form's subforms show. Since M89 (D-097) the controls
// themselves are QindaTK's positioned controls in the form's design
// (base_service_form_layout.cpp, base_form_layout.h).
#include "base_document.h"
#include "base_form_controls.h"
#include "base_objects.h"
#include "base_records.h"
#include "base_service.h"
#include "design_files.h"

#include <QSet>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

bool isLookup(const Engine::ColumnInfo &c)
{
    // M86: a value list is a lookup too (its own choices).
    return (c.lookup.isSet() && !c.lookup.displayField.isEmpty()) || c.lookup.isValueList();
}

} // namespace

QVariantList BaseService::lookupChoices(const QString &table, const QString &field) const
{
    QVariantList out;
    if (file() == nullptr) {
        return out;
    }
    for (const Engine::ColumnInfo &c : Engine::Records::columns(*file(), table)) {
        if (c.name.compare(field, Qt::CaseInsensitive) == 0 && isLookup(c)) {
            for (const auto &[value, text] : Engine::Records::lookupChoices(*file(), c.lookup, 20000, c.type)) {
                out.append(QVariantMap{{s("value"), value}, {s("text"), text}});
            }
        }
    }
    return out;
}

DatasheetModel *BaseService::subformSheet(int index)
{
    if (index < 0 || index >= Engine::kMaxSubforms) {
        return nullptr;
    }
    while (m_subforms.size() <= index) {
        auto *sheet = new DatasheetModel(this);
        sheet->setDocument(m_document);
        connect(sheet, &DatasheetModel::messageRequested, this, &BaseService::notify);
        m_subforms.append(sheet);
    }
    return m_subforms.at(index);
}

} // namespace QindaOffice::Apps::Base
