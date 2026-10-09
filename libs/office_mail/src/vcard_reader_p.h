// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the vCard reader (M64): the content-line model, the per-card
// state and the Reader, split over vcard_reader.cpp (lines, unfolding,
// cards) and vcard_reader_properties.cpp (property → Contact mapping).
// Not part of the library's public surface; see vcard.h.

#include "vcard.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>

#include <optional>

namespace QindaOffice::Mail::VCard::Detail {

using Params = QList<QPair<QString, QString>>;

struct ContentLine {
    QString group;
    QString name;       // as written
    QString key;        // upper-case name
    Params params;      // 2.1 bare parameters normalized to TYPE= / ENCODING=
    QByteArray value;   // raw bytes after unfolding
};

enum class ValueKind { Email, Phone, Url, Address };
enum class LineKind { Plain, QuotedPrintable, Base64 };
enum class Continuation { None, Fold, SoftBreak, Base64 };

// The first value of a parameter (case-insensitive name), or "".
QString paramValue(const Params &params, QLatin1StringView name);
// `[group.]NAME;param=value;param="quoted,value";BARE:value` (RFC 6350 §3.3);
// nullopt when the line is not a property.
std::optional<ContentLine> parseContentLine(const QByteArray &line);
// The transfer encoding named by a line's parameters.
LineKind lineKind(QByteArrayView line);
bool isBase64Line(QByteArrayView line);
// TYPE tokens without the noise (pref/voice/internet/…), and whether the
// value is preferred (3.0 TYPE=pref, 2.1 PREF, 4.0 PREF=1).
QPair<QString, bool> readTypes(const Params &params, ValueKind kind);
// Apple's itemN.X-ABLabel: a built-in "_$!<Mobile>!$_" label as a token,
// anything else verbatim.
QString appleLabelType(const QString &label);

struct GroupedValue {
    ValueKind kind;
    qsizetype index;
    QString group; // upper-case
};

struct PendingLabel {
    qsizetype extraIndex;
    QString type;
    QString text;
};

struct Card {
    Contact contact;
    QString version = QStringLiteral("3.0");
    bool sawVersion = false;
    int number = 0;
    int line = 0;
    QSet<QString> mapped;                     // single-valued keys already mapped
    QList<GroupedValue> grouped;              // values that carry an itemN group
    QList<QPair<QString, qsizetype>> labels;  // X-ABLabel: (upper group, index in extra)
    QList<PendingLabel> pendingLabels;        // LABELs kept in extra until an ADR claims them
    qsizetype lastAddress = -1;               // the ADR a directly following LABEL belongs to
    bool previousWasEmptyAgent = false;
    bool warnedValues = false;
    bool warnedExtras = false;
};

class Reader {
public:
    explicit Reader(ReadResult &result) : m_result(result) {}
    void run(const QByteArray &input);

private:
    QByteArray toUtf8(const QByteArray &input);
    void feed(QByteArrayView line);
    Continuation continuation(QByteArrayView line) const;
    void flush();
    void handleLine(const QByteArray &line);
    void finishCard(bool terminated);
    void apply(const ContentLine &cl);
    [[nodiscard]] QString text(const ContentLine &cl) const;
    void addValue(ValueKind kind, const ContentLine &cl, const QString &value);
    void addAddress(const ContentLine &cl);
    void addLabel(const ContentLine &cl);
    bool addDate(const ContentLine &cl, QString &field);
    bool addPhoto(const ContentLine &cl);
    void addExtra(const ContentLine &cl);
    void tooMany();
    void warn(const QString &message);
    [[nodiscard]] bool v21() const { return m_card.version == QLatin1StringView("2.1"); }

    ReadResult &m_result;
    bool m_unicodeFile = false;
    int m_lineNo = 0;
    QByteArray m_current;           // the logical line being assembled
    bool m_haveCurrent = false;
    bool m_oversized = false;
    int m_currentLine = 0;
    LineKind m_currentKind = LineKind::Plain;
    bool m_inCard = false;
    int m_nested = 0;               // depth inside an embedded (AGENT) vCard
    int m_cards = 0;
    Card m_card;
    int m_droppedWarnings = 0;
    bool m_warnedFull = false;
};

} // namespace QindaOffice::Mail::VCard::Detail
