// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A minimal namespace-aware DOM for OpenDocument (flat ODP) input, plus the
// unit and transform arithmetic both fodp directions share.
//
// AGENT-NOTE: why not Ooxml::XmlElement? That tree drops whitespace-only
// text between child elements (right for OOXML, where it is indentation),
// but ODF paragraphs are MIXED content: `<text:span>A</text:span>
// <text:span>B</text:span>` carries a real space between the spans. OdfNode
// keeps every character node; the paragraph reader applies ODF's own
// white-space rules (ODF 1.3 part 3 §6.1.2) instead.
//
// AGENT-NOTE: units. ODF lengths always carry a unit. The QindaShow writer
// before M62 wrote unit-less numbers that meant CENTIMETRES (LibreOffice
// read them as 1/100 mm, piling every shape into a corner), so a bare
// number is read as cm — no conforming producer writes one.

#include <QHash>
#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QTransform>

#include <memory>

namespace QindaOffice::Show::Odf {

inline constexpr char16_t kOffice[] = u"urn:oasis:names:tc:opendocument:xmlns:office:1.0";
inline constexpr char16_t kStyle[] = u"urn:oasis:names:tc:opendocument:xmlns:style:1.0";
inline constexpr char16_t kText[] = u"urn:oasis:names:tc:opendocument:xmlns:text:1.0";
inline constexpr char16_t kDraw[] = u"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0";
inline constexpr char16_t kSvg[] = u"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0";
inline constexpr char16_t kFo[] = u"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0";
inline constexpr char16_t kPresentation[] = u"urn:oasis:names:tc:opendocument:xmlns:presentation:1.0";
inline constexpr char16_t kSmil[] = u"urn:oasis:names:tc:opendocument:xmlns:smil-compatible:1.0";
inline constexpr char16_t kXlink[] = u"http://www.w3.org/1999/xlink";
// The suite's extension namespace (fields ODF has no home for).
inline constexpr char16_t kQq[] = u"https://qindaqt.example.org/qqo";

struct OdfNode;
using OdfNodePtr = std::shared_ptr<OdfNode>;

struct OdfNode {
    QString ns;   // empty for a text node
    QString name; // empty for a text node
    QString text; // character data (text nodes only)
    QHash<QString, QString> attributes; // "{ns}local" → value
    QList<OdfNodePtr> children;

    [[nodiscard]] bool isText() const { return name.isEmpty(); }
    [[nodiscard]] bool is(QStringView nsUri, QStringView local) const { return name == local && ns == nsUri; }
    [[nodiscard]] QString attr(QStringView nsUri, QStringView local, const QString &fallback = {}) const;
    [[nodiscard]] bool hasAttr(QStringView nsUri, QStringView local) const;
    [[nodiscard]] const OdfNode *child(QStringView nsUri, QStringView local) const;
    [[nodiscard]] QList<const OdfNode *> elements() const;
    [[nodiscard]] QList<const OdfNode *> elements(QStringView nsUri, QStringView local) const;
};

// Parses a whole document; nullptr with `error` on malformed input.
[[nodiscard]] OdfNodePtr parseOdf(const QByteArray &data, QString *error);

// "2.5cm", "12pt", "1in", "10mm", "3pc", "96px" → points. A bare number is
// the pre-M62 QindaShow spelling of centimetres (see the AGENT-NOTE).
[[nodiscard]] qreal lengthToPt(QStringView value);
// Points → an ODF length in centimetres ("2.54cm"), the unit LibreOffice
// writes for drawings.
[[nodiscard]] QString ptToLength(qreal pt);

// ODF draw:transform ("rotate (a) translate (x y)", also scale/skewX/
// matrix) as a QTransform in points. ODF rotate() turns COUNTER-clockwise
// on the page (verified against LibreOffice 25.2: rotate(π/6) becomes
// PowerPoint rot 330°).
[[nodiscard]] QTransform parseTransform(QStringView value, bool *ok = nullptr);

// The rotate+translate pair that places a w×h box rotated `clockwiseDegrees`
// about its own centre at `rect` (rect = the unrotated box, as the model
// stores it).
[[nodiscard]] QString rotationTransform(const QRectF &rect, qreal clockwiseDegrees);

} // namespace QindaOffice::Show::Odf
