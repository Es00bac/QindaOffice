// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_write_parts.h"

#include "pptx_show.h"

#include "pptx_write_shapes.h"
#include "pptx_write_styles.h"
#include "pptx_write_text.h"

#include "ooxml_namespaces.h"

namespace QindaOffice::Show::Pptx {

namespace {

const char *const kNamespaces =
    R"(xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main")";

const char *const kColorMap =
    R"(bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" folHlink="folHlink")";

const char *const kGroupStart =
    R"(<p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/><a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>)";

QString pml(const char *kind)
{
    return QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.") + QLatin1String(kind);
}

QString escaped(const QString &text)
{
    return text.toHtmlEscaped();
}

QString hex(const QColor &color)
{
    return (color.isValid() ? color : QColor(Qt::black)).name(QColor::HexRgb).mid(1).toUpper();
}

QByteArray document(const QString &body)
{
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n") + body.toUtf8();
}

// M75 (D-084): the theme a generated master relates to, written as
// PowerPoint writes one (show_theme.h).
QString addTheme(WriteSession &session, const ThemeSpec &theme)
{
    const QString part = session.writer.uniquePartName(QStringLiteral("ppt/theme/theme"), QStringLiteral(".xml"));
    session.writer.addPart(part, themePartXml(theme), QString::fromUtf16(Ooxml::ContentType::Theme));
    return part;
}

// The theme of a generated master: the model's own, else Office's in the
// master's font and name (a QindaShow master from the Masters dialog).
ThemeSpec generatedTheme(const SlideMaster *master)
{
    if (master != nullptr && !master->theme.isEmpty()) {
        return master->theme;
    }
    ThemeSpec theme = officeTheme();
    if (master != nullptr && !master->fontFamily.isEmpty()) {
        theme.fonts = {QStringLiteral("Custom"), master->fontFamily, master->fontFamily};
    }
    if (master != nullptr && !master->name.isEmpty()) {
        theme.name = master->name;
    }
    return theme;
}

// A generated master with the model's text styles where it has them.
QByteArray withModelStyles(const QByteArray &bytes, const SlideMaster *master, const ThemeFonts &fonts)
{
    if (master == nullptr || (master->titleStyle.isEmpty() && master->bodyStyle.isEmpty() && master->otherStyle.isEmpty())) {
        return bytes;
    }
    const Ooxml::XmlElementPtr root = Ooxml::parseXml(bytes);
    Ooxml::XmlElement *styles = root ? root->child(nsP(), u"txStyles") : nullptr;
    if (styles == nullptr) {
        return bytes;
    }
    for (const auto &[name, levels] : {std::pair{"titleStyle", &master->titleStyle}, std::pair{"bodyStyle", &master->bodyStyle},
                                       std::pair{"otherStyle", &master->otherStyle}}) {
        if (levels->isEmpty()) {
            continue;
        }
        for (Ooxml::XmlChild &child : styles->children) {
            if (child.element && child.element->name == QLatin1String(name)) {
                child.element = listStyleElement(nsP(), QLatin1String(name), *levels, fonts);
            }
        }
    }
    return Ooxml::serializeDocument(*root);
}

QString placeholder(int id, const QString &name, const QString &ph, const QString &xfrm, const QString &body)
{
    return QStringLiteral(R"(<p:sp><p:nvSpPr><p:cNvPr id="%1" name="%2"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr><p:nvPr>%3</p:nvPr></p:nvSpPr><p:spPr>%4</p:spPr><p:txBody>%5</p:txBody></p:sp>)")
        .arg(id)
        .arg(escaped(name), ph, xfrm, body);
}

QString box(const QSizeF &slide, qreal x, qreal y, qreal w, qreal h)
{
    return QStringLiteral(R"(<a:xfrm><a:off x="%1" y="%2"/><a:ext cx="%3" cy="%4"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom>)")
        .arg(emu(slide.width() * x), emu(slide.height() * y), emu(slide.width() * w), emu(slide.height() * h));
}

QString levelStyles(const QColor &body)
{
    static const int sizes[] = {2800, 2400, 2000, 1800, 1800, 1800, 1800, 1800, 1800};
    QString out;
    for (int level = 0; level < 9; ++level) {
        const qint64 margin = 228600 + 457200 * level;
        out += QStringLiteral(R"(<a:lvl%1pPr marL="%2" indent="-228600" algn="l"><a:lnSpc><a:spcPct val="90000"/></a:lnSpc><a:spcBef><a:spcPts val="%3"/></a:spcBef><a:buFont typeface="Arial"/><a:buChar char="&#x2022;"/><a:defRPr sz="%4"><a:solidFill><a:srgbClr val="%5"/></a:solidFill><a:latin typeface="+mn-lt"/></a:defRPr></a:lvl%1pPr>)")
                   .arg(level + 1)
                   .arg(margin)
                   .arg(level == 0 ? 1000 : 500)
                   .arg(sizes[level])
                   .arg(hex(body));
    }
    return out;
}

QString layoutXml(const QString &type, const QString &name, const QString &shapes)
{
    return QStringLiteral(R"(<p:sldLayout %1 type="%2" preserve="1"><p:cSld name="%3"><p:spTree>%4%5</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>)")
        .arg(QLatin1String(kNamespaces), type, escaped(name), QLatin1String(kGroupStart), shapes);
}

const char *const kEmptyBody = R"(<a:bodyPr/><a:lstStyle/><a:p><a:endParaRPr lang="en-US"/></a:p>)";

} // namespace

// ST_SlideLayoutType: a model layout of another type is written as "cust".
bool isLayoutType(const QString &type)
{
    static const QSet<QString> types = {
        QStringLiteral("title"), QStringLiteral("tx"), QStringLiteral("twoColTx"), QStringLiteral("tbl"),
        QStringLiteral("txAndChart"), QStringLiteral("chartAndTx"), QStringLiteral("dgm"), QStringLiteral("chart"),
        QStringLiteral("txAndClipArt"), QStringLiteral("clipArtAndTx"), QStringLiteral("titleOnly"),
        QStringLiteral("blank"), QStringLiteral("txAndObj"), QStringLiteral("objAndTx"), QStringLiteral("objOnly"),
        QStringLiteral("obj"), QStringLiteral("txAndMedia"), QStringLiteral("mediaAndTx"), QStringLiteral("objOverTx"),
        QStringLiteral("txOverObj"), QStringLiteral("txAndTwoObj"), QStringLiteral("twoObjAndTx"),
        QStringLiteral("twoObjOverTx"), QStringLiteral("fourObj"), QStringLiteral("vertTx"),
        QStringLiteral("clipArtAndVertTx"), QStringLiteral("vertTitleAndTx"), QStringLiteral("vertTitleAndTxOverChart"),
        QStringLiteral("twoObj"), QStringLiteral("objAndTwoObj"), QStringLiteral("twoObjAndObj"), QStringLiteral("cust"),
        QStringLiteral("secHead"), QStringLiteral("twoTxTwoObj"), QStringLiteral("objTx"), QStringLiteral("picTx")};
    return types.contains(type);
}

namespace {

// A generated master or layout part with the model's background and
// decorations written in; `xml` is the part as generated.
QByteArray decorated(WriteSession &session, const QString &part, const QString &xml, const Background &background,
                     const QList<Shape> &shapes, bool showMasterShapes = true)
{
    if (!background.set && shapes.isEmpty() && showMasterShapes) {
        return document(xml);
    }
    Ooxml::XmlElementPtr root = Ooxml::parseXml(xml.toUtf8());
    Ooxml::XmlElement *cSld = root ? root->child(nsP(), u"cSld") : nullptr;
    Ooxml::XmlElement *tree = cSld != nullptr ? cSld->child(nsP(), u"spTree") : nullptr;
    if (tree == nullptr) {
        return document(xml);
    }
    if (!showMasterShapes) {
        root->setAttribute(QString(), QStringLiteral("showMasterSp"), QStringLiteral("0"));
    }
    PartWriter out(session, part);
    if (background.set) {
        cSld->removeChildren(nsP(), u"bg");
        cSld->insertElementBefore(backgroundElement(out, background), {u"spTree"});
    }
    // The generated placeholders hold ids 2 and 3.
    out.reserve(2);
    out.reserve(3);
    for (const Shape &shape : shapes) {
        if (Ooxml::XmlElementPtr element = writeShape(out, shape, out.claim(0))) {
            tree->appendElement(element);
        }
    }
    return Ooxml::serializeDocument(*root);
}

} // namespace

GeneratedMaster generateMaster(WriteSession &session, const SlideMaster *master, quint32 *nextId)
{
    Ooxml::PackageWriter &w = session.writer;
    const QSizeF slide = session.deck.slideSize;
    const ThemeSpec theme = generatedTheme(master);
    const QString themePart = addTheme(session, theme);
    GeneratedMaster out;
    out.themePart = themePart;
    out.part = w.uniquePartName(QStringLiteral("ppt/slideMasters/slideMaster"), QStringLiteral(".xml"));
    out.masterId = (*nextId)++;
    // Layouts first: the master lists them.
    const QString empty = QLatin1String(kEmptyBody);
    const QString titleBody = QStringLiteral(R"(<a:bodyPr anchor="b"/><a:lstStyle><a:lvl1pPr algn="ctr"><a:defRPr sz="6000"/></a:lvl1pPr></a:lstStyle><a:p><a:endParaRPr lang="en-US"/></a:p>)");
    const QString subtitleBody = QStringLiteral(R"(<a:bodyPr/><a:lstStyle><a:lvl1pPr marL="0" indent="0" algn="ctr"><a:buNone/><a:defRPr sz="2400"/></a:lvl1pPr></a:lstStyle><a:p><a:endParaRPr lang="en-US"/></a:p>)");
    const struct {
        const char *type;
        const char *name;
        QString shapes;
        QString *into;
    } layouts[] = {
        {"title", "Title Slide",
         placeholder(2, QStringLiteral("Title 1"), QStringLiteral(R"(<p:ph type="ctrTitle"/>)"), box(slide, 0.125, 0.1637, 0.75, 0.3481), titleBody)
             + placeholder(3, QStringLiteral("Subtitle 2"), QStringLiteral(R"(<p:ph type="subTitle" idx="1"/>)"),
                           box(slide, 0.125, 0.5252, 0.75, 0.2414), subtitleBody),
         &out.titleLayout},
        {"obj", "Title and Content",
         placeholder(2, QStringLiteral("Title 1"), QStringLiteral(R"(<p:ph type="title"/>)"), QString(), empty)
             + placeholder(3, QStringLiteral("Content Placeholder 2"), QStringLiteral(R"(<p:ph idx="1"/>)"), QString(), empty),
         &out.contentLayout},
        {"blank", "Blank", QString(), &out.blankLayout},
    };
    QString layoutIds;
    const auto addLayout = [&](const QString &part, const QByteArray &bytes) {
        w.addPart(part, bytes, pml("slideLayout+xml"));
        w.addRelationship(part, relType("slideMaster"), out.part);
        const QString id = w.addRelationship(out.part, relType("slideLayout"), part);
        layoutIds += QStringLiteral(R"(<p:sldLayoutId id="%1" r:id="%2"/>)").arg((*nextId)++).arg(id);
    };
    for (const auto &layout : layouts) {
        *layout.into = w.uniquePartName(QStringLiteral("ppt/slideLayouts/slideLayout"), QStringLiteral(".xml"));
        addLayout(*layout.into, document(layoutXml(QLatin1String(layout.type), QLatin1String(layout.name), layout.shapes)));
    }
    // The model's own layouts of this master (their placeholders are the
    // generated ones of the closest kind; the slides carry their text).
    for (const SlideLayout &layout : session.deck.layouts) {
        if (master == nullptr || layout.master != master->id) {
            continue;
        }
        const QString type = isLayoutType(layout.type) ? layout.type : QStringLiteral("cust");
        // M75: a layout that brought its placeholders writes them.
        const bool own = !layout.placeholders.isEmpty();
        const QString shapes = own              ? QString()
            : type == u"title"                  ? layouts[0].shapes
            : type == u"blank"                  ? QString()
            : type == u"titleOnly"              ? placeholder(2, QStringLiteral("Title 1"), QStringLiteral(R"(<p:ph type="title"/>)"), QString(), empty)
                                                : layouts[1].shapes;
        const QString part = w.uniquePartName(QStringLiteral("ppt/slideLayouts/slideLayout"), QStringLiteral(".xml"));
        const QString name = layout.name.isEmpty() ? QStringLiteral("Layout") : layout.name;
        addLayout(part, decorated(session, part, layoutXml(type, name, shapes), layout.background,
                                  own ? layout.placeholders + layout.shapes : layout.shapes, layout.showMasterShapes));
        out.modelLayouts.append({layout.id, layout.type, part});
    }
    w.addRelationship(out.part, relType("theme"), themePart);
    const QColor background = master != nullptr ? master->background : QColor(Qt::white);
    const QColor title = master != nullptr ? master->titleColor : QColor(Qt::black);
    const QColor body = master != nullptr ? master->bodyColor : QColor(Qt::black);
    const bool ownPlaceholders = master != nullptr && !master->placeholders.isEmpty();
    const QString shapes = ownPlaceholders ? QString() : placeholder(2, QStringLiteral("Title Placeholder 1"), QStringLiteral(R"(<p:ph type="title"/>)"),
                                       box(slide, 0.06875, 0.0532, 0.8625, 0.1933),
                                       QStringLiteral(R"(<a:bodyPr vert="horz" anchor="ctr"><a:normAutofit/></a:bodyPr><a:lstStyle/><a:p><a:r><a:rPr lang="en-US"/><a:t>Click to edit Master title style</a:t></a:r></a:p>)"))
        + placeholder(3, QStringLiteral("Text Placeholder 2"), QStringLiteral(R"(<p:ph type="body" idx="1"/>)"),
                      box(slide, 0.06875, 0.2662, 0.8625, 0.6345),
                      QStringLiteral(R"(<a:bodyPr vert="horz"><a:normAutofit/></a:bodyPr><a:lstStyle/><a:p><a:pPr lvl="0"/><a:r><a:rPr lang="en-US"/><a:t>Click to edit Master text styles</a:t></a:r></a:p>)"));
    const QString xml = QStringLiteral(R"(<p:sldMaster %1><p:cSld><p:bg><p:bgPr><a:solidFill><a:srgbClr val="%2"/></a:solidFill><a:effectLst/></p:bgPr></p:bg><p:spTree>%3%4</p:spTree></p:cSld><p:clrMap %5/><p:sldLayoutIdLst>%6</p:sldLayoutIdLst>)"
                                       R"(<p:txStyles><p:titleStyle><a:lvl1pPr algn="l"><a:lnSpc><a:spcPct val="90000"/></a:lnSpc><a:spcBef><a:spcPct val="0"/></a:spcBef><a:buNone/><a:defRPr sz="4400"><a:solidFill><a:srgbClr val="%7"/></a:solidFill><a:latin typeface="+mj-lt"/></a:defRPr></a:lvl1pPr></p:titleStyle>)"
                                       R"(<p:bodyStyle>%8</p:bodyStyle><p:otherStyle><a:lvl1pPr marL="0" algn="l"><a:defRPr sz="1800"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill><a:latin typeface="+mn-lt"/></a:defRPr></a:lvl1pPr></p:otherStyle></p:txStyles></p:sldMaster>)")
                            .arg(QLatin1String(kNamespaces), hex(background), QLatin1String(kGroupStart), shapes,
                                 QLatin1String(kColorMap), layoutIds, hex(title), levelStyles(body));
    Background styled;
    styled.set = master != nullptr && master->backgroundStyle.kind != FillKind::Plain;
    if (styled.set) {
        styled.color = master->background;
        styled.style = master->backgroundStyle;
    }
    QList<Shape> drawn = master != nullptr ? master->shapes : QList<Shape>();
    if (ownPlaceholders) {
        drawn = master->placeholders + drawn;
    }
    w.addPart(out.part, withModelStyles(decorated(session, out.part, xml, styled, drawn), master, theme.fonts),
              pml("slideMaster+xml"));
    return out;
}

QString generateNotesMaster(WriteSession &session)
{
    Ooxml::PackageWriter &w = session.writer;
    const QString part = w.uniquePartName(QStringLiteral("ppt/notesMasters/notesMaster"), QStringLiteral(".xml"));
    const QSizeF notes = session.deck.notesSize;
    const QString shapes =
        QStringLiteral(R"(<p:sp><p:nvSpPr><p:cNvPr id="2" name="Slide Image Placeholder 1"/><p:cNvSpPr><a:spLocks noGrp="1" noRot="1" noChangeAspect="1"/></p:cNvSpPr><p:nvPr><p:ph type="sldImg" idx="2"/></p:nvPr></p:nvSpPr><p:spPr>%1</p:spPr></p:sp>)")
            .arg(box(notes, 0.125, 0.125, 0.75, 0.3375))
        + QStringLiteral(R"(<p:sp><p:nvSpPr><p:cNvPr id="3" name="Notes Placeholder 2"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr><p:nvPr><p:ph type="body" sz="quarter" idx="3"/></p:nvPr></p:nvSpPr><p:spPr>%1</p:spPr><p:txBody>%2</p:txBody></p:sp>)")
              .arg(box(notes, 0.1, 0.48, 0.8, 0.39), QLatin1String(kEmptyBody));
    const QString xml = QStringLiteral(R"(<p:notesMaster %1><p:cSld><p:bg><p:bgRef idx="1001"><a:schemeClr val="bg1"/></p:bgRef></p:bg><p:spTree>%2%3</p:spTree></p:cSld><p:clrMap %4/>)"
                                       R"(<p:notesStyle><a:lvl1pPr marL="0" algn="l"><a:defRPr sz="1200"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill><a:latin typeface="+mn-lt"/></a:defRPr></a:lvl1pPr></p:notesStyle></p:notesMaster>)")
                            .arg(QLatin1String(kNamespaces), QLatin1String(kGroupStart), shapes, QLatin1String(kColorMap));
    w.addPart(part, document(xml), pml("notesMaster+xml"));
    w.addRelationship(part, relType("theme"), addTheme(session, officeTheme()));
    return part;
}

Ooxml::XmlElementPtr notesSlideElement(const QString &notes)
{
    QString paragraphs;
    for (const QString &line : notes.split(QLatin1Char('\n'))) {
        paragraphs += line.isEmpty() ? QStringLiteral("<a:p><a:endParaRPr lang=\"en-US\"/></a:p>")
                                     : QStringLiteral("<a:p><a:r><a:rPr lang=\"en-US\"/><a:t>%1</a:t></a:r></a:p>")
                                           .arg(escaped(Ooxml::xmlSafeText(line)));
    }
    const QString xml = QStringLiteral(R"(<p:notes %1><p:cSld><p:spTree>%2)"
                                       R"(<p:sp><p:nvSpPr><p:cNvPr id="2" name="Slide Image Placeholder 1"/><p:cNvSpPr><a:spLocks noGrp="1" noRot="1" noChangeAspect="1"/></p:cNvSpPr><p:nvPr><p:ph type="sldImg"/></p:nvPr></p:nvSpPr><p:spPr/></p:sp>)"
                                       R"(<p:sp><p:nvSpPr><p:cNvPr id="3" name="Notes Placeholder 2"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr><p:nvPr><p:ph type="body" idx="1"/></p:nvPr></p:nvSpPr><p:spPr/><p:txBody><a:bodyPr/><a:lstStyle/>%3</p:txBody></p:sp>)"
                                       R"(</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:notes>)")
                            .arg(QLatin1String(kNamespaces), QLatin1String(kGroupStart), paragraphs);
    return Ooxml::parseXml(xml.toUtf8());
}

PresentationParts generatePresentationParts(WriteSession &session)
{
    Ooxml::PackageWriter &w = session.writer;
    PresentationParts parts;
    parts.presProps = QStringLiteral("ppt/presProps.xml");
    // M91 (D-099): Set Up Slide Show, as PowerPoint always writes it.
    w.addXmlPart(parts.presProps, *presentationPropertiesElement(session.deck.showSettings, nullptr), pml("presProps+xml"));
    parts.viewProps = QStringLiteral("ppt/viewProps.xml");
    w.addPart(parts.viewProps,
              document(QStringLiteral(R"(<p:viewPr %1><p:normalViewPr><p:restoredLeft sz="15620"/><p:restoredTop sz="94660"/></p:normalViewPr><p:slideViewPr><p:cSldViewPr snapToGrid="0"><p:cViewPr varScale="1"><p:scale><a:sx n="100" d="100"/><a:sy n="100" d="100"/></p:scale><p:origin x="0" y="0"/></p:cViewPr><p:guideLst/></p:cSldViewPr></p:slideViewPr><p:notesTextViewPr><p:cViewPr><p:scale><a:sx n="1" d="1"/><a:sy n="1" d="1"/></p:scale><p:origin x="0" y="0"/></p:cViewPr></p:notesTextViewPr><p:gridSpacing cx="76200" cy="76200"/></p:viewPr>)")
                           .arg(QLatin1String(kNamespaces))),
              pml("viewProps+xml"));
    parts.tableStyles = QStringLiteral("ppt/tableStyles.xml");
    w.addPart(parts.tableStyles,
              document(QStringLiteral(R"(<a:tblStyleLst xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" def="{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}"/>)")),
              pml("tableStyles+xml"));
    return parts;
}

Ooxml::XmlElementPtr defaultTextStyleElement()
{
    QString levels;
    for (int level = 0; level < 9; ++level) {
        levels += QStringLiteral(R"(<a:lvl%1pPr marL="%2" algn="l"><a:defRPr sz="1800"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill><a:latin typeface="+mn-lt"/></a:defRPr></a:lvl%1pPr>)")
                      .arg(level + 1)
                      .arg(457200 * level);
    }
    return Ooxml::parseXml(QStringLiteral(R"(<p:defaultTextStyle %1><a:defPPr><a:defRPr lang="en-US"/></a:defPPr>%2</p:defaultTextStyle>)")
                               .arg(QLatin1String(kNamespaces), levels)
                               .toUtf8());
}

} // namespace QindaOffice::Show::Pptx
