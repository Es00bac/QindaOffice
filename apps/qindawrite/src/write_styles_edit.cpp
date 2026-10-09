// SPDX-License-Identifier: GPL-3.0-or-later
// StylesController's definition commands (write_styles.h): New Style and
// Modify Style (the StyleDraft's OK), Update Style to Match Selection and
// Delete Style — each the styles part swapped and the text re-derived in
// one undo step.

#include "write_styles.h"

#include "docx_list_edit.h"
#include "docx_list_labels.h"
#include "docx_lists.h"
#include "docx_properties.h"
#include "docx_restyle.h"
#include "docx_style_edit.h"
#include "ooxml_namespaces.h"
#include "page_text_control.h"
#include "write_document.h"
#include "write_selection.h"
#include "write_style_draft.h"

#include <QTextBlock>
#include <QTextCursor>

namespace QindaOffice::Apps::Write {

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

// A linked pair's two halves name each other (w:link), as Word writes them.
void link(Ooxml::XmlElement &style, const QString &partner)
{
    style.removeChildren(kW, u"link");
    Ooxml::XmlElement &element = style.insertElementBefore(std::make_shared<Ooxml::XmlElement>(kW, QStringLiteral("link")),
                                                           {u"autoRedefine", u"hidden", u"uiPriority", u"semiHidden",
                                                            u"unhideWhenUsed", u"qFormat", u"locked", u"rsid", u"pPr", u"rPr"});
    element.setAttribute(kW, QStringLiteral("val"), partner);
}

// The paragraphs of the body whose style is `id`.
QList<QTextBlock> paragraphsOf(const QTextDocument &body, const Docx::DocumentStyles &styles, const QString &id)
{
    QList<QTextBlock> out;
    for (QTextBlock block = body.begin(); block.isValid(); block = block.next()) {
        if (!block.blockFormat().boolProperty(Docx::kParagraphSynthetic) && styles.paragraphStyleOf(block) == id) {
            out.append(block);
        }
    }
    return out;
}

} // namespace

void StylesController::newStyle()
{
    if (!onBody()) {
        return;
    }
    m_draft->loadSelection(m_control->textCursor());
    emit styleDialogRequested();
}

bool StylesController::modify(const QString &id)
{
    if (!onBody() || !m_draft->loadStyle(id)) {
        return false;
    }
    emit styleDialogRequested();
    return true;
}

bool StylesController::commitDraft()
{
    if (!onBody() || m_draft->name().trimmed().isEmpty()) {
        return false;
    }
    const StyleDraft &d = *m_draft;
    const Ooxml::XmlElementPtr pPr = d.paragraphMarkup();
    const Ooxml::XmlElementPtr rPr = d.runMarkup();
    return command([&](QTextCursor &cursor) {
        const Ooxml::XmlElementPtr original = m_document->stylesRoot();
        const Docx::StyleSheet before = Docx::StyleSheet::parse(original.get());
        if (d.mode() == u"new") {
            const QString id = Docx::newStyleId(*original, d.name());
            const bool character = d.type() == u"character";
            Ooxml::XmlElementPtr style = Docx::makeStyle(character ? QStringLiteral("character") : QStringLiteral("paragraph"), id,
                                                         d.name(), d.basedOn(), d.next().isEmpty() ? id : d.next(),
                                                         d.quick(), pPr, rPr);
            Ooxml::XmlElementPtr root = Docx::withStyle(*original, style);
            if (d.type() == u"linked") {
                const QString partner = Docx::newStyleId(*root, d.name() + QStringLiteral(" Char"));
                Ooxml::XmlElementPtr half = Docx::makeStyle(QStringLiteral("character"), partner, d.name() + QStringLiteral(" Char"),
                                                            QString(), QString(), false, nullptr, rPr);
                link(*style, partner);
                link(*half, id);
                root = Docx::withStyle(*Docx::withStyle(*original, style), half);
            }
            commitRoot(root, original);
            const Docx::DocumentStyles styles(m_document->parts());
            if (character) {
                QTextCursor runs = cursor;
                if (!runs.hasSelection()) {
                    runs.select(QTextCursor::WordUnderCursor);
                }
                styles.applyCharacter(runs, id);
            } else {
                for (const QTextBlock &block : selectedBlocks(cursor)) {
                    styles.apply(block, id);
                }
            }
            return true;
        }
        // Modify: the style's own markup now, everything else it says kept.
        const QString id = d.styleId();
        const Ooxml::XmlElementPtr element = Docx::styleElement(*original, id);
        if (!element) {
            return false;
        }
        Ooxml::XmlElementPtr paragraphMarkup = pPr;
        const QList<QTextBlock> users = paragraphsOf(*m_document->textDocument(), Docx::DocumentStyles(m_document->parts()), id);
        if (d.numbering() == u"none" && paragraphMarkup) {
            paragraphMarkup->removeChildren(kW, u"numPr");
            for (const QTextBlock &block : users) {
                Docx::removeFromList(block, false);
            }
        } else if (d.numbering() == u"bullet" || d.numbering() == u"number") {
            // Format ▸ Numbering: the style's paragraphs become one list,
            // and the style says so (w:numPr), as Word's list styles do.
            const QString definition = d.numbering() == u"bullet"
                ? Docx::bulletDefinition(QString(QChar(0xF0B7)), QStringLiteral("Symbol"))
                : Docx::numberingDefinition(QStringLiteral("decimal"), QStringLiteral("%1."));
            const int numId = Docx::makeList(users, definition, 0, false);
            if (!paragraphMarkup) {
                paragraphMarkup = std::make_shared<Ooxml::XmlElement>(kW, QStringLiteral("pPr"));
            }
            paragraphMarkup->removeChildren(kW, u"numPr");
            Ooxml::XmlElement &numPr = paragraphMarkup->insertElementBefore(
                std::make_shared<Ooxml::XmlElement>(kW, QStringLiteral("numPr")),
                {u"suppressLineNumbers", u"pBdr", u"shd", u"tabs", u"suppressAutoHyphens", u"spacing", u"ind", u"jc",
                 u"outlineLvl"});
            numPr.appendElement(kW, QStringLiteral("numId")).setAttribute(kW, QStringLiteral("val"), QString::number(numId));
        }
        const QString next = d.type() == u"character" ? QString() : (d.next().isEmpty() ? id : d.next());
        const Ooxml::XmlElementPtr edited = Docx::editedStyle(*element, d.wordName(), d.basedOn(), next, d.quick(),
                                                              paragraphMarkup, rPr);
        Ooxml::XmlElementPtr root = Docx::withStyle(*original, edited);
        if (d.type() == u"linked") { // the character half keeps the same look
            const QString partner = Docx::childVal(element.get(), u"link");
            if (const Ooxml::XmlElementPtr half = Docx::styleElement(*root, partner)) {
                root = Docx::withStyle(*root, Docx::editedStyle(*half, Docx::childVal(half.get(), u"name"),
                                                               Docx::childVal(half.get(), u"basedOn"), QString(),
                                                               half->child(kW, u"qFormat") != nullptr, nullptr, rPr));
            }
        }
        commitRoot(root, original);
        const Docx::DocumentStyles styles(m_document->parts());
        styles.restyle(m_document->textDocument(), before, Docx::derivedStyles(styles.sheet(), id) + Docx::derivedStyles(before, id));
        Docx::layoutListText(m_document->textDocument(), m_document->parts().defaultTabStop);
        return true;
    });
}

bool StylesController::updateToMatch(const QString &id)
{
    if (!onBody()) {
        return false;
    }
    return command([&](QTextCursor &cursor) {
        const Ooxml::XmlElementPtr original = m_document->stylesRoot();
        const Ooxml::XmlElementPtr element = Docx::styleElement(*original, id);
        if (!element) {
            return false;
        }
        const Docx::StyleSheet before = Docx::StyleSheet::parse(original.get());
        const Docx::Style *style = before.style(id);
        // The selection's own formatting joins the style's.
        QTextCursor first(cursor.document());
        first.setPosition(cursor.hasSelection() ? cursor.selectionStart() + 1 : cursor.position());
        QTextCharFormat run = first.charFormat();
        run.clearProperty(Docx::kRunStyleId);
        const Ooxml::XmlElementPtr pPr =
            style->type == u"paragraph"
            ? Docx::mergeProperties(style->pPr.get(), Docx::paragraphFormatting(cursor.block(), m_document->parts()).get())
            : nullptr;
        const Ooxml::XmlElementPtr rPr =
            Docx::mergeProperties(style->rPr.get(), Docx::runFormatting(run, *cursor.document(), m_document->parts()).get());
        commitRoot(Docx::withStyle(*original, Docx::editedStyle(*element, style->name, style->basedOn, style->next,
                                                                element->child(kW, u"qFormat") != nullptr, pPr, rPr)),
                   original);
        const Docx::DocumentStyles styles(m_document->parts());
        styles.restyle(m_document->textDocument(), before, Docx::derivedStyles(before, id));
        styles.clearFormatting(cursor); // the selection's formatting is the style's now
        return true;
    });
}

bool StylesController::canRemove(const QString &id) const
{
    const Ooxml::XmlElementPtr root = m_document->stylesRoot();
    const Ooxml::XmlElementPtr element = id.isEmpty() ? nullptr : Docx::styleElement(*root, id);
    // Word deletes the document's own styles, never its built-ins or Normal.
    return element && Docx::wAttr(element.get(), u"customStyle") == u"1"
        && Docx::StyleSheet::parse(root.get()).defaultParagraphStyle() != id;
}

bool StylesController::remove(const QString &id)
{
    if (!onBody() || !canRemove(id)) {
        return false;
    }
    return command([&](QTextCursor &) {
        const Ooxml::XmlElementPtr original = m_document->stylesRoot();
        const Docx::StyleSheet before = Docx::StyleSheet::parse(original.get());
        const Docx::Style *style = before.style(id);
        const QString parent = before.style(style->basedOn) != nullptr ? style->basedOn : before.defaultParagraphStyle();
        const QList<QTextBlock> users = paragraphsOf(*m_document->textDocument(), Docx::DocumentStyles(m_document->parts()), id);
        commitRoot(Docx::withoutStyle(*original, id), original);
        const Docx::DocumentStyles styles(m_document->parts());
        // Its paragraphs fall back to its parent; its runs to no character
        // style; styles based on it now inherit from that parent.
        for (const QTextBlock &block : users) {
            styles.apply(block, parent);
        }
        if (style->type == u"character") {
            QList<std::pair<int, int>> runs; // collected first: restyling splits fragments
            for (QTextBlock block = m_document->textDocument()->begin(); block.isValid(); block = block.next()) {
                for (auto it = block.begin(); !it.atEnd(); ++it) {
                    if (it.fragment().charFormat().stringProperty(Docx::kRunStyleId) == id) {
                        runs.append({it.fragment().position(), it.fragment().length()});
                    }
                }
            }
            for (const auto &[position, length] : std::as_const(runs)) {
                QTextCursor run(m_document->textDocument());
                run.setPosition(position);
                run.setPosition(position + length, QTextCursor::KeepAnchor);
                styles.applyCharacter(run, QString());
            }
        }
        QSet<QString> children = Docx::derivedStyles(before, id);
        children.remove(id);
        styles.restyle(m_document->textDocument(), before, children);
        return true;
    });
}

} // namespace QindaOffice::Apps::Write
