// SPDX-License-Identifier: GPL-3.0-or-later
// Theme parts as PowerPoint writes them (show_theme.h): the colour scheme,
// the font scheme (the Office set with its script fonts), the three
// format schemes QindaShow offers as effect sets, and the theme family
// extension.

#include "show_theme_xml.h"

#include <QUuid>

namespace QindaOffice::Show {

namespace {

QString escaped(const QString &text)
{
    return text.toHtmlEscaped();
}

QString hex(const QColor &color)
{
    return (color.isValid() ? color : QColor(Qt::black)).name(QColor::HexRgb).mid(1).toUpper();
}

// The script fonts of Office's own font scheme (PowerPoint 2013–2022):
// script, heading, body.
struct ScriptFont {
    const char *script;
    const char *major;
    const char *minor;
};
const ScriptFont kOfficeScripts[] = {
    {"Jpan", "游ゴシック Light", "游ゴシック"},
    {"Hang", "맑은 고딕", "맑은 고딕"},
    {"Hans", "等线 Light", "等线"},
    {"Hant", "新細明體", "新細明體"},
    {"Arab", "Times New Roman", "Arial"},
    {"Hebr", "Times New Roman", "Arial"},
    {"Thai", "Angsana New", "Cordia New"},
    {"Ethi", "Nyala", "Nyala"},
    {"Beng", "Vrinda", "Vrinda"},
    {"Gujr", "Shruti", "Shruti"},
    {"Khmr", "MoolBoran", "DaunPenh"},
    {"Knda", "Tunga", "Tunga"},
    {"Guru", "Raavi", "Raavi"},
    {"Cans", "Euphemia", "Euphemia"},
    {"Cher", "Plantagenet Cherokee", "Plantagenet Cherokee"},
    {"Yiii", "Microsoft Yi Baiti", "Microsoft Yi Baiti"},
    {"Tibt", "Microsoft Himalaya", "Microsoft Himalaya"},
    {"Thaa", "MV Boli", "MV Boli"},
    {"Deva", "Mangal", "Mangal"},
    {"Telu", "Gautami", "Gautami"},
    {"Taml", "Latha", "Latha"},
    {"Syrc", "Estrangelo Edessa", "Estrangelo Edessa"},
    {"Orya", "Kalinga", "Kalinga"},
    {"Mlym", "Kartika", "Kartika"},
    {"Laoo", "DokChampa", "DokChampa"},
    {"Sinh", "Iskoola Pota", "Iskoola Pota"},
    {"Mong", "Mongolian Baiti", "Mongolian Baiti"},
    {"Viet", "Times New Roman", "Arial"},
    {"Uigh", "Microsoft Uighur", "Microsoft Uighur"},
    {"Geor", "Sylfaen", "Sylfaen"},
    {"Armn", "Arial", "Arial"},
    {"Bugi", "Leelawadee UI", "Leelawadee UI"},
    {"Bopo", "Microsoft JhengHei", "Microsoft JhengHei"},
    {"Java", "Javanese Text", "Javanese Text"},
    {"Lisu", "Segoe UI", "Segoe UI"},
    {"Mymr", "Myanmar Text", "Myanmar Text"},
    {"Nkoo", "Ebrima", "Ebrima"},
    {"Olck", "Nirmala UI", "Nirmala UI"},
    {"Osma", "Ebrima", "Ebrima"},
    {"Phag", "Phagspa", "Phagspa"},
    {"Syrn", "Estrangelo Edessa", "Estrangelo Edessa"},
    {"Syrj", "Estrangelo Edessa", "Estrangelo Edessa"},
    {"Syre", "Estrangelo Edessa", "Estrangelo Edessa"},
    {"Sora", "Nirmala UI", "Nirmala UI"},
    {"Tale", "Microsoft Tai Le", "Microsoft Tai Le"},
    {"Talu", "Microsoft New Tai Lue", "Microsoft New Tai Lue"},
    {"Tfng", "Ebrima", "Ebrima"},
};

// Office's latin pair carries PANOSE numbers, as PowerPoint writes them.
QString latin(const QString &typeface)
{
    if (typeface == u"Calibri Light") {
        return QStringLiteral(R"(<a:latin typeface="Calibri Light" panose="020F0302020204030204"/>)");
    }
    if (typeface == u"Calibri") {
        return QStringLiteral(R"(<a:latin typeface="Calibri" panose="020F0502020204030204"/>)");
    }
    return QStringLiteral(R"(<a:latin typeface="%1"/>)").arg(escaped(typeface));
}

QString fontElement(const char *name, const QString &typeface, bool office, bool major)
{
    QString out = QStringLiteral("<a:%1>").arg(QLatin1String(name)) + latin(typeface)
        + QStringLiteral(R"(<a:ea typeface=""/><a:cs typeface=""/>)");
    if (office) {
        for (const ScriptFont &font : kOfficeScripts) {
            out += QStringLiteral(R"(<a:font script="%1" typeface="%2"/>)")
                       .arg(QLatin1String(font.script), escaped(QString::fromUtf8(major ? font.major : font.minor)));
        }
    }
    return out + QStringLiteral("</a:%1>").arg(QLatin1String(name));
}

QString solid(const char *transforms = "")
{
    return QStringLiteral(R"(<a:solidFill><a:schemeClr val="phClr">%1</a:schemeClr></a:solidFill>)").arg(QLatin1String(transforms));
}

QString gradient(const char *a, const char *b, const char *c)
{
    return QStringLiteral(R"(<a:gradFill rotWithShape="1"><a:gsLst><a:gs pos="0"><a:schemeClr val="phClr">%1</a:schemeClr></a:gs>)"
                          R"(<a:gs pos="50000"><a:schemeClr val="phClr">%2</a:schemeClr></a:gs>)"
                          R"(<a:gs pos="100000"><a:schemeClr val="phClr">%3</a:schemeClr></a:gs></a:gsLst><a:lin ang="5400000" scaled="0"/></a:gradFill>)")
        .arg(QLatin1String(a), QLatin1String(b), QLatin1String(c));
}

QString lines(const char *w1, const char *w2, const char *w3)
{
    QString out;
    for (const char *w : {w1, w2, w3}) {
        out += QStringLiteral(R"(<a:ln w="%1" cap="flat" cmpd="sng" algn="ctr"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:prstDash val="solid"/><a:miter lim="800000"/></a:ln>)")
                   .arg(QLatin1String(w));
    }
    return out;
}

QString shadow(const char *blur, const char *distance, const char *alpha)
{
    return QStringLiteral(R"(<a:effectStyle><a:effectLst><a:outerShdw blurRad="%1" dist="%2" dir="5400000" algn="ctr" rotWithShape="0"><a:srgbClr val="000000"><a:alpha val="%3"/></a:srgbClr></a:outerShdw></a:effectLst></a:effectStyle>)")
        .arg(QLatin1String(blur), QLatin1String(distance), QLatin1String(alpha));
}

const QString kNoEffect = QStringLiteral("<a:effectStyle><a:effectLst/></a:effectStyle>");

QString uuidFor(const QString &name, const char *salt)
{
    static const QUuid ns(QStringLiteral("{2f0c6d8e-3b61-4c2a-9a7e-51f4d3c2b1a0}"));
    return QUuid::createUuidV5(ns, name + QLatin1String(salt)).toString(QUuid::WithBraces).toUpper();
}

} // namespace

QString formatSchemeXml(const QString &effects)
{
    if (effects == u"Flat") {
        return QStringLiteral(R"(<a:fmtScheme name="Flat"><a:fillStyleLst>)") + solid() + solid(R"(<a:tint val="60000"/>)")
            + solid(R"(<a:shade val="80000"/>)") + QStringLiteral("</a:fillStyleLst><a:lnStyleLst>")
            + lines("6350", "12700", "19050") + QStringLiteral("</a:lnStyleLst><a:effectStyleLst>") + kNoEffect + kNoEffect
            + kNoEffect + QStringLiteral("</a:effectStyleLst><a:bgFillStyleLst>") + solid() + solid(R"(<a:tint val="90000"/>)")
            + solid(R"(<a:shade val="85000"/>)") + QStringLiteral("</a:bgFillStyleLst></a:fmtScheme>");
    }
    if (effects == u"Soft Shadow") {
        return QStringLiteral(R"(<a:fmtScheme name="Soft Shadow"><a:fillStyleLst>)") + solid() + solid(R"(<a:tint val="70000"/>)")
            + gradient(R"(<a:tint val="80000"/>)", R"(<a:tint val="95000"/>)", R"(<a:shade val="90000"/>)")
            + QStringLiteral("</a:fillStyleLst><a:lnStyleLst>") + lines("9525", "15875", "25400")
            + QStringLiteral("</a:lnStyleLst><a:effectStyleLst>") + shadow("38100", "12700", "25000")
            + shadow("50800", "19050", "30000") + shadow("76200", "25400", "35000")
            + QStringLiteral("</a:effectStyleLst><a:bgFillStyleLst>") + solid() + solid(R"(<a:tint val="95000"/>)")
            + gradient(R"(<a:tint val="90000"/>)", R"(<a:tint val="97000"/>)", R"(<a:shade val="92000"/>)")
            + QStringLiteral("</a:bgFillStyleLst></a:fmtScheme>");
    }
    // Office (PowerPoint 2013–2022).
    return QStringLiteral(R"(<a:fmtScheme name="Office"><a:fillStyleLst>)") + solid()
        + gradient(R"(<a:lumMod val="110000"/><a:satMod val="105000"/><a:tint val="67000"/>)",
                   R"(<a:lumMod val="105000"/><a:satMod val="103000"/><a:tint val="73000"/>)",
                   R"(<a:lumMod val="105000"/><a:satMod val="109000"/><a:tint val="81000"/>)")
        + gradient(R"(<a:satMod val="103000"/><a:lumMod val="102000"/><a:tint val="94000"/>)",
                   R"(<a:satMod val="110000"/><a:lumMod val="100000"/><a:shade val="100000"/>)",
                   R"(<a:lumMod val="99000"/><a:satMod val="120000"/><a:shade val="78000"/>)")
        + QStringLiteral("</a:fillStyleLst><a:lnStyleLst>") + lines("6350", "12700", "19050")
        + QStringLiteral("</a:lnStyleLst><a:effectStyleLst>") + kNoEffect + kNoEffect + shadow("57150", "19050", "63000")
        + QStringLiteral("</a:effectStyleLst><a:bgFillStyleLst>") + solid()
        + solid(R"(<a:tint val="95000"/><a:satMod val="170000"/>)")
        + gradient(R"(<a:tint val="93000"/><a:satMod val="150000"/><a:shade val="98000"/><a:lumMod val="102000"/>)",
                   R"(<a:tint val="98000"/><a:satMod val="130000"/><a:shade val="90000"/><a:lumMod val="103000"/>)",
                   R"(<a:shade val="63000"/><a:satMod val="120000"/>)")
        + QStringLiteral("</a:bgFillStyleLst></a:fmtScheme>");
}

QString colorSchemeXml(const ThemeColors &colors)
{
    QString out = QStringLiteral(R"(<a:clrScheme name="%1">)").arg(escaped(colors.name.isEmpty() ? QStringLiteral("Custom") : colors.name));
    for (int i = 0; i < kThemeSlots; ++i) {
        const char *name = themeSlotNames().at(size_t(i));
        const QString value = hex(colors.values.at(size_t(i)));
        // PowerPoint spells the first two through the system colours.
        QString element;
        if (i == 0 && value == u"000000") {
            element = QStringLiteral(R"(<a:sysClr val="windowText" lastClr="000000"/>)");
        } else if (i == 1 && value == u"FFFFFF") {
            element = QStringLiteral(R"(<a:sysClr val="window" lastClr="FFFFFF"/>)");
        } else {
            element = QStringLiteral(R"(<a:srgbClr val="%1"/>)").arg(value);
        }
        out += QStringLiteral("<a:%1>%2</a:%1>").arg(QLatin1String(name), element);
    }
    return out + QStringLiteral("</a:clrScheme>");
}

QString fontSchemeXml(const ThemeFonts &fonts)
{
    const bool office = fonts.major == u"Calibri Light" && fonts.minor == u"Calibri";
    return QStringLiteral(R"(<a:fontScheme name="%1">)").arg(escaped(fonts.name.isEmpty() ? QStringLiteral("Custom") : fonts.name))
        + fontElement("majorFont", fonts.major.isEmpty() ? QStringLiteral("Calibri Light") : fonts.major, office, true)
        + fontElement("minorFont", fonts.minor.isEmpty() ? QStringLiteral("Calibri") : fonts.minor, office, false)
        + QStringLiteral("</a:fontScheme>");
}

QString themeFamilyXml(const QString &name)
{
    // Office's own theme keeps Office's family ids; QindaShow's themes get
    // stable ones derived from their names.
    const bool office = name == u"Office Theme";
    const QString id = office ? QStringLiteral("{62F939B6-93AF-4DB8-9C6B-D6C7DFDC589F}") : uuidFor(name, "id");
    const QString vid = office ? QStringLiteral("{4A3C46E8-61CC-4603-A589-7422A47A8E4A}") : uuidFor(name, "vid");
    return QStringLiteral(R"(<a:ext uri="{05A4C25C-085E-4340-85A3-A5531E510DB2}"><thm15:themeFamily xmlns:thm15="http://schemas.microsoft.com/office/thememl/2012/main" name="%1" id="%2" vid="%3"/></a:ext>)")
        .arg(escaped(name), id, vid);
}

QByteArray themePartXml(const ThemeSpec &theme)
{
    if (!theme.part.isEmpty()) {
        return theme.part;
    }
    const QString name = theme.name.isEmpty() ? QStringLiteral("Office Theme") : theme.name;
    const QString xml = QStringLiteral(R"(<a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="%1"><a:themeElements>)")
                            .arg(escaped(name))
        + colorSchemeXml(theme.colors) + fontSchemeXml(theme.fonts)
        + formatSchemeXml(theme.effects.isEmpty() ? QStringLiteral("Office") : theme.effects)
        + QStringLiteral("</a:themeElements><a:objectDefaults/><a:extraClrSchemeLst/><a:extLst>") + themeFamilyXml(name)
        + QStringLiteral("</a:extLst></a:theme>");
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n") + xml.toUtf8();
}

} // namespace QindaOffice::Show
