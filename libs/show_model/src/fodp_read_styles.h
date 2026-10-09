// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The .fodp reader's view of a document's styles: office:styles and
// office:automatic-styles (graphic, presentation, paragraph, text and
// drawing-page families, the family default styles, list styles, page
// layouts, master pages), with every property lookup walking
// style:parent-style-name up to the family default — the way LibreOffice
// resolves them, so its gr*/pr*/P*/T*/dp* styles read like ours.

#include "odf_dom.h"

#include <QHash>
#include <QString>

namespace QindaOffice::Show::Fodp {

class StyleSheet {
public:
    explicit StyleSheet(const Odf::OdfNode &document);

    // The value of `ns:local` on `element` ("graphic-properties", …) of
    // the named style or its ancestors; empty when nobody sets it.
    [[nodiscard]] QString property(const QString &styleName, const QString &family, QStringView element,
                                   QStringView ns, QStringView local) const;
    [[nodiscard]] bool hasStyle(const QString &styleName, const QString &family) const;

    // A list style's level (1-based) as "bullet" / "number" / "image" /
    // "none" (no label).
    [[nodiscard]] QString listLevelKind(const QString &listStyle, int level) const;

    // Page size (points) of the page layout a master page uses.
    [[nodiscard]] QSizeF masterPageSize(const QString &masterPage) const;
    [[nodiscard]] const Odf::OdfNode *masterPage(const QString &name) const { return m_masterPages.value(name); }
    [[nodiscard]] QList<const Odf::OdfNode *> masterPages() const { return m_masterOrder; }

private:
    struct Style {
        QString parent;
        QString family;
        // element local name → attribute key ("{ns}local") → value
        QHash<QString, QHash<QString, QString>> properties;
    };
    void collect(const Odf::OdfNode *container);

    QHash<QString, Style> m_styles;         // family + '/' + name → style
    QHash<QString, Style> m_defaults;       // family → default style
    QHash<QString, const Odf::OdfNode *> m_listStyles;
    QHash<QString, QSizeF> m_pageLayouts;
    QHash<QString, const Odf::OdfNode *> m_masterPages;
    QList<const Odf::OdfNode *> m_masterOrder;
};

} // namespace QindaOffice::Show::Fodp
