// SPDX-License-Identifier: GPL-3.0-or-later
// The parts a Visio document needs when it is written without a source:
// document.xml with Visio's standard style sheets, windows.xml, a page
// entry — and docProps/custom.xml asking Visio to recalculate on open
// (visio_write_p.h).
#include "visio_write_p.h"

#include "ooxml_namespaces.h"

namespace QindaOffice::Diagram::Visio {

namespace {

// AGENT-NOTE: generated from the cell list Visio 2016 writes for the
// "No Style" style sheet (ID 0); "Name=Value[|Unit][!]" with ! for
// F="No Formula". US units.
constexpr char kNoStyleCells[] =
    "EnableLineProps=1;EnableFillProps=1;EnableTextProps=1;HideForApply=0;LineWeight=0.01041666666666667;"
    "LineColor=0;LinePattern=1;Rounding=0;EndArrowSize=2;BeginArrow=0;EndArrow=0;LineCap=0;"
    "BeginArrowSize=2;LineColorTrans=0;CompoundType=0;FillForegnd=1;FillBkgnd=0;FillPattern=1;"
    "ShdwForegnd=0;ShdwPattern=0;FillForegndTrans=0;FillBkgndTrans=0;ShdwForegndTrans=0;ShapeShdwType=0;"
    "ShapeShdwOffsetX=0;ShapeShdwOffsetY=0;ShapeShdwObliqueAngle=0;ShapeShdwScaleFactor=1;"
    "ShapeShdwBlur=0;ShapeShdwShow=0;LeftMargin=0;RightMargin=0;TopMargin=0;BottomMargin=0;"
    "VerticalAlign=1;TextBkgnd=0;DefaultTabStop=0.5;TextDirection=0;TextBkgndTrans=0;LockWidth=0;"
    "LockHeight=0;LockMoveX=0;LockMoveY=0;LockAspect=0;LockDelete=0;LockBegin=0;LockEnd=0;LockRotate=0;"
    "LockCrop=0;LockVtxEdit=0;LockTextEdit=0;LockFormat=0;LockGroup=0;LockCalcWH=0;LockSelect=0;"
    "LockCustProp=0;LockFromGroupFormat=0;LockThemeColors=0;LockThemeEffects=0;LockThemeConnectors=0;"
    "LockThemeFonts=0;LockThemeIndex=0;LockReplace=0;LockVariation=0;NoObjHandles=0;NonPrinting=0;"
    "NoCtlHandles=0;NoAlignBox=0;UpdateAlignBox=0;HideText=0;DynFeedback=0;GlueType=0;WalkPreference=0;"
    "BegTrigger=0!;EndTrigger=0!;ObjType=0;Comment=;IsDropSource=0;NoLiveDynamics=0;LocalizeMerge=0;"
    "NoProofing=0;Calendar=0;LangID=en-US;ShapeKeywords=;DropOnPageScale=1;TheData=0!;TheText=0!;"
    "EventDblClick=0!;EventXFMod=0!;EventDrop=0!;EventMultiDrop=0!;HelpTopic=;Copyright=;LayerMember=;"
    "XRulerDensity=32;YRulerDensity=32;XRulerOrigin=0;YRulerOrigin=0;XGridDensity=8;YGridDensity=8;"
    "XGridSpacing=0;YGridSpacing=0;XGridOrigin=0;YGridOrigin=0;Gamma=1;Contrast=0.5;Brightness=0.5;"
    "Sharpen=0;Blur=0;Denoise=0;Transparency=0;SelectMode=1;DisplayMode=2;IsDropTarget=0;IsSnapTarget=1;"
    "IsTextEditTarget=1;DontMoveChildren=0;ShapePermeableX=0;ShapePermeableY=0;ShapePermeablePlace=0;"
    "Relationships=0;ShapeFixedCode=0;ShapePlowCode=0;ShapeRouteStyle=0;ShapePlaceStyle=0;ConFixedCode=0;"
    "ConLineJumpCode=0;ConLineJumpStyle=0;ConLineJumpDirX=0;ConLineJumpDirY=0;ShapePlaceFlip=0;"
    "ConLineRouteExt=0;ShapeSplit=0;ShapeSplittable=0;DisplayLevel=0;ResizePage=0;EnableGrid=0;"
    "DynamicsOff=0;CtrlAsInput=0;AvoidPageBreaks=0;PlaceStyle=0;RouteStyle=0;PlaceDepth=0;PlowCode=0;"
    "LineJumpCode=1;LineJumpStyle=0;PageLineJumpDirX=0;PageLineJumpDirY=0;LineToNodeX=0.125;"
    "LineToNodeY=0.125;BlockSizeX=0.25;BlockSizeY=0.25;AvenueSizeX=0.375;AvenueSizeY=0.375;"
    "LineToLineX=0.125;LineToLineY=0.125;LineJumpFactorX=0.66666666666667;"
    "LineJumpFactorY=0.66666666666667;LineAdjustFrom=0;LineAdjustTo=0;PlaceFlip=0;LineRouteExt=0;"
    "PageShapeSplit=0;PageLeftMargin=0.25;PageRightMargin=0.25;PageTopMargin=0.25;PageBottomMargin=0.25;"
    "ScaleX=1;ScaleY=1;PagesX=1;PagesY=1;CenterX=0;CenterY=0;OnPage=0;PrintGrid=0;PrintPageOrientation=1;"
    "PaperKind=9;PaperSource=7;QuickStyleLineColor=100;QuickStyleFillColor=100;QuickStyleShadowColor=100;"
    "QuickStyleFontColor=100;QuickStyleLineMatrix=100;QuickStyleFillMatrix=100;"
    "QuickStyleEffectsMatrix=100;QuickStyleFontMatrix=100;QuickStyleType=0;QuickStyleVariation=0;"
    "LineGradientDir=0;LineGradientAngle=1.5707963267949;FillGradientDir=0;"
    "FillGradientAngle=1.5707963267949;LineGradientEnabled=0;FillGradientEnabled=0;"
    "RotateGradientWithShape=1;UseGroupGradient=0;BevelTopType=0;BevelTopWidth=0;BevelTopHeight=0;"
    "BevelBottomType=0;BevelBottomWidth=0;BevelBottomHeight=0;BevelDepthColor=1;BevelDepthSize=0;"
    "BevelContourColor=0;BevelContourSize=0;BevelMaterialType=0;BevelLightingType=0;BevelLightingAngle=0;"
    "RotationXAngle=0;RotationYAngle=0;RotationZAngle=0;RotationType=0;Perspective=0;"
    "DistanceFromGround=0;KeepTextFlat=0;ReflectionTrans=0;ReflectionSize=0;ReflectionDist=0;"
    "ReflectionBlur=0;GlowColor=1;GlowColorTrans=0;GlowSize=0;SoftEdgesSize=0;SketchSeed=0;"
    "SketchEnabled=0;SketchAmount=5;SketchLineWeight=0.04166666666666666|PT;SketchLineChange=0.14;"
    "SketchFillChange=0.1;ColorSchemeIndex=0;EffectSchemeIndex=0;ConnectorSchemeIndex=0;"
    "FontSchemeIndex=0;ThemeIndex=0;VariationColorIndex=0;VariationStyleIndex=0;EmbellishmentIndex=0;"
    "ReplaceLockShapeData=0;ReplaceLockText=0;ReplaceLockFormat=0;ReplaceCopyCells=0|BOOL!;PageWidth=0!;"
    "PageHeight=0!;ShdwOffsetX=0!;ShdwOffsetY=0!;PageScale=0|IN!;DrawingScale=0|IN!;DrawingSizeType=0!;"
    "DrawingScaleType=0!;InhibitSnap=0!;PageLockReplace=0|BOOL!;PageLockDuplicate=0|BOOL!;"
    "UIVisibility=0!;ShdwType=0!;ShdwObliqueAngle=0!;ShdwScaleFactor=0!;DrawingResizeType=0!;"
    ;

const QString kZero = QStringLiteral("0");

XmlElementPtr root(const QString &name)
{
    XmlElementPtr e = makeElement(name);
    e->setAttribute(QString::fromUtf16(Ooxml::Ns::XML), QStringLiteral("space"), QStringLiteral("preserve"));
    return e;
}

XmlElement &styleSheet(XmlElement &sheets, int id, const QString &name, int parent)
{
    XmlElement &s = appendElement(sheets, QStringLiteral("StyleSheet"));
    s.setAttribute({}, QStringLiteral("ID"), QString::number(id));
    s.setAttribute({}, QStringLiteral("NameU"), name);
    s.setAttribute({}, QStringLiteral("IsCustomNameU"), QStringLiteral("1"));
    s.setAttribute({}, QStringLiteral("Name"), name);
    s.setAttribute({}, QStringLiteral("IsCustomName"), QStringLiteral("1"));
    if (parent >= 0) {
        for (const char *kind : {"LineStyle", "FillStyle", "TextStyle"}) {
            s.setAttribute({}, QString::fromLatin1(kind), QString::number(parent));
        }
    }
    for (const char *cell : {"EnableLineProps", "EnableFillProps", "EnableTextProps"}) {
        appendCell(s, QString::fromLatin1(cell), QStringLiteral("1"));
    }
    appendCell(s, QStringLiteral("HideForApply"), kZero);
    return s;
}

XmlElement &row(XmlElement &shape, const QString &section)
{
    XmlElement &s = appendElement(shape, QStringLiteral("Section"));
    s.setAttribute({}, QStringLiteral("N"), section);
    XmlElement &r = appendElement(s, QStringLiteral("Row"));
    r.setAttribute({}, QStringLiteral("IX"), kZero);
    return r;
}

void noStyle(XmlElement &sheets)
{
    XmlElement &s = appendElement(sheets, QStringLiteral("StyleSheet"));
    s.setAttribute({}, QStringLiteral("ID"), kZero);
    s.setAttribute({}, QStringLiteral("NameU"), QStringLiteral("No Style"));
    s.setAttribute({}, QStringLiteral("IsCustomNameU"), QStringLiteral("1"));
    s.setAttribute({}, QStringLiteral("Name"), QStringLiteral("No Style"));
    s.setAttribute({}, QStringLiteral("IsCustomName"), QStringLiteral("1"));
    for (const QString &entry : QString::fromLatin1(kNoStyleCells).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        QString spec = entry;
        const bool noFormula = spec.endsWith(QLatin1Char('!'));
        if (noFormula) {
            spec.chop(1);
        }
        const QString name = spec.section(QLatin1Char('='), 0, 0);
        const QString rest = spec.section(QLatin1Char('='), 1);
        appendCell(s, name, rest.section(QLatin1Char('|'), 0, 0), noFormula ? QStringLiteral("No Formula") : QString(),
                   rest.section(QLatin1Char('|'), 1));
    }
    XmlElement &character = row(s, QStringLiteral("Character"));
    for (const auto &[name, value] : {std::pair{"Font", "Calibri"}, {"Color", "0"}, {"Style", "0"}, {"Case", "0"},
                                      {"Pos", "0"}, {"FontScale", "1"}, {"Size", "0.1666666666666667"},
                                      {"DblUnderline", "0"}, {"Overline", "0"}, {"Strikethru", "0"},
                                      {"DoubleStrikethrough", "0"}, {"Letterspace", "0"}, {"ColorTrans", "0"},
                                      {"AsianFont", "0"}, {"ComplexScriptFont", "0"}, {"ComplexScriptSize", "-1"},
                                      {"LangID", "en-US"}}) {
        appendCell(character, QString::fromLatin1(name), QString::fromLatin1(value));
    }
    XmlElement &paragraph = row(s, QStringLiteral("Paragraph"));
    for (const auto &[name, value] : {std::pair{"IndFirst", "0"}, {"IndLeft", "0"}, {"IndRight", "0"},
                                      {"SpLine", "-1.2"}, {"SpBefore", "0"}, {"SpAfter", "0"}, {"HorzAlign", "1"},
                                      {"Bullet", "0"}, {"BulletStr", ""}, {"BulletFont", "0"},
                                      {"BulletFontSize", "-1"}, {"TextPosAfterBullet", "0"}, {"Flags", "0"}}) {
        appendCell(paragraph, QString::fromLatin1(name), QString::fromLatin1(value));
    }
    XmlElement &tabs = appendElement(s, QStringLiteral("Section"));
    tabs.setAttribute({}, QStringLiteral("N"), QStringLiteral("Tabs"));
    appendElement(tabs, QStringLiteral("Row")).setAttribute({}, QStringLiteral("IX"), kZero);
}

} // namespace

XmlElementPtr freshDocument()
{
    XmlElementPtr doc = root(QStringLiteral("VisioDocument"));
    XmlElement &settings = appendElement(*doc, QStringLiteral("DocumentSettings"));
    settings.setAttribute({}, QStringLiteral("TopPage"), kZero);
    for (const char *name : {"DefaultTextStyle", "DefaultLineStyle", "DefaultFillStyle"}) {
        settings.setAttribute({}, QString::fromLatin1(name), QStringLiteral("3"));
    }
    settings.setAttribute({}, QStringLiteral("DefaultGuideStyle"), QStringLiteral("4"));
    for (const auto &[name, value] : {std::pair{"GlueSettings", "9"}, {"SnapSettings", "65847"}, {"SnapExtensions", "34"},
                                      {"SnapAngles", ""}, {"DynamicGridEnabled", "1"}, {"ProtectStyles", "0"},
                                      {"ProtectShapes", "0"}, {"ProtectMasters", "0"}, {"ProtectBkgnds", "0"}}) {
        XmlElement &e = appendElement(settings, QString::fromLatin1(name));
        if (*value != 0) {
            e.appendText(QString::fromLatin1(value));
        }
    }
    XmlElement &faces = appendElement(*doc, QStringLiteral("FaceNames"));
    XmlElement &calibri = appendElement(faces, QStringLiteral("FaceName"));
    calibri.setAttribute({}, QStringLiteral("NameU"), QStringLiteral("Calibri"));
    calibri.setAttribute({}, QStringLiteral("UnicodeRanges"), QStringLiteral("-469750017 -1073732485 9 0"));
    calibri.setAttribute({}, QStringLiteral("CharSets"), QStringLiteral("536871423 0"));
    calibri.setAttribute({}, QStringLiteral("Panose"), QStringLiteral("2 15 5 2 2 2 4 3 2 4"));
    calibri.setAttribute({}, QStringLiteral("Flags"), QStringLiteral("325"));
    XmlElement &sheets = appendElement(*doc, QStringLiteral("StyleSheets"));
    noStyle(sheets);
    // 1 Text Only: no line, no fill, text at the top left.
    XmlElement &textOnly = styleSheet(sheets, 1, QStringLiteral("Text Only"), 3);
    appendCell(textOnly, QStringLiteral("LinePattern"), kZero);
    appendCell(textOnly, QStringLiteral("FillPattern"), kZero);
    for (const char *margin : {"LeftMargin", "RightMargin", "TopMargin", "BottomMargin"}) {
        appendCell(textOnly, QString::fromLatin1(margin), kZero);
    }
    appendCell(textOnly, QStringLiteral("VerticalAlign"), kZero);
    appendCell(row(textOnly, QStringLiteral("Paragraph")), QStringLiteral("HorzAlign"), kZero);
    // 2 None: no line, no fill.
    XmlElement &none = styleSheet(sheets, 2, QStringLiteral("None"), 3);
    appendCell(none, QStringLiteral("LinePattern"), kZero);
    appendCell(none, QStringLiteral("FillPattern"), kZero);
    // 3 Normal: Visio's four-point text margins.
    XmlElement &normal = styleSheet(sheets, 3, QStringLiteral("Normal"), 0);
    for (const char *margin : {"LeftMargin", "RightMargin", "TopMargin", "BottomMargin"}) {
        appendCell(normal, QString::fromLatin1(margin), QStringLiteral("0.05555555555555555"), {}, QStringLiteral("PT"));
    }
    // 4 Guide: dotted blue-grey, never printed.
    XmlElement &guide = styleSheet(sheets, 4, QStringLiteral("Guide"), 3);
    appendCell(guide, QStringLiteral("LineWeight"), kZero, {}, QStringLiteral("PT"));
    appendCell(guide, QStringLiteral("LineColor"), QStringLiteral("#7f7f7f"));
    appendCell(guide, QStringLiteral("LinePattern"), QStringLiteral("23"));
    appendCell(guide, QStringLiteral("FillPattern"), kZero);
    appendCell(guide, QStringLiteral("NonPrinting"), QStringLiteral("1"));
    // 5 Connector: the style the writer's connectors use.
    XmlElement &connector = styleSheet(sheets, 5, QStringLiteral("Connector"), 3);
    appendCell(connector, QStringLiteral("EndArrow"), QStringLiteral("4"));
    appendCell(connector, QStringLiteral("FillPattern"), kZero);
    appendCell(row(connector, QStringLiteral("Character")), QStringLiteral("Size"), QStringLiteral("0.1111111111111111"),
               {}, QStringLiteral("PT"));
    XmlElement &sheet = appendElement(*doc, QStringLiteral("DocumentSheet"));
    sheet.setAttribute({}, QStringLiteral("NameU"), QStringLiteral("TheDoc"));
    sheet.setAttribute({}, QStringLiteral("IsCustomNameU"), QStringLiteral("1"));
    sheet.setAttribute({}, QStringLiteral("Name"), QStringLiteral("TheDoc"));
    sheet.setAttribute({}, QStringLiteral("IsCustomName"), QStringLiteral("1"));
    for (const char *kind : {"LineStyle", "FillStyle", "TextStyle"}) {
        sheet.setAttribute({}, QString::fromLatin1(kind), kZero);
    }
    for (const auto &[name, value] : {std::pair{"OutputFormat", "0"}, {"LockPreview", "0"}, {"AddMarkup", "0"},
                                      {"ViewMarkup", "0"}, {"PreviewQuality", "0"}, {"PreviewScope", "0"},
                                      {"DocLangID", "en-US"}}) {
        appendCell(sheet, QString::fromLatin1(name), QString::fromLatin1(value));
    }
    return doc;
}

XmlElementPtr freshWindows(const QString &firstPageId, QSizeF firstPageInches)
{
    XmlElementPtr windows = root(QStringLiteral("Windows"));
    windows->setAttribute({}, QStringLiteral("ClientWidth"), QStringLiteral("1600"));
    windows->setAttribute({}, QStringLiteral("ClientHeight"), QStringLiteral("900"));
    XmlElement &w = appendElement(*windows, QStringLiteral("Window"));
    for (const auto &[name, value] : {std::pair{"ID", "0"}, {"WindowType", "Drawing"}, {"WindowState", "1073741824"},
                                      {"WindowLeft", "0"}, {"WindowTop", "0"}, {"WindowWidth", "1600"},
                                      {"WindowHeight", "900"}, {"ContainerType", "Page"}}) {
        w.setAttribute({}, QString::fromLatin1(name), QString::fromLatin1(value));
    }
    w.setAttribute({}, QStringLiteral("Page"), firstPageId);
    w.setAttribute({}, QStringLiteral("ViewScale"), QStringLiteral("-1"));
    w.setAttribute({}, QStringLiteral("ViewCenterX"), numberText(firstPageInches.width() / 2));
    w.setAttribute({}, QStringLiteral("ViewCenterY"), numberText(firstPageInches.height() / 2));
    for (const auto &[name, value] : {std::pair{"ShowRulers", "1"}, {"ShowGrid", "1"}, {"ShowPageBreaks", "0"},
                                      {"ShowGuides", "1"}, {"ShowConnectionPoints", "1"}, {"GlueSettings", "9"},
                                      {"SnapSettings", "65847"}, {"SnapExtensions", "34"}, {"SnapAngles", ""},
                                      {"DynamicGridEnabled", "1"}, {"TabSplitterPos", "0.5"}}) {
        XmlElement &e = appendElement(w, QString::fromLatin1(name));
        if (*value != 0) {
            e.appendText(QString::fromLatin1(value));
        }
    }
    return windows;
}

XmlElementPtr freshPageEntry(int id, const QString &name, QSizeF inches)
{
    XmlElementPtr page = makeElement(QStringLiteral("Page"));
    page->setAttribute({}, QStringLiteral("ID"), QString::number(id));
    page->setAttribute({}, QStringLiteral("NameU"), name);
    page->setAttribute({}, QStringLiteral("Name"), name);
    page->setAttribute({}, QStringLiteral("ViewScale"), QStringLiteral("-1"));
    page->setAttribute({}, QStringLiteral("ViewCenterX"), numberText(inches.width() / 2));
    page->setAttribute({}, QStringLiteral("ViewCenterY"), numberText(inches.height() / 2));
    XmlElement &sheet = appendElement(*page, QStringLiteral("PageSheet"));
    for (const char *kind : {"LineStyle", "FillStyle", "TextStyle"}) {
        sheet.setAttribute({}, QString::fromLatin1(kind), kZero);
    }
    appendCell(sheet, QStringLiteral("PageWidth"), numberText(inches.width()));
    appendCell(sheet, QStringLiteral("PageHeight"), numberText(inches.height()));
    appendCell(sheet, QStringLiteral("ShdwOffsetX"), QStringLiteral("0.125"));
    appendCell(sheet, QStringLiteral("ShdwOffsetY"), QStringLiteral("-0.125"));
    appendCell(sheet, QStringLiteral("PageScale"), QStringLiteral("1"), {}, QStringLiteral("IN"));
    appendCell(sheet, QStringLiteral("DrawingScale"), QStringLiteral("1"), {}, QStringLiteral("IN"));
    appendCell(sheet, QStringLiteral("DrawingSizeType"), kZero);
    appendCell(sheet, QStringLiteral("DrawingScaleType"), kZero);
    appendCell(sheet, QStringLiteral("InhibitSnap"), kZero);
    appendCell(sheet, QStringLiteral("UIVisibility"), kZero);
    appendCell(sheet, QStringLiteral("ShdwType"), kZero);
    appendCell(sheet, QStringLiteral("ShdwObliqueAngle"), kZero);
    appendCell(sheet, QStringLiteral("ShdwScaleFactor"), QStringLiteral("1"));
    appendCell(sheet, QStringLiteral("DrawingResizeType"), QStringLiteral("1"));
    appendCell(sheet, QStringLiteral("PageShapeSplit"), QStringLiteral("1"));
    appendCell(sheet, QStringLiteral("PrintPageOrientation"),
               inches.width() > inches.height() ? QStringLiteral("2") : QStringLiteral("1"));
    return page;
}

XmlElementPtr customProperties(const XmlElement *source)
{
    const QString ns = QString::fromUtf16(Ooxml::Ns::CUSTOM_PROPERTIES);
    const QString vt = QString::fromUtf16(Ooxml::Ns::VT);
    XmlElementPtr props = source != nullptr ? source->clone() : std::make_shared<XmlElement>(ns, QStringLiteral("Properties"));
    int pid = 1;
    for (const XmlElement *p : props->elements(ns, u"property")) {
        if (p->attribute({}, u"name") == QLatin1String("RecalcDocument")) {
            return props;
        }
        pid = std::max(pid, p->attribute({}, u"pid").toInt());
    }
    XmlElement &p = props->appendElement(ns, QStringLiteral("property"));
    p.setAttribute({}, QStringLiteral("fmtid"), QStringLiteral("{D5CDD505-2E9C-101B-9397-08002B2CF9AE}"));
    p.setAttribute({}, QStringLiteral("pid"), QString::number(pid + 1));
    p.setAttribute({}, QStringLiteral("name"), QStringLiteral("RecalcDocument"));
    p.appendElement(vt, QStringLiteral("bool")).appendText(QStringLiteral("true"));
    return props;
}

} // namespace QindaOffice::Diagram::Visio
