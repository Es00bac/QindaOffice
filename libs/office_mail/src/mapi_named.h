// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Named properties (M76, D-085): Outlook keeps most contact, calendar and
// task fields under (property set GUID, number or name) pairs and maps each
// pair to a per-file property id 0x8000 + n. The map lives in three streams
// that .msg (MS-OXMSG 2.2.3, the "__nameid_version1.0" storage) and .pst
// (MS-PST 2.4.7, the name-to-id map node) share byte for byte: GUIDs, NAMEID
// entries and names.
//
// AGENT-GUARD: the streams are untrusted. Every entry is bounds-checked; an
// entry with an unknown GUID index or a name that runs past its stream is
// skipped, never read out of bounds.

#include <QByteArray>
#include <QHash>
#include <QPair>
#include <QString>
#include <QUuid>

#include <optional>

namespace QindaOffice::Mail::Mapi {

// MS-OXPROPS 1.3.2 property sets.
inline const QUuid kPsMapi{0x00020328, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
inline const QUuid kPsPublicStrings{0x00020329, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
inline const QUuid kPsetidAppointment{0x00062002, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
inline const QUuid kPsetidTask{0x00062003, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
inline const QUuid kPsetidAddress{0x00062004, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
inline const QUuid kPsetidCommon{0x00062008, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46};
inline const QUuid kPsetidMeeting{0x6ED8DA90, 0x450B, 0x101B, 0x98, 0xDA, 0x00, 0xAA, 0x00, 0x3F, 0x13, 0x05};

class NamedPropertyMap {
public:
    static constexpr int kMaxEntries = 0x7FFE;

    void parse(const QByteArray &guids, const QByteArray &entries, const QByteArray &strings);
    [[nodiscard]] std::optional<quint16> find(const QUuid &set, quint32 lid) const;
    [[nodiscard]] std::optional<quint16> find(const QUuid &set, const QString &name) const;
    [[nodiscard]] int size() const { return int(m_numeric.size() + m_names.size()); }

private:
    QHash<QPair<QUuid, quint32>, quint16> m_numeric;
    QHash<QPair<QUuid, QString>, quint16> m_names;
};

// A GUID as MAPI structures store it (Data1–3 little-endian), and back.
[[nodiscard]] QUuid guidFromBytes(const QByteArray &bytes, qsizetype at);
[[nodiscard]] QByteArray guidBytes(const QUuid &guid);

} // namespace QindaOffice::Mail::Mapi
