// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to office_ooxml's XML layer (ooxml_xml.cpp parses,
// ooxml_xml_write.cpp serialises): what both halves must agree on.

#include "ooxml_namespaces.h"

#include <QString>
#include <QStringView>

namespace QindaOffice::Ooxml {

// AGENT-CONTRACT (parser ⇄ serialiser): attributes whose VALUES name
// namespaces by prefix (mc:Ignorable, ProcessContent, MustUnderstand) are
// read as "{uri}" tokens and written back through the output's prefixes;
// every other attribute value is data (see ooxml_xml.h).
inline bool isPrefixListAttribute(const QString &ns, const QString &name)
{
    if (ns == QStringView(Ns::MC)) {
        return name == u"Ignorable" || name == u"ProcessContent" || name == u"MustUnderstand";
    }
    return false;
}

} // namespace QindaOffice::Ooxml
