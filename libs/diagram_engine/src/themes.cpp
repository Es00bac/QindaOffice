// SPDX-License-Identifier: GPL-3.0-or-later
// Diagram theme palettes. Colour literals here are document content (the
// author's diagram colours), not chrome (AGENTS.md rule 3).
#include "themes.h"

#include <QCoreApplication>

#include <algorithm>

namespace QindaOffice::Diagram {

namespace {

QColor c(const char *hex)
{
    return QColor(QString::fromLatin1(hex));
}

struct Palette {
    const char *id;
    const char *name;
    // fill/line per role: Primary, Decision, Terminal, Data, Neutral,
    // Accent, Container, Note.
    const char *pairs[8][2];
    const char *connector;
    const char *text;
    const char *font;
};

constexpr Palette kPalettes[] = {
    {"qinda", QT_TRANSLATE_NOOP("Themes", "Qinda"),
     {{"#e3eefc", "#2f6fb2"}, {"#fdf1d6", "#b7791f"}, {"#ddf3e6", "#2f855a"}, {"#ece6fb", "#6b46c1"},
      {"#eef2f6", "#64748b"}, {"#fde4e2", "#c0392b"}, {"#f7f9fc", "#94a3b8"}, {"#fff6c7", "#c9a227"}},
     "#475569", "#1f2937", "Inter"},
    {"slate", QT_TRANSLATE_NOOP("Themes", "Slate"),
     {{"#e8ecf1", "#334155"}, {"#f1f5f9", "#475569"}, {"#e2e8f0", "#1e293b"}, {"#f8fafc", "#475569"},
      {"#f1f5f9", "#64748b"}, {"#ffe4e6", "#9f1239"}, {"#f8fafc", "#94a3b8"}, {"#fef9c3", "#a16207"}},
     "#334155", "#0f172a", "IBM Plex Sans"},
    {"forest", QT_TRANSLATE_NOOP("Themes", "Forest"),
     {{"#e3f2ea", "#276749"}, {"#fef3c7", "#b45309"}, {"#d1fae5", "#065f46"}, {"#e0f2fe", "#0369a1"},
      {"#eef2ee", "#4b5e4b"}, {"#fee2e2", "#b91c1c"}, {"#f4f8f4", "#7c9a7c"}, {"#fef9c3", "#a16207"}},
     "#3f4f44", "#1c2b22", "Lato"},
    {"sunset", QT_TRANSLATE_NOOP("Themes", "Sunset"),
     {{"#ffedd5", "#c2410c"}, {"#fef3c7", "#b45309"}, {"#fce7f3", "#be185d"}, {"#ede9fe", "#6d28d9"},
      {"#f5f0eb", "#78716c"}, {"#ffe4e6", "#e11d48"}, {"#fbf7f4", "#a8a29e"}, {"#fef9c3", "#a16207"}},
     "#57534e", "#292524", "Lato"},
    {"ocean", QT_TRANSLATE_NOOP("Themes", "Ocean"),
     {{"#e0f2fe", "#0369a1"}, {"#fef9c3", "#a16207"}, {"#ccfbf1", "#0f766e"}, {"#e0e7ff", "#4338ca"},
      {"#f1f5f9", "#475569"}, {"#ffe4e6", "#be123c"}, {"#f0f9ff", "#60a5fa"}, {"#fef9c3", "#a16207"}},
     "#0f4c75", "#0b2545", "IBM Plex Sans"},
    {"mono", QT_TRANSLATE_NOOP("Themes", "Print (black and white)"),
     {{"#ffffff", "#111827"}, {"#ffffff", "#111827"}, {"#ffffff", "#111827"}, {"#ffffff", "#111827"},
      {"#ffffff", "#4b5563"}, {"#ffffff", "#111827"}, {"#ffffff", "#6b7280"}, {"#f3f4f6", "#374151"}},
     "#111827", "#111827", "DejaVu Sans"},
};

} // namespace

const QVector<Theme> &themes()
{
    static const QVector<Theme> list = [] {
        QVector<Theme> out;
        for (const Palette &p : kPalettes) {
            Theme t;
            t.id = QString::fromLatin1(p.id);
            t.name = QCoreApplication::translate("Themes", p.name);
            for (const auto &pair : p.pairs) {
                t.roles.append({c(pair[0]), c(pair[1])});
            }
            t.connector = c(p.connector);
            t.text = c(p.text);
            t.font = QString::fromLatin1(p.font);
            out.append(t);
        }
        return out;
    }();
    return list;
}

const Theme &theme(const QString &id)
{
    for (const Theme &t : themes()) {
        if (t.id == id) {
            return t;
        }
    }
    return themes().first();
}

ColorPair roleColors(const Theme &theme, StyleRole role)
{
    const int index = int(role);
    if (role == StyleRole::Plain || index < 0 || index >= theme.roles.size()) {
        return {QColor(Qt::transparent), QColor(Qt::transparent)};
    }
    return theme.roles.at(index);
}

ShapeStyle shapeStyleFor(const Theme &theme, const Stencil *stencil)
{
    ShapeStyle style;
    const StyleRole role = stencil != nullptr ? stencil->role : StyleRole::Primary;
    const ColorPair pair = roleColors(theme, role);
    style.fill = pair.fill;
    style.line = pair.line;
    style.lineWidth = 1.5;
    if (role == StyleRole::Plain) {
        // Text and the actor draw no box; the actor's stick figure uses the
        // connector ink.
        style.fill = QColor(Qt::transparent);
        style.line = stencil != nullptr && stencil->id == QLatin1String("umlActor") ? theme.connector
                                                                                      : QColor(Qt::transparent);
    }
    if (stencil != nullptr && stencil->id == QLatin1String("orgVacancy")) {
        style.pattern = LinePattern::Dash;
    }
    if (stencil != nullptr && stencil->id == QLatin1String("phase")) {
        style.fill = QColor(Qt::transparent);
        style.pattern = LinePattern::Dash;
        style.lineWidth = 1;
    }
    if (stencil != nullptr && stencil->text == TextPlacement::Card) {
        style.fill = QColor(Qt::white);
        style.shadow = true;
    }
    return style;
}

TextStyle textStyleFor(const Theme &theme, const Stencil *stencil)
{
    TextStyle text;
    text.family = theme.font;
    text.color = theme.text;
    if (stencil == nullptr) {
        return text;
    }
    switch (stencil->text) {
    case TextPlacement::Header:
        text.bold = true;
        text.size = 11;
        text.align = stencil->id == QLatin1String("laneHorizontal") ? (Qt::AlignHCenter | Qt::AlignVCenter)
                                                                    : (Qt::AlignLeft | Qt::AlignVCenter);
        break;
    case TextPlacement::Card:
        text.size = 10.5;
        text.align = Qt::AlignLeft | Qt::AlignVCenter;
        break;
    case TextPlacement::Below:
        text.size = 10;
        text.align = Qt::AlignHCenter | Qt::AlignTop;
        break;
    case TextPlacement::Inside:
        break;
    }
    if (stencil->id == QLatin1String("text")) {
        text.size = 12;
    } else if (stencil->id == QLatin1String("note") || stencil->id == QLatin1String("umlClass")) {
        text.align = Qt::AlignLeft | Qt::AlignTop;
    }
    return text;
}

void styleConnector(const Theme &theme, Connector &connector)
{
    connector.line = theme.connector;
    connector.textStyle.family = theme.font;
    connector.textStyle.color = theme.text;
    connector.textStyle.size = 9.5;
}

ShapeStyle quickStyle(const Theme &theme, int variant, const ShapeStyle &base)
{
    ShapeStyle style = base;
    const int index = std::clamp(variant, 0, int(theme.roles.size()) - 1);
    style.fill = theme.roles.at(index).fill;
    style.line = theme.roles.at(index).line;
    style.gradient = QColor();
    return style;
}

bool applyTheme(Drawing &drawing, const QString &id)
{
    bool known = false;
    for (const Theme &t : themes()) {
        known = known || t.id == id;
    }
    if (!known) {
        return false;
    }
    const Theme &next = theme(id);
    for (Page &page : drawing.pages) {
        for (Shape &shape : page.shapes) {
            if (shape.type == QLatin1String("picture")) {
                continue;
            }
            const Stencil *stencil = Diagram::stencil(shape.type);
            const ShapeStyle fresh = shapeStyleFor(next, stencil);
            shape.style.fill = fresh.fill;
            shape.style.line = fresh.line;
            shape.style.gradient = QColor();
            shape.textStyle.color = next.text;
            shape.textStyle.family = next.font;
        }
        for (Connector &connector : page.connectors) {
            connector.line = next.connector;
            connector.textStyle.family = next.font;
            connector.textStyle.color = next.text;
        }
    }
    drawing.themeId = id;
    return true;
}

} // namespace QindaOffice::Diagram
