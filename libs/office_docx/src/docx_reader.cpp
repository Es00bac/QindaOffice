// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_reader.h"

#include "docx_format.h"
#include "docx_reader_p.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QTextOption>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);

XmlElementPtr relatedXml(const Ooxml::Package &package, const QString &source, const char16_t *type,
                         QString *partName = nullptr)
{
    const auto rel = package.firstOfType(source, QString::fromUtf16(type));
    if (!rel) {
        return nullptr;
    }
    const QString part = package.targetPart(source, *rel);
    if (partName != nullptr) {
        *partName = part;
    }
    return part.isEmpty() ? nullptr : package.xml(part);
}

// A footnote's text as QindaWrite stores it: its paragraphs, plain.
QString noteText(const XmlElement &note)
{
    QStringList paragraphs;
    for (const XmlElement *p : note.elements(kW, u"p")) {
        QString line;
        std::function<void(const XmlElement &)> walk = [&](const XmlElement &e) {
            for (const XmlElement *c : e.elements()) {
                if (c->ns != kW) {
                    continue;
                }
                if (c->name == u"t") {
                    line += c->text();
                } else if (c->name == u"tab") {
                    line += QLatin1Char(' ');
                } else if (c->name != u"instrText" && c->name != u"rPr" && c->name != u"pPr") {
                    walk(*c);
                }
            }
        };
        walk(*p);
        paragraphs.append(line);
    }
    return paragraphs.join(QLatin1Char(' ')).trimmed();
}

void readLayout(const XmlElement *sectPr, PageLayout &layout)
{
    if (sectPr == nullptr) {
        return;
    }
    if (const XmlElement *pgSz = sectPr->child(kW, u"pgSz")) {
        const qreal w = Units::parseTwips(wAttr(pgSz, u"w"), 12240);
        const qreal h = Units::parseTwips(wAttr(pgSz, u"h"), 15840);
        if (w > 0 && h > 0) {
            layout.pageSize = QSizeF(Units::twipsToPx(w), Units::twipsToPx(h));
        }
    }
    if (const XmlElement *pgMar = sectPr->child(kW, u"pgMar")) {
        auto px = [pgMar](QStringView name, qreal fallback) {
            return Units::twipsToPx(qAbs(Units::parseTwips(wAttr(pgMar, name), fallback)));
        };
        layout.margins = QMarginsF(px(u"left", 1440), px(u"top", 1440), px(u"right", 1440), px(u"bottom", 1440));
        layout.headerDistance = px(u"header", 720);
        layout.footerDistance = px(u"footer", 720);
        layout.gutter = px(u"gutter", 0);
    }
}

QString describe(const QMap<QString, int> &approximations)
{
    QStringList parts;
    for (auto it = approximations.cbegin(); it != approximations.cend(); ++it) {
        parts.append(it.value() == 1 ? QStringLiteral("1 %1").arg(it.key())
                                     : QStringLiteral("%1 %2s").arg(it.value()).arg(it.key()));
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

bool isWordPackage(const Ooxml::Package &package)
{
    const QString main = package.mainPart();
    if (main.isEmpty()) {
        return false;
    }
    const QString type = package.contentType(main);
    return type.contains(u"wordprocessingml") || type.contains(u"ms-word");
}

bool readDocx(const QString &path, QTextDocument *body, WordParts *parts, QString *error)
{
    QString loadError;
    const auto package = Ooxml::Package::load(path, &loadError);
    if (!package) {
        if (error != nullptr) {
            *error = loadError;
        }
        return false;
    }
    return readDocx(package, body, parts, error);
}

bool readDocx(std::shared_ptr<const Ooxml::Package> package, QTextDocument *body, WordParts *parts, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!package || !isWordPackage(*package)) {
        return fail(QStringLiteral("not a Word document"));
    }
    const QString main = package->mainPart();
    QString xmlError;
    const XmlElementPtr root = package->xml(main, &xmlError);
    const XmlElement *bodyElement = root ? root->child(kW, u"body") : nullptr;
    if (bodyElement == nullptr) {
        return fail(xmlError.isEmpty() ? QStringLiteral("the document has no body") : xmlError);
    }

    *parts = WordParts();
    parts->source = package;
    parts->properties = Ooxml::readCoreProperties(*package);

    detail::ReadEnv env;
    env.package = package;
    env.parts = parts;
    const XmlElementPtr stylesRoot = relatedXml(*package, main, Ooxml::RelType::Styles);
    env.styles = StyleSheet::parse(stylesRoot.get());
    const XmlElementPtr numberingRoot = relatedXml(*package, main, Ooxml::RelType::Numbering);
    env.numbering = Numbering::parse(numberingRoot.get());
    if (const XmlElementPtr themeRoot = relatedXml(*package, main, Ooxml::RelType::Theme)) {
        env.theme = Ooxml::parseTheme(*themeRoot);
    }
    const XmlElementPtr settings = relatedXml(*package, main, Ooxml::RelType::Settings);
    if (settings) {
        const QString tab = childVal(settings.get(), u"defaultTabStop");
        if (!tab.isEmpty() && Units::parseTwips(tab) > 0) {
            parts->defaultTabStop = Units::twipsToPx(Units::parseTwips(tab));
        }
        parts->differentOddEven = onOff(settings.get(), u"evenAndOddHeaders");
    }
    QString notesPart;
    if (const XmlElementPtr notes = relatedXml(*package, main, Ooxml::RelType::Footnotes, &notesPart)) {
        for (const XmlElement *note : notes->elements(kW, u"footnote")) {
            const QString type = wAttr(note, u"type");
            if (!type.isEmpty() && type != u"normal") {
                continue; // separators
            }
            const QString id = wAttr(note, u"id");
            env.footnoteText.insert(id, noteText(*note));
            parts->footnoteXml.insert(QStringLiteral("docx-") + id, Ooxml::serializeFragment(*note));
        }
    }

    // Body defaults: the document's base font and tab stop.
    const XmlElementPtr baseRun = env.styles.paragraphRunProperties(QString());
    const QTextCharFormat baseFormat = charFormatFromRPr(baseRun.get(), env.theme);
    auto prepare = [&](QTextDocument *document) {
        QFont font = document->defaultFont();
        if (baseFormat.hasProperty(QTextFormat::FontFamilies)) {
            const QStringList families = baseFormat.fontFamilies().toStringList();
            if (!families.isEmpty()) {
                font.setFamilies(families);
            }
        }
        font.setPointSizeF(baseFormat.fontPointSize());
        document->setDefaultFont(font);
        QTextOption option = document->defaultTextOption();
        option.setTabStopDistance(parts->defaultTabStop);
        document->setDefaultTextOption(option);
    };

    const bool undo = body->isUndoRedoEnabled();
    body->setUndoRedoEnabled(false);
    body->clear();
    prepare(body);
    detail::BodyReader reader(env, body, main);
    reader.readDocument(*bodyElement);

    // Page layout and headers come from the first section; a later section
    // that names a header slot the first one lacks lends it (Word inherits
    // header slots section to section).
    const QList<const XmlElement *> &sections = reader.sections();
    if (!sections.isEmpty()) {
        readLayout(sections.first(), parts->layout);
        parts->differentFirstPage = onOff(sections.first(), u"titlePg");
        if (const XmlElement *closing = bodyElement->child(kW, u"sectPr")) {
            parts->sourceSectionXml = Ooxml::serializeFragment(*closing);
        }
    }
    parts->importedLayout = parts->layout;
    parts->importedDifferentFirstPage = parts->differentFirstPage;
    for (const XmlElement *sectPr : sections) {
        for (const XmlElement *ref : sectPr->elements()) {
            const bool isHeader = ref->name == u"headerReference";
            if (!isHeader && ref->name != u"footerReference") {
                continue;
            }
            const QString type = wAttr(ref, u"type", QStringLiteral("default"));
            const HeaderFooterSlot slot = type == u"first" ? HeaderFooterSlot::First
                : type == u"even"                          ? HeaderFooterSlot::Even
                                                           : HeaderFooterSlot::Default;
            HeaderFooter &target = isHeader ? parts->header(slot) : parts->footer(slot);
            if (target.document) {
                continue;
            }
            const auto rel = package->relationship(main, ref->attribute(kR, u"id"));
            const QString part = rel ? package->targetPart(main, *rel) : QString();
            const XmlElementPtr content = part.isEmpty() ? nullptr : package->xml(part);
            if (!content) {
                continue;
            }
            target.document = std::make_unique<QTextDocument>();
            target.document->setUndoRedoEnabled(false);
            prepare(target.document.get());
            detail::BodyReader headerReader(env, target.document.get(), part);
            headerReader.readDocument(*content);
            target.document->setUndoRedoEnabled(true);
            target.document->setModified(false);
            target.sourcePart = part;
        }
    }
    if (!env.approximations.isEmpty()) {
        parts->warnings.append(QStringLiteral("Shown approximately and kept unchanged on save: %1.")
                                   .arg(describe(env.approximations)));
    }
    body->setUndoRedoEnabled(undo);
    body->setModified(false);
    return true;
}

} // namespace QindaOffice::Docx
