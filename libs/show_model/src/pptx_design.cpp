// SPDX-License-Identifier: GPL-3.0-or-later
// Deck-wide design changes through re-resolution (pptx_design.h).

#include "pptx_design.h"

#include "pptx_reader.h"
#include "pptx_resize.h"
#include "pptx_writer.h"

#include "ooxml_package.h"

namespace QindaOffice::Show {

namespace {

// What .pptx does not carry, put back by position (pptx_design.h).
void keepAppState(const std::vector<Shape> *beforeChildren, const QList<Shape> *before, std::vector<Shape> *afterChildren,
                  QList<Shape> *after)
{
    const qsizetype count = before != nullptr ? before->size() : qsizetype(beforeChildren->size());
    const qsizetype other = after != nullptr ? after->size() : qsizetype(afterChildren->size());
    if (count != other) {
        return;
    }
    for (qsizetype i = 0; i < count; ++i) {
        const Shape &was = before != nullptr ? before->at(i) : beforeChildren->at(size_t(i));
        Shape &now = after != nullptr ? (*after)[i] : (*afterChildren)[size_t(i)];
        if (was.kind != now.kind && !(was.media.isMedia() && now.media.isMedia())) {
            continue;
        }
        now.groupId = was.groupId;
        now.extra = was.extra;
        if (!was.children.empty()) {
            keepAppState(&was.children, nullptr, &now.children, nullptr);
        }
    }
}

Presentation readBack(const Presentation &deck, const Ooxml::PackageWriter &written, QString *error)
{
    QList<int> same;
    for (int i = 0; i < deck.slides.size(); ++i) {
        same.append(i);
    }
    return readBackEdited(deck, written, same, error);
}

} // namespace

Presentation readBackEdited(const Presentation &deck, const Ooxml::PackageWriter &written, const QList<int> &fromDeck,
                            QString *error)
{
    QString problem;
    const std::shared_ptr<const Ooxml::Package> package = Ooxml::Package::fromEntries(written.entries(), &problem);
    Presentation fresh = package ? readPptx(package, &problem) : Presentation();
    if (!problem.isEmpty() || fresh.slides.size() != fromDeck.size()) {
        if (error != nullptr) {
            *error = problem.isEmpty() ? QStringLiteral("the deck did not read back") : problem;
        }
        return deck;
    }
    for (qsizetype i = 0; i < fromDeck.size(); ++i) {
        if (fromDeck.at(i) < 0 || fromDeck.at(i) >= deck.slides.size()) {
            continue; // a slide the edit made
        }
        const Slide &was = deck.slides.at(fromDeck.at(i));
        Slide &now = fresh.slides[i];
        keepAppState(nullptr, &was.shapes, nullptr, &now.shapes);
        if (now.title.isEmpty()) {
            now.title = was.title;
        }
    }
    fresh.title = deck.title;
    return fresh;
}

Presentation reresolved(const Presentation &deck, QString *error)
{
    const std::unique_ptr<Ooxml::PackageWriter> written = writePptxPackage(deck);
    return readBack(deck, *written, error);
}

bool resizeAsks(const QSizeF &from, const QSizeF &to)
{
    if (from.isEmpty() || to.isEmpty()) {
        return false;
    }
    const qreal sx = to.width() / from.width();
    const qreal sy = to.height() / from.height();
    return qAbs(sx - sy) > 1e-3 && qMin(sx, sy) < 1.0 - 1e-6;
}

qreal resizeFactor(const QSizeF &from, const QSizeF &to, SlideScale scale)
{
    if (from.isEmpty() || to.isEmpty()) {
        return 1.0;
    }
    const qreal sx = to.width() / from.width();
    const qreal sy = to.height() / from.height();
    return scale == SlideScale::Maximize ? qMax(sx, sy) : qMin(sx, sy);
}

Presentation resized(const Presentation &deck, const QSizeF &size, SlideScale scale, QString *error)
{
    if (size.isEmpty() || size == deck.slideSize) {
        return deck;
    }
    const std::unique_ptr<Ooxml::PackageWriter> written = writePptxPackage(deck);
    Pptx::scalePackage(*written, deck.slideSize, size, resizeFactor(deck.slideSize, size, scale));
    Presentation fresh = readBack(deck, *written, error);
    return fresh;
}

ThemeSpec deckTheme(const Presentation &deck, const QString &masterId)
{
    for (const SlideMaster &master : deck.masters) {
        if ((masterId.isEmpty() || master.id == masterId) && !master.theme.isEmpty()) {
            return master.theme;
        }
    }
    // A deck QindaShow made shows Office's theme until it has its own.
    ThemeSpec office = officeTheme();
    if (!deck.masters.isEmpty() && !deck.masters.first().fontFamily.isEmpty() && deck.masters.first().theme.isEmpty()) {
        office.fonts = {QStringLiteral("Custom"), deck.masters.first().fontFamily, deck.masters.first().fontFamily};
    }
    return office;
}

Presentation themed(const Presentation &deck, const ThemeSpec &theme, ThemePart part, const QString &masterId,
                    QString *error)
{
    Presentation edited = deck;
    if (edited.masters.isEmpty()) {
        // A deck without masters gets one (its slides with it) to carry the theme.
        SlideMaster master;
        master.id = QStringLiteral("qqTheme");
        master.name = theme.name;
        edited.masters.append(master);
        for (Slide &slide : edited.slides) {
            slide.master = master.id;
        }
    }
    for (SlideMaster &master : edited.masters) {
        if (!masterId.isEmpty() && master.id != masterId) {
            continue;
        }
        if (master.theme.isEmpty()) {
            master.theme = deckTheme(deck, master.id);
        }
        switch (part) {
        case ThemePart::Whole:
            // A theme replaces the master's theme part, as PowerPoint does.
            master.theme = theme;
            if (master.theme.part.isEmpty()) {
                master.theme.part = themePartXml(theme);
            }
            break;
        case ThemePart::Colors:
            master.theme.colors = theme.colors;
            break;
        case ThemePart::Fonts:
            master.theme.fonts = theme.fonts;
            break;
        case ThemePart::Effects:
            master.theme.effects = theme.effects;
            break;
        }
        if (part != ThemePart::Whole) {
            master.theme.part.clear(); // patched into the master's own part
        }
    }
    return reresolved(edited, error);
}

Presentation withBackground(const Presentation &deck, const QString &slideId, const Background &background, bool all)
{
    Presentation edited = deck;
    if (!all) {
        if (Slide *slide = edited.findSlide(slideId)) {
            slide->background = background;
        }
        return edited;
    }
    for (SlideMaster &master : edited.masters) {
        master.background = background.color.isValid() ? background.color : QColor(Qt::white);
        master.backgroundStyle = background.style;
    }
    for (SlideLayout &layout : edited.layouts) {
        layout.background = Background();
    }
    for (Slide &slide : edited.slides) {
        // A slide without a master keeps the background itself.
        slide.background = edited.masterFor(slide) != nullptr ? Background() : background;
    }
    return edited;
}

} // namespace QindaOffice::Show
