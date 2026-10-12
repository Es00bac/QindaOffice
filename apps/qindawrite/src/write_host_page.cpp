// SPDX-License-Identifier: GPL-3.0-or-later
// WriteHost's page: Page Setup per section (M88, D-096: "Apply to" this
// section, this point forward or the whole document, as Word's dialog
// offers it) and printing, each page on its own paper.

#include "write_host.h"

#include "pdf_exporter.h"
#include "write_section_commands.h"
#include "write_sections.h"

#include <QPainter>
#include <QPrinter>
#include <QTextCursor>

namespace QindaOffice::Apps::Write {

namespace {

const std::pair<Docx::SectionStart, const char *> kStarts[] = {
    {Docx::SectionStart::NextPage, "nextPage"},
    {Docx::SectionStart::Continuous, "continuous"},
    {Docx::SectionStart::EvenPage, "evenPage"},
    {Docx::SectionStart::OddPage, "oddPage"},
    {Docx::SectionStart::NextColumn, "nextColumn"},
};

QString startName(Docx::SectionStart start)
{
    for (const auto &[value, name] : kStarts) {
        if (value == start) {
            return QString::fromLatin1(name);
        }
    }
    return QStringLiteral("nextPage");
}

// A page's paper for a printer: the portrait-shaped size and the
// orientation, which the print job's own orientation choice then keeps.
QPageLayout paperFor(const QSizeF &page)
{
    const bool landscape = page.width() > page.height();
    const QSizeF portrait = landscape ? page.transposed() : page;
    return QPageLayout(QPageSize(portrait * 0.75, QPageSize::Point),
                       landscape ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF());
}

} // namespace

QObject *WriteHost::sectionsObject() const
{
    return m_sections;
}

QVariantMap WriteHost::pageSetup() const
{
    // The caret's section (M88): Page Setup acts per section, as in Word.
    // Document px throughout; the dialog shows inches or centimetres.
    const SectionLayout &layout = m_document->sections();
    const int index = m_sections != nullptr ? qBound(0, m_sections->currentSection(), layout.count() - 1)
                                            : layout.count() - 1;
    const Docx::SectionSettings &s = layout.sections().at(index).ref.settings;
    const Docx::PageLayout &l = s.layout;
    return {{QStringLiteral("width"), l.pageSize.width()},
            {QStringLiteral("height"), l.pageSize.height()},
            {QStringLiteral("left"), l.margins.left()},
            {QStringLiteral("top"), l.margins.top()},
            {QStringLiteral("right"), l.margins.right()},
            {QStringLiteral("bottom"), l.margins.bottom()},
            {QStringLiteral("headerDistance"), l.headerDistance},
            {QStringLiteral("footerDistance"), l.footerDistance},
            {QStringLiteral("landscape"), l.landscape()},
            {QStringLiteral("differentFirstPage"), s.titlePage},
            {QStringLiteral("header"), m_document->headerText(index)},
            {QStringLiteral("footer"), m_document->footerText(index)},
            {QStringLiteral("headerRich"), !m_document->headerIsSimple(index)},
            {QStringLiteral("footerRich"), !m_document->footerIsSimple(index)},
            {QStringLiteral("sectionStart"), startName(s.flow.start)},
            {QStringLiteral("section"), index + 1},
            {QStringLiteral("sections"), layout.count()}};
}

void WriteHost::applyPageSetup(const QVariantMap &setup)
{
    const QVariantMap now = pageSetup();
    const qreal width = setup.value(QStringLiteral("width"), now.value(QStringLiteral("width"))).toReal();
    const qreal height = setup.value(QStringLiteral("height"), now.value(QStringLiteral("height"))).toReal();
    if (width <= 0 || height <= 0) {
        return;
    }
    auto margin = [&](const char *key) {
        const QString name = QString::fromLatin1(key);
        return qBound<qreal>(0, setup.value(name, now.value(name)).toReal(), qMin(width, height) / 2);
    };
    const QMarginsF margins(margin("left"), margin("top"), margin("right"), margin("bottom"));
    const qreal headerDistance = margin("headerDistance");
    const qreal footerDistance = margin("footerDistance");
    const bool first = setup.value(QStringLiteral("differentFirstPage"), now.value(QStringLiteral("differentFirstPage")))
                           .toBool();
    const QString scope = setup.value(QStringLiteral("applyTo"), QStringLiteral("document")).toString();
    const QString start = setup.value(QStringLiteral("sectionStart")).toString();
    // One undo step: every section it applies to, its section start.
    QTextCursor block(m_document->textDocument());
    block.beginEditBlock();
    m_sections->applySettings(scope, [&](Docx::SectionSettings &s) {
        s.layout.pageSize = QSizeF(width, height);
        s.layout.margins = margins;
        s.layout.headerDistance = headerDistance;
        s.layout.footerDistance = footerDistance;
        s.titlePage = first;
    });
    for (const auto &[value, name] : kStarts) {
        if (start == QLatin1String(name)) {
            m_sections->applySettings(QStringLiteral("section"),
                                      [value](Docx::SectionSettings &s) { s.flow.start = value; });
        }
    }
    block.endEditBlock();
    const int section = m_sections->currentSection();
    m_document->setHeaderFooter(setup.value(QStringLiteral("header"), m_document->headerText(section)).toString(),
                                setup.value(QStringLiteral("footer"), m_document->footerText(section)).toString(),
                                section);
    m_document->setDirty(true);
}

bool WriteHost::printPrefersLandscape() const
{
    const QSizeF page = m_printOverride ? m_printOverride->pageSize() : m_document->pageFrame(0).size;
    return page.width() > page.height();
}

std::optional<QPageLayout> WriteHost::printPageLayout() const
{
    // The first page's paper, set before the painter starts (a layout set
    // while painting only reaches the next page, print_service.h).
    return paperFor(m_printOverride ? m_printOverride->pageSize() : m_document->pageFrame(0).size);
}

Quick::PrintPageLayoutCallback WriteHost::printPageLayoutCallback() const
{
    if (m_printOverride) {
        const std::shared_ptr<QTextDocument> other = m_printOverride;
        return [other](int page) {
            return page <= other->pageCount() ? paperFor(other->pageSize()) : QPageLayout();
        };
    }
    const WriteDocument *doc = m_document;
    return [doc](int page) {
        return page <= doc->pageFrames().size() ? paperFor(doc->pageFrame(page - 1).size) : QPageLayout();
    };
}

Quick::PrintRenderCallback WriteHost::printRenderCallback() const
{
    if (m_printOverride) {
        // A merged document, an envelope or a label sheet (M77): its own
        // pages; the headers and footers of this document only when it was
        // made from this document's pages.
        std::shared_ptr<QTextDocument> other = m_printOverride;
        PageDecoration decoration;
        if (other->pageSize() == m_document->pageSize()) {
            decoration = m_document->decoration();
            decoration.footnotes.clear();
            decoration.endnotes = nullptr;
            decoration.pages.clear(); // the copy's pages are uniform
        }
        decoration.overTextPictures = true;
        return [other, decoration](QPrinter *printer, QPainter *painter) {
            renderDocumentPages(
                painter, QSizeF(printer->width(), printer->height()), other.get(),
                [printer](const QSizeF &) {
                    printer->newPage();
                    return QSizeF(printer->width(), printer->height());
                },
                decoration, printer->fromPage(), printer->toPage());
        };
    }
    const WriteDocument *doc = m_document;
    return [doc](QPrinter *printer, QPainter *painter) {
        // WYSIWYG: each page's paper is its section's (M88), matching the
        // on-screen and PDF pagination; set before newPage(), as QPrinter
        // asks.
        renderDocumentPages(
            painter, QSizeF(printer->width(), printer->height()), const_cast<QTextDocument *>(doc->textDocument()),
            [printer](const QSizeF &next) {
                printer->setPageLayout(paperFor(next));
                printer->newPage();
                return QSizeF(printer->width(), printer->height());
            },
            doc->decoration(), printer->fromPage(), printer->toPage());
    };
}

} // namespace QindaOffice::Apps::Write
