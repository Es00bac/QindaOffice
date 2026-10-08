// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_writer.h"

#include "docx_properties.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"
#include "ooxml_properties.h"

#include <QSaveFile>
#include <QTextFrame>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
using Ooxml::XmlChild;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);
const QString kMC = QString::fromUtf16(Ooxml::Ns::MC);
const QString kMain = QStringLiteral("word/document.xml");
const QString kTypePrefix = QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.");

QString type(const char *suffix)
{
    return kTypePrefix + QString::fromLatin1(suffix);
}

// Microsoft extension vocabularies Word itself marks ignorable.
const QStringList &ignorableExtensions()
{
    static const QStringList list{
        QString::fromUtf16(Ooxml::Ns::W14), QString::fromUtf16(Ooxml::Ns::W15), QString::fromUtf16(Ooxml::Ns::WP14),
        QStringLiteral("http://schemas.microsoft.com/office/word/2015/wordml/symex"),
        QStringLiteral("http://schemas.microsoft.com/office/word/2016/wordml/cid"),
        QStringLiteral("http://schemas.microsoft.com/office/word/2018/wordml"),
        QStringLiteral("http://schemas.microsoft.com/office/word/2018/wordml/cex"),
        QStringLiteral("http://schemas.microsoft.com/office/word/2020/wordml/sdtdatahash"),
        QStringLiteral("http://schemas.microsoft.com/office/word/2023/wordml/word16du")};
    return list;
}

// Declares every Microsoft extension namespace the part uses as
// mc:Ignorable on its root, as Word does.
void markIgnorable(XmlElement &root)
{
    QSet<QString> used;
    std::function<void(const XmlElement &)> walk = [&](const XmlElement &e) {
        used.insert(e.ns);
        for (const Ooxml::XmlAttribute &a : e.attributes) {
            used.insert(a.ns);
        }
        for (const Ooxml::XmlChild &c : e.children) {
            if (c.element) {
                walk(*c.element);
            }
        }
    };
    walk(root);
    QStringList tokens;
    for (const QString &uri : ignorableExtensions()) {
        if (used.contains(uri)) {
            tokens.append(QLatin1Char('{') + uri + QLatin1Char('}'));
        }
    }
    root.removeAttribute(kMC, u"Ignorable");
    if (!tokens.isEmpty()) {
        root.setAttribute(kMC, QStringLiteral("Ignorable"), tokens.join(QLatin1Char(' ')));
    }
}

XmlElementPtr sourceXml(const Ooxml::Package &package, const QString &main, const char16_t *relType, QString *part)
{
    const auto rel = package.firstOfType(main, QString::fromUtf16(relType));
    if (!rel) {
        return nullptr;
    }
    *part = package.targetPart(main, *rel);
    return part->isEmpty() ? nullptr : package.xml(*part);
}

const QStringList &settingsOrder()
{
    // CT_Settings sequence (ECMA-376 Part 1 §17.15.1.78), abridged to the
    // elements Word writes; unknown ones keep their place.
    static const QStringList order{
        QStringLiteral("writeProtection"), QStringLiteral("view"), QStringLiteral("zoom"),
        QStringLiteral("removePersonalInformation"), QStringLiteral("removeDateAndTime"),
        QStringLiteral("doNotDisplayPageBoundaries"), QStringLiteral("displayBackgroundShape"),
        QStringLiteral("embedTrueTypeFonts"), QStringLiteral("embedSystemFonts"), QStringLiteral("saveSubsetFonts"),
        QStringLiteral("mirrorMargins"), QStringLiteral("gutterAtTop"), QStringLiteral("hideSpellingErrors"),
        QStringLiteral("hideGrammaticalErrors"), QStringLiteral("activeWritingStyle"), QStringLiteral("proofState"),
        QStringLiteral("formsDesign"), QStringLiteral("attachedTemplate"), QStringLiteral("linkStyles"),
        QStringLiteral("stylePaneFormatFilter"), QStringLiteral("stylePaneSortMethod"), QStringLiteral("documentType"),
        QStringLiteral("mailMerge"), QStringLiteral("revisionView"), QStringLiteral("trackRevisions"),
        QStringLiteral("doNotTrackMoves"), QStringLiteral("doNotTrackFormatting"), QStringLiteral("documentProtection"),
        QStringLiteral("autoFormatOverride"), QStringLiteral("styleLockTheme"), QStringLiteral("styleLockQFSet"),
        QStringLiteral("defaultTabStop"), QStringLiteral("autoHyphenation"), QStringLiteral("consecutiveHyphenLimit"),
        QStringLiteral("hyphenationZone"), QStringLiteral("doNotHyphenateCaps"), QStringLiteral("showEnvelope"),
        QStringLiteral("summaryLength"), QStringLiteral("clickAndTypeStyle"), QStringLiteral("defaultTableStyle"),
        QStringLiteral("evenAndOddHeaders"), QStringLiteral("bookFoldRevPrinting"), QStringLiteral("bookFoldPrinting"),
        QStringLiteral("bookFoldPrintingSheets"), QStringLiteral("drawingGridHorizontalSpacing"),
        QStringLiteral("drawingGridVerticalSpacing"), QStringLiteral("displayHorizontalDrawingGridEvery"),
        QStringLiteral("displayVerticalDrawingGridEvery"), QStringLiteral("doNotUseMarginsForDrawingGridOrigin"),
        QStringLiteral("drawingGridHorizontalOrigin"), QStringLiteral("drawingGridVerticalOrigin"),
        QStringLiteral("doNotShadeFormData"), QStringLiteral("noPunctuationKerning"),
        QStringLiteral("characterSpacingControl"), QStringLiteral("printTwoOnOne"),
        QStringLiteral("strictFirstAndLastChars"), QStringLiteral("noLineBreaksAfter"),
        QStringLiteral("noLineBreaksBefore"), QStringLiteral("savePreviewPicture"),
        QStringLiteral("doNotValidateAgainstSchema"), QStringLiteral("saveInvalidXml"),
        QStringLiteral("ignoreMixedContent"), QStringLiteral("alwaysShowPlaceholderText"),
        QStringLiteral("doNotDemarcateInvalidXml"), QStringLiteral("saveXmlDataOnly"),
        QStringLiteral("useXSLTWhenSaving"), QStringLiteral("saveThroughXslt"), QStringLiteral("showXMLTags"),
        QStringLiteral("alwaysMergeEmptyNamespace"), QStringLiteral("updateFields"),
        QStringLiteral("hdrShapeDefaults"), QStringLiteral("footnotePr"), QStringLiteral("endnotePr"),
        QStringLiteral("compat"), QStringLiteral("docVars"), QStringLiteral("rsids"), QStringLiteral("mathPr"),
        QStringLiteral("attachedSchema"), QStringLiteral("themeFontLang"), QStringLiteral("clrSchemeMapping"),
        QStringLiteral("doNotIncludeSubdocsInStats"), QStringLiteral("doNotAutoCompressPictures"),
        QStringLiteral("forceUpgrade"), QStringLiteral("captions"), QStringLiteral("readModeInkLockDown"),
        QStringLiteral("smartTagType"), QStringLiteral("schemaLibrary"), QStringLiteral("shapeDefaults"),
        QStringLiteral("doNotEmbedSmartTags"), QStringLiteral("decimalSymbol"), QStringLiteral("listSeparator")};
    return order;
}

void sortSettings(XmlElement &settings)
{
    const QStringList &order = settingsOrder();
    QList<std::pair<qsizetype, Ooxml::XmlChild>> ranked;
    qsizetype last = -1;
    for (const Ooxml::XmlChild &c : std::as_const(settings.children)) {
        const qsizetype at = c.element && c.element->ns == kW ? order.indexOf(c.element->name) : -1;
        last = at >= 0 ? at : last;
        ranked.append({at >= 0 ? at : last, c});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    settings.children.clear();
    for (const auto &entry : ranked) {
        settings.children.append(entry.second);
    }
}

XmlElementPtr separatorNote(const QString &kind, const QString &id)
{
    auto note = std::make_shared<XmlElement>(kW, QStringLiteral("footnote"));
    note->setAttribute(kW, QStringLiteral("type"), kind);
    note->setAttribute(kW, QStringLiteral("id"), id);
    XmlElement &p = note->appendElement(kW, QStringLiteral("p"));
    XmlElement &spacing = p.appendElement(kW, QStringLiteral("pPr")).appendElement(kW, QStringLiteral("spacing"));
    spacing.setAttribute(kW, QStringLiteral("after"), QStringLiteral("0"));
    spacing.setAttribute(kW, QStringLiteral("line"), QStringLiteral("240"));
    spacing.setAttribute(kW, QStringLiteral("lineRule"), QStringLiteral("auto"));
    p.appendElement(kW, QStringLiteral("r")).appendElement(kW, kind);
    return note;
}

} // namespace

QByteArray docxBytes(const QTextDocument &body, const WordParts &parts, const WriteOptions &options, QString *error)
{
    detail::WriteEnv env;
    env.parts = &parts;
    const std::shared_ptr<const Ooxml::Package> &source = parts.source;
    XmlElementPtr sourceStyles;
    XmlElementPtr sourceNumbering;
    XmlElementPtr sourceSettings;
    QString sourceSettingsPart;
    if (source) {
        env.sourceMain = source->mainPart();
        env.copier = std::make_unique<Ooxml::RelationshipCopier>(source, &env.writer);
        QString part;
        sourceStyles = sourceXml(*source, env.sourceMain, Ooxml::RelType::Styles, &part);
        env.sourceStyles = StyleSheet::parse(sourceStyles.get());
        sourceNumbering = sourceXml(*source, env.sourceMain, Ooxml::RelType::Numbering, &part);
        sourceSettings = sourceXml(*source, env.sourceMain, Ooxml::RelType::Settings, &sourceSettingsPart);
        if (const auto rel = source->firstOfType(env.sourceMain, QString::fromUtf16(Ooxml::RelType::Footnotes))) {
            env.sourceFootnotesPart = source->targetPart(env.sourceMain, *rel);
        }
    }
    env.numbering = std::make_unique<detail::NumberingBuilder>(sourceNumbering);
    env.layoutChanged = !source || parts.sourceSectionXml.isEmpty() || !(parts.layout == parts.importedLayout);
    env.headersChanged = !source || parts.differentFirstPage != parts.importedDifferentFirstPage;
    for (const auto *list : {&parts.headers, &parts.footers}) {
        for (const HeaderFooter &hf : *list) {
            if (hf.document && (hf.document->isModified() || hf.sourcePart.isEmpty())) {
                env.headersChanged = true;
            }
        }
    }

    // Headers and footers first, so the section hook can point at them.
    struct Reference {
        bool header;
        QString slot;
        QString id;
    };
    QList<Reference> references;
    if (env.headersChanged) {
        static const char *kSlots[] = {"default", "first", "even"};
        for (int s = 0; s < kHeaderFooterSlots; ++s) {
            for (const bool header : {true, false}) {
                const HeaderFooter &hf = header ? parts.headers[size_t(s)] : parts.footers[size_t(s)];
                if (!hf.document || (hf.document->isEmpty() && hf.sourcePart.isEmpty())) {
                    continue;
                }
                QString partName;
                if (!hf.document->isModified() && !hf.sourcePart.isEmpty() && env.copier) {
                    partName = env.copier->copyPart(hf.sourcePart);
                } else {
                    partName = env.writer.uniquePartName(header ? QStringLiteral("word/header")
                                                                : QStringLiteral("word/footer"),
                                                         QStringLiteral(".xml"));
                    XmlElement root(kW, header ? QStringLiteral("hdr") : QStringLiteral("ftr"));
                    detail::BodyWriter(env, hf.document.get(), partName).writeFrame(hf.document->rootFrame(), root);
                    markIgnorable(root);
                    env.writer.addXmlPart(partName, root, type(header ? "header+xml" : "footer+xml"));
                }
                const QString rel = env.writer.addRelationship(
                    kMain, QString::fromUtf16(header ? Ooxml::RelType::Header : Ooxml::RelType::Footer), partName);
                references.append({header, QString::fromLatin1(kSlots[s]), rel});
            }
        }
    }
    env.sectionHook = [&](XmlElement &sectPr, const QString &part) {
        if (env.copier) {
            if (env.headersChanged) {
                sectPr.removeChildren(kW, u"headerReference");
                sectPr.removeChildren(kW, u"footerReference");
            }
            env.copier->rewrite(sectPr, env.sourceMain, part);
        }
        if (env.headersChanged) {
            sectPr.removeChildren(kW, u"headerReference");
            sectPr.removeChildren(kW, u"footerReference");
            for (const Reference &ref : std::as_const(references)) {
                XmlElement &e = sectPr.appendElement(kW, ref.header ? QStringLiteral("headerReference")
                                                                    : QStringLiteral("footerReference"));
                e.setAttribute(kW, QStringLiteral("type"), ref.slot);
                e.setAttribute(kR, QStringLiteral("id"), ref.id);
            }
            sectPr.removeChildren(kW, u"titlePg");
            if (parts.differentFirstPage) {
                sectPr.appendElement(kW, QStringLiteral("titlePg"));
            }
        }
        if (env.layoutChanged) {
            const PageLayout &l = parts.layout;
            XmlElement &size = detail::wChild(sectPr, QStringLiteral("pgSz"));
            size.attributes.clear();
            size.setAttribute(kW, QStringLiteral("w"), detail::twips(l.pageSize.width()));
            size.setAttribute(kW, QStringLiteral("h"), detail::twips(l.pageSize.height()));
            if (l.landscape()) {
                size.setAttribute(kW, QStringLiteral("orient"), QStringLiteral("landscape"));
            }
            XmlElement &margin = detail::wChild(sectPr, QStringLiteral("pgMar"));
            margin.attributes.clear();
            margin.setAttribute(kW, QStringLiteral("top"), detail::twips(l.margins.top()));
            margin.setAttribute(kW, QStringLiteral("right"), detail::twips(l.margins.right()));
            margin.setAttribute(kW, QStringLiteral("bottom"), detail::twips(l.margins.bottom()));
            margin.setAttribute(kW, QStringLiteral("left"), detail::twips(l.margins.left()));
            margin.setAttribute(kW, QStringLiteral("header"), detail::twips(l.headerDistance));
            margin.setAttribute(kW, QStringLiteral("footer"), detail::twips(l.footerDistance));
            margin.setAttribute(kW, QStringLiteral("gutter"), detail::twips(l.gutter));
        }
        detail::sortChildren(sectPr);
    };

    // The body.
    XmlElement document(kW, QStringLiteral("document"));
    XmlElement &bodyElement = document.appendElement(kW, QStringLiteral("body"));
    detail::BodyWriter(env, &body, kMain).writeFrame(body.rootFrame(), bodyElement);
    XmlElementPtr sectPr = Ooxml::parseFragment(parts.sourceSectionXml);
    if (!sectPr) {
        sectPr = std::make_shared<XmlElement>(kW, QStringLiteral("sectPr"));
        XmlElement &cols = sectPr->appendElement(kW, QStringLiteral("cols"));
        cols.setAttribute(kW, QStringLiteral("space"), QStringLiteral("720"));
        sectPr->appendElement(kW, QStringLiteral("docGrid"))
            .setAttribute(kW, QStringLiteral("linePitch"), QStringLiteral("360"));
    }
    env.sectionHook(*sectPr, kMain);
    bodyElement.appendElement(sectPr);

    // Footnotes the body references, in reference order.
    bool wroteFootnotes = false;
    if (!env.footnoteOrder.isEmpty()) {
        XmlElement notes(kW, QStringLiteral("footnotes"));
        notes.appendElement(separatorNote(QStringLiteral("separator"), QStringLiteral("-1")));
        notes.appendElement(separatorNote(QStringLiteral("continuationSeparator"), QStringLiteral("0")));
        for (const QString &noteId : std::as_const(env.footnoteOrder)) {
            const QString id = QString::number(env.footnoteIds.value(noteId));
            const QString text = parts.footnotes.value(noteId);
            XmlElementPtr preserved = parts.footnoteImportedText.contains(noteId)
                    && parts.footnoteImportedText.value(noteId) == text
                ? Ooxml::parseFragment(parts.footnoteXml.value(noteId))
                : nullptr;
            if (preserved) {
                preserved->setAttribute(kW, QStringLiteral("id"), id);
                if (env.copier) {
                    env.copier->rewrite(*preserved, env.sourceFootnotesPart, QStringLiteral("word/footnotes.xml"));
                }
                notes.appendElement(preserved);
                continue;
            }
            XmlElement &note = notes.appendElement(kW, QStringLiteral("footnote"));
            note.setAttribute(kW, QStringLiteral("id"), id);
            XmlElement &p = note.appendElement(kW, QStringLiteral("p"));
            detail::setChildVal(p.appendElement(kW, QStringLiteral("pPr")), QStringLiteral("pStyle"),
                                QStringLiteral("FootnoteText"));
            XmlElement &ref = p.appendElement(kW, QStringLiteral("r"));
            detail::setChildVal(ref.appendElement(kW, QStringLiteral("rPr")), QStringLiteral("rStyle"),
                                QStringLiteral("FootnoteReference"));
            ref.appendElement(kW, QStringLiteral("footnoteRef"));
            XmlElement &t = p.appendElement(kW, QStringLiteral("r")).appendElement(kW, QStringLiteral("t"));
            t.setAttribute(QString::fromUtf16(Ooxml::Ns::XML), QStringLiteral("space"), QStringLiteral("preserve"));
            t.appendText(QLatin1Char(' ') + text);
            env.usedStyles.insert(QStringLiteral("FootnoteText"));
            env.usedStyles.insert(QStringLiteral("FootnoteReference"));
        }
        markIgnorable(notes);
        env.writer.addXmlPart(QStringLiteral("word/footnotes.xml"), notes, type("footnotes+xml"));
        env.writer.addRelationship(kMain, QString::fromUtf16(Ooxml::RelType::Footnotes),
                                   QStringLiteral("word/footnotes.xml"));
        wroteFootnotes = true;
    }

    // Parts carried over verbatim from the source document.
    bool carriedEndnotes = false;
    if (source && env.copier) {
        static const QStringList kCarry{
            QString::fromUtf16(Ooxml::RelType::Theme), QString::fromUtf16(Ooxml::RelType::FontTable),
            QString::fromUtf16(Ooxml::RelType::WebSettings), QString::fromUtf16(Ooxml::RelType::Endnotes),
            QString::fromUtf16(Ooxml::RelType::Comments),
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml"),
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/glossaryDocument"),
            QStringLiteral("http://schemas.microsoft.com/office/2007/relationships/stylesWithEffects"),
            QStringLiteral("http://schemas.microsoft.com/office/2011/relationships/people"),
            QStringLiteral("http://schemas.microsoft.com/office/2011/relationships/commentsExtended"),
            QStringLiteral("http://schemas.microsoft.com/office/2016/09/relationships/commentsIds"),
            QStringLiteral("http://schemas.microsoft.com/office/2018/08/relationships/commentsExtensible")};
        static const QString kVba = QStringLiteral("http://schemas.microsoft.com/office/2006/relationships/vbaProject");
        const bool macros = options.kind == WriteOptions::MacroDocument || options.kind == WriteOptions::MacroTemplate;
        for (const Ooxml::Relationship &rel : source->relationships(env.sourceMain)) {
            if (rel.external || (!kCarry.contains(rel.type) && !(macros && rel.type == kVba))) {
                continue;
            }
            const QString target = source->targetPart(env.sourceMain, rel);
            if (target.isEmpty()) {
                continue;
            }
            env.writer.addRelationship(kMain, rel.type, env.copier->copyPart(target));
            carriedEndnotes = carriedEndnotes || rel.type == QString::fromUtf16(Ooxml::RelType::Endnotes);
        }
        for (const Ooxml::Relationship &rel : source->relationships(QString())) {
            if (!rel.external && rel.type.endsWith(u"/custom-properties")) {
                env.writer.addRelationship(QString(), rel.type, env.copier->copyPart(source->targetPart(QString(), rel)));
            }
        }
    }

    // Styles and numbering, now that every reference is known.
    XmlElementPtr styles = detail::stylesPart(env, body.defaultFont(), sourceStyles);
    env.writer.addXmlPart(QStringLiteral("word/styles.xml"), *styles, type("styles+xml"));
    env.writer.addRelationship(kMain, QString::fromUtf16(Ooxml::RelType::Styles), QStringLiteral("word/styles.xml"));
    if (XmlElementPtr numbering = env.numbering->result()) {
        markIgnorable(*numbering);
        env.writer.addXmlPart(QStringLiteral("word/numbering.xml"), *numbering, type("numbering+xml"));
        env.writer.addRelationship(kMain, QString::fromUtf16(Ooxml::RelType::Numbering),
                                   QStringLiteral("word/numbering.xml"));
    }

    // Settings: the source's, kept consistent with what was written.
    XmlElementPtr settings = sourceSettings ? sourceSettings->clone() : nullptr;
    if (!settings) {
        settings = std::make_shared<XmlElement>(kW, QStringLiteral("settings"));
        settings->appendElement(kW, QStringLiteral("zoom")).setAttribute(kW, QStringLiteral("percent"), QStringLiteral("100"));
        detail::setChildVal(*settings, QStringLiteral("defaultTabStop"), detail::twips(parts.defaultTabStop));
        detail::setChildVal(*settings, QStringLiteral("characterSpacingControl"), QStringLiteral("doNotCompress"));
        XmlElement &compat = settings->appendElement(kW, QStringLiteral("compat"));
        XmlElement &mode = compat.appendElement(kW, QStringLiteral("compatSetting"));
        mode.setAttribute(kW, QStringLiteral("name"), QStringLiteral("compatibilityMode"));
        mode.setAttribute(kW, QStringLiteral("uri"), QStringLiteral("http://schemas.microsoft.com/office/word"));
        mode.setAttribute(kW, QStringLiteral("val"), QStringLiteral("15"));
    } else if (env.copier) {
        env.copier->rewrite(*settings, sourceSettingsPart, QStringLiteral("word/settings.xml"));
    }
    settings->removeChildren(kW, u"footnotePr");
    if (wroteFootnotes) {
        XmlElement &pr = settings->appendElement(kW, QStringLiteral("footnotePr"));
        pr.appendElement(kW, QStringLiteral("footnote")).setAttribute(kW, QStringLiteral("id"), QStringLiteral("-1"));
        pr.appendElement(kW, QStringLiteral("footnote")).setAttribute(kW, QStringLiteral("id"), QStringLiteral("0"));
    }
    if (!carriedEndnotes) {
        settings->removeChildren(kW, u"endnotePr");
    }
    settings->removeChildren(kW, u"evenAndOddHeaders");
    if (parts.differentOddEven) {
        settings->appendElement(kW, QStringLiteral("evenAndOddHeaders"));
    }
    sortSettings(*settings);
    markIgnorable(*settings);
    env.writer.addXmlPart(QStringLiteral("word/settings.xml"), *settings, type("settings+xml"));
    env.writer.addRelationship(kMain, QString::fromUtf16(Ooxml::RelType::Settings), QStringLiteral("word/settings.xml"));

    markIgnorable(document);
    static const char *kMainTypes[] = {
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml",
        "application/vnd.openxmlformats-officedocument.wordprocessingml.template.main+xml",
        "application/vnd.ms-word.document.macroEnabled.main+xml",
        "application/vnd.ms-word.template.macroEnabledTemplate.main+xml"};
    env.writer.addXmlPart(kMain, document, QString::fromLatin1(kMainTypes[options.kind]));
    env.writer.addRelationship(QString(), QString::fromUtf16(Ooxml::RelType::OfficeDocument), kMain);
    Ooxml::CoreProperties properties = parts.properties;
    properties.modified = QDateTime::currentDateTimeUtc();
    if (!properties.created.isValid()) {
        properties.created = properties.modified;
    }
    Ooxml::writeProperties(env.writer, properties, options.application, options.pageCount, options.wordCount);
    return env.writer.toZip(error);
}

bool writeDocx(const QString &path, const QTextDocument &body, const WordParts &parts, const WriteOptions &options,
               QString *error)
{
    const QByteArray bytes = docxBytes(body, parts, options, error);
    if (bytes.isEmpty()) {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Docx
