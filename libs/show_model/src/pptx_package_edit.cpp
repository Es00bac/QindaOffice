// SPDX-License-Identifier: GPL-3.0-or-later
// Editing a written deck package (pptx_package_edit.h, M82, D-091).

#include "pptx_package_edit.h"

#include "pptx_context.h"
#include "pptx_review.h"

#include "ooxml_namespaces.h"

#include <QRandomGenerator>

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

const QList<QStringView> kPresentationOrder{u"sldMasterIdLst", u"notesMasterIdLst", u"handoutMasterIdLst", u"sldIdLst",
                                            u"sldSz", u"notesSz", u"smartTags", u"embeddedFontLst", u"custShowLst",
                                            u"photoAlbum", u"custDataLst", u"kinsoku", u"defaultTextStyle",
                                            u"modifyVerifier", u"extLst"};

XmlElementPtr make(const QString &ns, const QString &name)
{
    return std::make_shared<XmlElement>(ns, name);
}

void set(XmlElement &e, const QString &name, const QString &value)
{
    e.setAttribute(QString(), name, value);
}

QString presentationPart()
{
    return QString::fromUtf16(kPresentationPart);
}

} // namespace

QString slideContentType()
{
    return QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.slide+xml");
}

QString slideRelType(const char *name)
{
    return QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/") + QLatin1String(name);
}

PackageEdit::PackageEdit(Ooxml::PackageWriter &writer)
    : m_writer(writer)
    , m_presentation(Ooxml::parseXml(writer.partData(presentationPart())))
{
    if (!m_presentation) {
        m_presentation = make(nsP(), QStringLiteral("presentation"));
    }
    // Every master and layout id in use, so the next one is free deck-wide.
    if (const XmlElement *masters = m_presentation->child(nsP(), u"sldMasterIdLst")) {
        for (const XmlElement *entry : masters->elements(nsP(), u"sldMasterId")) {
            m_nextLayoutId = qMax(m_nextLayoutId, entry->attr(u"id").toUInt() + 1);
        }
    }
    for (const QString &part : writer.partNames()) {
        if (!part.startsWith(QLatin1String("ppt/slideMasters/")) || !part.endsWith(QLatin1String(".xml"))) {
            continue;
        }
        const XmlElementPtr root = Ooxml::parseXml(writer.partData(part));
        if (const XmlElement *layouts = root ? root->child(nsP(), u"sldLayoutIdLst") : nullptr) {
            for (const XmlElement *entry : layouts->elements(nsP(), u"sldLayoutId")) {
                m_nextLayoutId = qMax(m_nextLayoutId, entry->attr(u"id").toUInt() + 1);
            }
        }
    }
}

XmlElementPtr PackageEdit::xml(const QString &part) const
{
    return m_writer.hasPart(part) ? Ooxml::parseXml(m_writer.partData(part)) : nullptr;
}

void PackageEdit::setXml(const QString &part, const XmlElement &root)
{
    m_writer.addXmlPart(part, root, m_writer.contentTypeOf(part));
}

XmlElement &PackageEdit::list(const char16_t *name)
{
    if (XmlElement *found = m_presentation->child(nsP(), name)) {
        return *found;
    }
    const qsizetype at = kPresentationOrder.indexOf(QStringView(name));
    return m_presentation->insertElementBefore(make(nsP(), QString::fromUtf16(name)), kPresentationOrder.mid(at + 1));
}

QStringList PackageEdit::slideParts() const
{
    QStringList parts;
    const XmlElement *slides = m_presentation->child(nsP(), u"sldIdLst");
    if (slides == nullptr) {
        return parts;
    }
    const QList<Ooxml::Relationship> rels = m_writer.relationshipsOf(presentationPart());
    for (const XmlElement *entry : slides->elements(nsP(), u"sldId")) {
        const QString id = entry->attribute(nsR(), u"id");
        for (const Ooxml::Relationship &rel : rels) {
            if (rel.id == id) {
                parts.append(rel.target);
            }
        }
    }
    return parts;
}

void PackageEdit::insertSlide(const QString &part, int index)
{
    XmlElement &slides = list(u"sldIdLst");
    quint32 next = 256;
    for (const XmlElement *entry : slides.elements(nsP(), u"sldId")) {
        next = qMax(next, entry->attr(u"id").toUInt() + 1);
    }
    auto entry = make(nsP(), QStringLiteral("sldId"));
    set(*entry, QStringLiteral("id"), QString::number(next));
    entry->setAttribute(nsR(), QStringLiteral("id"), m_writer.addRelationship(presentationPart(), slideRelType("slide"), part));
    // The index among the sldId elements, not among all children.
    qsizetype seen = 0;
    for (qsizetype i = 0; i < slides.children.size(); ++i) {
        if (slides.children.at(i).element && seen++ == index) {
            slides.children.insert(i, {entry, QString()});
            return;
        }
    }
    slides.appendElement(entry);
}

void PackageEdit::addMaster(const QString &part)
{
    XmlElement &masters = list(u"sldMasterIdLst");
    XmlElement &entry = masters.appendElement(nsP(), QStringLiteral("sldMasterId"));
    set(entry, QStringLiteral("id"), QString::number(takeLayoutId()));
    entry.setAttribute(nsR(), QStringLiteral("id"), m_writer.addRelationship(presentationPart(), slideRelType("slideMaster"), part));
}

void PackageEdit::removeMaster(const QString &part)
{
    XmlElement *masters = m_presentation->child(nsP(), u"sldMasterIdLst");
    if (masters == nullptr) {
        return;
    }
    for (const Ooxml::Relationship &rel : m_writer.relationshipsOf(presentationPart())) {
        if (rel.target != part) {
            continue;
        }
        masters->children.removeIf([&rel](const Ooxml::XmlChild &c) {
            return c.element && c.element->attribute(nsR(), u"id") == rel.id;
        });
        m_writer.removeRelationship(presentationPart(), rel.id);
    }
}

QStringList PackageEdit::masterParts() const
{
    QStringList parts;
    const XmlElement *masters = m_presentation->child(nsP(), u"sldMasterIdLst");
    const QList<Ooxml::Relationship> rels = m_writer.relationshipsOf(presentationPart());
    for (const XmlElement *entry : masters != nullptr ? masters->elements(nsP(), u"sldMasterId") : QList<const XmlElement *>()) {
        for (const Ooxml::Relationship &rel : rels) {
            if (rel.id == entry->attribute(nsR(), u"id")) {
                parts.append(rel.target);
            }
        }
    }
    return parts;
}

quint32 PackageEdit::takeLayoutId()
{
    return m_nextLayoutId++;
}

void PackageEdit::commit()
{
    m_writer.addXmlPart(presentationPart(), *m_presentation, m_writer.contentTypeOf(presentationPart()));
}

QString PackageEdit::layoutOf(const QString &slidePart) const
{
    for (const Ooxml::Relationship &rel : m_writer.relationshipsOf(slidePart)) {
        if (rel.type == slideRelType("slideLayout")) {
            return rel.target;
        }
    }
    return {};
}

void PackageEdit::setLayout(const QString &slidePart, const QString &layoutPart)
{
    for (const Ooxml::Relationship &rel : m_writer.relationshipsOf(slidePart)) {
        if (rel.type == slideRelType("slideLayout")) {
            m_writer.retargetRelationship(slidePart, rel.id, layoutPart);
            return;
        }
    }
    m_writer.addRelationship(slidePart, slideRelType("slideLayout"), layoutPart);
}

// ---- placeholders --------------------------------------------------------------

bool PlaceholderRef::isFooter() const
{
    return type == u"dt" || type == u"ftr" || type == u"sldNum" || type == u"hdr";
}

bool PlaceholderRef::isTitle() const
{
    return type == u"title" || type == u"ctrTitle";
}

XmlElement *phElement(XmlElement &shape)
{
    for (const char16_t *wrapper : {u"nvSpPr", u"nvPicPr", u"nvGraphicFramePr", u"nvGrpSpPr", u"nvCxnSpPr"}) {
        XmlElement *nv = shape.child(nsP(), wrapper);
        XmlElement *nvPr = nv != nullptr ? nv->child(nsP(), u"nvPr") : nullptr;
        if (XmlElement *ph = nvPr != nullptr ? nvPr->child(nsP(), u"ph") : nullptr) {
            return ph;
        }
    }
    return nullptr;
}

PlaceholderRef placeholderOf(const XmlElement &shape)
{
    PlaceholderRef ref;
    XmlElement *ph = phElement(const_cast<XmlElement &>(shape));
    if (ph == nullptr) {
        return ref;
    }
    ref.type = ph->attr(u"type", QStringLiteral("obj"));
    ref.index = ph->hasAttribute(QString(), u"idx") ? ph->attr(u"idx").toLongLong() : -1;
    ref.size = ph->attr(u"sz");
    ref.orient = ph->attr(u"orient");
    for (const char16_t *wrapper : {u"nvSpPr", u"nvPicPr", u"nvGraphicFramePr"}) {
        if (const XmlElement *nv = shape.child(nsP(), wrapper)) {
            if (const XmlElement *cNvPr = nv->child(nsP(), u"cNvPr")) {
                ref.name = cNvPr->attr(u"name");
            }
        }
    }
    const XmlElement *spPr = shape.child(nsP(), u"spPr");
    if (const XmlElement *xfrm = spPr != nullptr ? spPr->child(nsA(), u"xfrm") : nullptr) {
        const XmlElement *off = xfrm->child(nsA(), u"off");
        const XmlElement *ext = xfrm->child(nsA(), u"ext");
        if (off != nullptr && ext != nullptr) {
            ref.rectEmu = QRectF(off->attr(u"x").toDouble(), off->attr(u"y").toDouble(), ext->attr(u"cx").toDouble(),
                                 ext->attr(u"cy").toDouble());
        }
    }
    return ref;
}

XmlElement *shapeTree(XmlElement &sld)
{
    XmlElement *cSld = sld.child(nsP(), u"cSld");
    return cSld != nullptr ? cSld->child(nsP(), u"spTree") : nullptr;
}

QList<PlaceholderRef> placeholdersOf(const XmlElement &part)
{
    QList<PlaceholderRef> out;
    const XmlElement *cSld = part.child(nsP(), u"cSld");
    const XmlElement *tree = cSld != nullptr ? cSld->child(nsP(), u"spTree") : nullptr;
    for (const XmlElement *shape : tree != nullptr ? tree->elements() : QList<const XmlElement *>()) {
        const PlaceholderRef ref = placeholderOf(*shape);
        if (!ref.type.isEmpty()) {
            out.append(ref);
        }
    }
    return out;
}

int matchPlaceholder(const PlaceholderRef &ph, const QList<PlaceholderRef> &candidates, const QList<int> &taken)
{
    const auto family = [](const PlaceholderRef &ref) {
        return ref.isTitle() ? 1 : ref.isFooter() ? 2 : 3;
    };
    const auto free = [&taken](int i) { return !taken.contains(i); };
    // The same index, of a compatible kind.
    if (ph.index >= 0) {
        for (int i = 0; i < candidates.size(); ++i) {
            if (free(i) && candidates.at(i).index == ph.index && family(candidates.at(i)) == family(ph)
                && (!ph.isFooter() || candidates.at(i).type == ph.type)) {
                return i;
            }
        }
    }
    // The same type (titles with titles).
    for (int i = 0; i < candidates.size(); ++i) {
        if (free(i) && (candidates.at(i).type == ph.type || (ph.isTitle() && candidates.at(i).isTitle()))) {
            return i;
        }
    }
    // Any content placeholder for content.
    if (family(ph) == 3) {
        for (int i = 0; i < candidates.size(); ++i) {
            if (free(i) && family(candidates.at(i)) == 3 && candidates.at(i).type != u"pic") {
                return i;
            }
        }
    }
    return -1;
}

namespace {

// PowerPoint's names for the placeholders it puts on a new slide.
QString placeholderName(const QString &type, quint32 id)
{
    static const QHash<QString, const char *> names{
        {QStringLiteral("title"), "Title"},
        {QStringLiteral("ctrTitle"), "Title"},
        {QStringLiteral("subTitle"), "Subtitle"},
        {QStringLiteral("body"), "Text Placeholder"},
        {QStringLiteral("obj"), "Content Placeholder"},
        {QStringLiteral("pic"), "Picture Placeholder"},
        {QStringLiteral("chart"), "Chart Placeholder"},
        {QStringLiteral("tbl"), "Table Placeholder"},
        {QStringLiteral("dgm"), "SmartArt Placeholder"},
        {QStringLiteral("media"), "Media Placeholder"},
        {QStringLiteral("clipArt"), "Clip Art Placeholder"},
        {QStringLiteral("dt"), "Date Placeholder"},
        {QStringLiteral("ftr"), "Footer Placeholder"},
        {QStringLiteral("sldNum"), "Slide Number Placeholder"}};
    return QStringLiteral("%1 %2").arg(QLatin1String(names.value(type, "Placeholder"))).arg(id);
}

} // namespace

XmlElementPtr emptyPlaceholder(const PlaceholderRef &ph, quint32 id)
{
    auto sp = make(nsP(), QStringLiteral("sp"));
    XmlElement &nv = sp->appendElement(nsP(), QStringLiteral("nvSpPr"));
    XmlElement &cNvPr = nv.appendElement(nsP(), QStringLiteral("cNvPr"));
    set(cNvPr, QStringLiteral("id"), QString::number(id));
    set(cNvPr, QStringLiteral("name"), placeholderName(ph.type, id - 1));
    set(nv.appendElement(nsP(), QStringLiteral("cNvSpPr")).appendElement(nsA(), QStringLiteral("spLocks")),
        QStringLiteral("noGrp"), QStringLiteral("1"));
    XmlElement &phElem = nv.appendElement(nsP(), QStringLiteral("nvPr")).appendElement(nsP(), QStringLiteral("ph"));
    if (ph.type != u"obj") {
        set(phElem, QStringLiteral("type"), ph.type);
    }
    if (!ph.orient.isEmpty()) {
        set(phElem, QStringLiteral("orient"), ph.orient);
    }
    if (!ph.size.isEmpty()) {
        set(phElem, QStringLiteral("sz"), ph.size);
    }
    if (ph.index >= 0) {
        set(phElem, QStringLiteral("idx"), QString::number(ph.index));
    }
    sp->appendElement(nsP(), QStringLiteral("spPr"));
    XmlElement &body = sp->appendElement(nsP(), QStringLiteral("txBody"));
    body.appendElement(nsA(), QStringLiteral("bodyPr"));
    body.appendElement(nsA(), QStringLiteral("lstStyle"));
    set(body.appendElement(nsA(), QStringLiteral("p")).appendElement(nsA(), QStringLiteral("endParaRPr")),
        QStringLiteral("lang"), QStringLiteral("en-US"));
    return sp;
}

XmlElementPtr newSlideRoot(const QList<XmlElementPtr> &shapes)
{
    auto sld = make(nsP(), QStringLiteral("sld"));
    XmlElement &tree = sld->appendElement(nsP(), QStringLiteral("cSld")).appendElement(nsP(), QStringLiteral("spTree"));
    XmlElement &nv = tree.appendElement(nsP(), QStringLiteral("nvGrpSpPr"));
    XmlElement &cNvPr = nv.appendElement(nsP(), QStringLiteral("cNvPr"));
    set(cNvPr, QStringLiteral("id"), QStringLiteral("1"));
    set(cNvPr, QStringLiteral("name"), QString());
    nv.appendElement(nsP(), QStringLiteral("cNvGrpSpPr"));
    nv.appendElement(nsP(), QStringLiteral("nvPr"));
    XmlElement &xfrm = tree.appendElement(nsP(), QStringLiteral("grpSpPr")).appendElement(nsA(), QStringLiteral("xfrm"));
    for (const auto &[name, a, b] : {std::tuple{"off", "x", "y"}, std::tuple{"ext", "cx", "cy"},
                                     std::tuple{"chOff", "x", "y"}, std::tuple{"chExt", "cx", "cy"}}) {
        XmlElement &e = xfrm.appendElement(nsA(), QLatin1String(name));
        set(e, QLatin1String(a), QStringLiteral("0"));
        set(e, QLatin1String(b), QStringLiteral("0"));
    }
    for (const XmlElementPtr &shape : shapes) {
        tree.appendElement(shape);
    }
    sld->appendElement(nsP(), QStringLiteral("clrMapOvr")).appendElement(nsA(), QStringLiteral("masterClrMapping"));
    renewCreationId(*sld);
    return sld;
}

quint32 largestShapeId(const XmlElement &tree)
{
    quint32 largest = 1;
    for (const XmlElement *shape : tree.elements()) {
        for (const char16_t *wrapper : {u"nvSpPr", u"nvPicPr", u"nvGraphicFramePr", u"nvGrpSpPr", u"nvCxnSpPr"}) {
            const XmlElement *nv = shape->child(nsP(), wrapper);
            if (const XmlElement *cNvPr = nv != nullptr ? nv->child(nsP(), u"cNvPr") : nullptr) {
                largest = qMax(largest, cNvPr->attr(u"id").toUInt());
            }
        }
        if (shape->is(nsP(), u"grpSp")) {
            largest = qMax(largest, largestShapeId(*shape));
        }
    }
    return largest;
}

void renewCreationId(XmlElement &sld)
{
    const QString value = QString::number((QRandomGenerator::global()->generate() & 0x7FFFFFFFu) | 1u);
    XmlElement *list = sld.child(nsP(), u"extLst");
    if (list != nullptr) {
        for (Ooxml::XmlChild &child : list->children) {
            if (XmlElement *id = child.element ? child.element->child(nsP14(), u"creationId") : nullptr) {
                set(*id, QStringLiteral("val"), value);
                return;
            }
        }
    } else {
        list = &sld.appendElement(nsP(), QStringLiteral("extLst"));
    }
    XmlElement &ext = list->appendElement(nsP(), QStringLiteral("ext"));
    set(ext, QStringLiteral("uri"), QString::fromUtf16(kCreationIdUri));
    set(ext.appendElement(nsP14(), QStringLiteral("creationId")), QStringLiteral("val"), value);
}

} // namespace QindaOffice::Show::Pptx
