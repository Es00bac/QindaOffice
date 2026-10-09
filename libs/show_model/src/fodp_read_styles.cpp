// SPDX-License-Identifier: GPL-3.0-or-later
#include "fodp_read_styles.h"

namespace QindaOffice::Show::Fodp {

namespace {

QString attrKey(QStringView ns, QStringView local)
{
    return QLatin1Char('{') + ns.toString() + QLatin1Char('}') + local.toString();
}

} // namespace

StyleSheet::StyleSheet(const Odf::OdfNode &document)
{
    using namespace Odf;
    collect(document.child(kOffice, u"styles"));
    collect(document.child(kOffice, u"automatic-styles"));
    if (const OdfNode *masters = document.child(kOffice, u"master-styles")) {
        for (const OdfNode *page : masters->elements(kStyle, u"master-page")) {
            m_masterPages.insert(page->attr(kStyle, u"name"), page);
            m_masterOrder.append(page);
        }
    }
}

void StyleSheet::collect(const Odf::OdfNode *container)
{
    using namespace Odf;
    if (container == nullptr) {
        return;
    }
    for (const OdfNode *node : container->elements()) {
        const bool isDefault = node->is(kStyle, u"default-style");
        if (node->is(kStyle, u"style") || isDefault) {
            Style style;
            style.family = node->attr(kStyle, u"family");
            style.parent = node->attr(kStyle, u"parent-style-name");
            for (const OdfNode *properties : node->elements()) {
                QHash<QString, QString> &target = style.properties[properties->name];
                for (auto it = properties->attributes.cbegin(); it != properties->attributes.cend(); ++it) {
                    target.insert(it.key(), it.value());
                }
            }
            if (isDefault) {
                m_defaults.insert(style.family, style);
            } else {
                m_styles.insert(style.family + QLatin1Char('/') + node->attr(kStyle, u"name"), style);
            }
        } else if (node->is(kText, u"list-style")) {
            m_listStyles.insert(node->attr(kStyle, u"name"), node);
        } else if (node->is(kStyle, u"page-layout")) {
            if (const OdfNode *props = node->child(kStyle, u"page-layout-properties")) {
                m_pageLayouts.insert(node->attr(kStyle, u"name"),
                                     QSizeF(lengthToPt(props->attr(kFo, u"page-width")),
                                            lengthToPt(props->attr(kFo, u"page-height"))));
            }
        }
    }
}

bool StyleSheet::hasStyle(const QString &styleName, const QString &family) const
{
    return m_styles.contains(family + QLatin1Char('/') + styleName);
}

QString StyleSheet::property(const QString &styleName, const QString &family, QStringView element, QStringView ns,
                             QStringView local) const
{
    const QString key = attrKey(ns, local);
    const QString elementName = element.toString();
    QString name = styleName;
    // Guard against a parent cycle in a hostile file.
    for (int depth = 0; depth < 32 && !name.isEmpty(); ++depth) {
        const auto found = m_styles.constFind(family + QLatin1Char('/') + name);
        if (found == m_styles.constEnd()) {
            break;
        }
        const auto props = found->properties.constFind(elementName);
        if (props != found->properties.constEnd()) {
            const auto value = props->constFind(key);
            if (value != props->constEnd()) {
                return value.value();
            }
        }
        name = found->parent;
    }
    const auto fallback = m_defaults.constFind(family);
    if (fallback != m_defaults.constEnd()) {
        const auto props = fallback->properties.constFind(elementName);
        if (props != fallback->properties.constEnd()) {
            return props->value(key);
        }
    }
    return {};
}

QString StyleSheet::listLevelKind(const QString &listStyle, int level) const
{
    using namespace Odf;
    const OdfNode *style = m_listStyles.value(listStyle);
    if (style == nullptr) {
        return QStringLiteral("bullet"); // an unknown list style still lists
    }
    for (const OdfNode *entry : style->elements()) {
        if (entry->attr(kText, u"level").toInt() != level) {
            continue;
        }
        if (entry->is(kText, u"list-level-style-bullet")) {
            return entry->attr(kText, u"bullet-char").isEmpty() ? QStringLiteral("none") : QStringLiteral("bullet");
        }
        if (entry->is(kText, u"list-level-style-number")) {
            return entry->attr(kStyle, u"num-format").isEmpty() ? QStringLiteral("none") : QStringLiteral("number");
        }
        if (entry->is(kText, u"list-level-style-image")) {
            return QStringLiteral("image");
        }
    }
    return QStringLiteral("none");
}

QSizeF StyleSheet::masterPageSize(const QString &masterPage) const
{
    const Odf::OdfNode *page = m_masterPages.value(masterPage);
    if (page == nullptr && !m_masterOrder.isEmpty()) {
        page = m_masterOrder.first();
    }
    if (page == nullptr) {
        return {};
    }
    return m_pageLayouts.value(page->attr(Odf::kStyle, u"page-layout-name"));
}

} // namespace QindaOffice::Show::Fodp
