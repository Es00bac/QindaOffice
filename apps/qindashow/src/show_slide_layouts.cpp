// SPDX-License-Identifier: GPL-3.0-or-later
// QindaShow's slide commands on the deck's own layouts (show_slide_layouts.h,
// M82, D-091).

#include "show_slide_layouts.h"

#include "sections.h"
#include "show_document.h"
#include "show_host.h"
#include "show_master_view.h"
#include "slide_factory.h"
#include "slide_renderer.h"

#include "menu_tree.h"
#include "ooxml_package.h"
#include "pptx_origin.h"
#include "pptx_reader.h"
#include "standard_actions.h"

#include <QPainter>

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

// QindaShow's six slide kinds (show_host.cpp's kLayouts), for a deck
// without layouts of its own.
struct Kind {
    const char *id;
    const char *label;
};
const Kind kKinds[] = {{"title", QT_TR_NOOP("Title Slide")},
                       {"title+content", QT_TR_NOOP("Title and Content")},
                       {"title+2content", QT_TR_NOOP("Title and Two Content")},
                       {"content", QT_TR_NOOP("Content")},
                       {"two-content", QT_TR_NOOP("Two Content")},
                       {"blank", QT_TR_NOOP("Blank")}};

QString newId(int position)
{
    return QStringLiteral("showNewSlideLayout_%1").arg(position + 1);
}

QString applyId(int position)
{
    return QStringLiteral("showSlideLayout_%1").arg(position + 1);
}

} // namespace

ShowSlideLayouts::ShowSlideLayouts(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    createActions();
    rebuild();
    connect(host, &ShowHost::deckChanged, this, [this] { rebuild(); });
    connect(host, &ShowHost::currentSlideChanged, this, [this] { rebuild(); });
}

ShowSlideLayouts::~ShowSlideLayouts() = default;

void ShowSlideLayouts::createActions()
{
    Quick::ActionModel *model = m_host->actions();
    ActionEntry *reset = model->add(QString::fromLatin1(SlideActionId::kResetSlide), tr("&Reset Slide"), {},
                                    QStringLiteral("rotate-ccw"));
    connect(reset, &ActionEntry::triggered, this, [this] { resetSlide(); });
    connect(model->add(QString::fromLatin1(SlideActionId::kReuseSlides), tr("Re&use Slides…"), {}, QStringLiteral("copy-plus")),
            &ActionEntry::triggered, this, &ShowSlideLayouts::reuseFileRequested);
}

bool ShowSlideLayouts::deckHasLayouts() const
{
    return !ShowModel::layoutChoices(*m_host->showDocument()->presentation()).isEmpty();
}

void ShowSlideLayouts::rebuild()
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    QList<ShowModel::LayoutChoice> choices = ShowModel::layoutChoices(*deck);
    const bool builtIn = choices.isEmpty();
    if (builtIn) {
        for (const Kind &kind : kKinds) {
            choices.append({QString(), QString(), QString::fromLatin1(kind.id), tr(kind.label), QString::fromLatin1(kind.id)});
        }
    }
    const bool changed = choices != m_choices || builtIn != m_builtIn;
    const bool counted = choices.size() != m_choices.size();
    if (counted) {
        emit idsAboutToChange();
    }
    m_choices = choices;
    m_builtIn = builtIn;
    Quick::ActionModel *model = m_host->actions();
    QSet<QString> masters;
    for (const ShowModel::LayoutChoice &choice : std::as_const(m_choices)) {
        masters.insert(choice.masterId);
    }
    const ShowModel::Slide *current = m_host->currentSlide();
    const int count = int(m_choices.size());
    for (int i = 0; i < qMax(count, m_entries); ++i) {
        if (i >= m_entries) {
            // AGENT-GUARD: positional ids (see the header): created once,
            // re-labelled on every change of the deck's layouts.
            ActionEntry *add = model->add(newId(i), QString(), {}, QStringLiteral("file-plus"));
            connect(add, &ActionEntry::triggered, this, [this, i] { newSlideOn(i); });
            ActionEntry *apply = model->add(applyId(i), QString(), {}, QStringLiteral("layout-template"));
            apply->setCheckable(true);
            apply->setRadioGroup(QStringLiteral("showSlideLayout"));
            connect(apply, &ActionEntry::triggered, this, [this, i] { applyLayout(i); });
        }
        const bool live = i < count;
        QString label;
        if (live) {
            const ShowModel::LayoutChoice &choice = m_choices.at(i);
            label = masters.size() > 1 ? tr("%1 (%2)").arg(choice.name, choice.masterName) : choice.name;
        }
        for (ActionEntry *entry : {model->entry(newId(i)), model->entry(applyId(i))}) {
            entry->setText(label);
            entry->setVisible(live);
        }
        model->entry(newId(i))->setEnabled(live); // an empty deck takes a first slide
        model->entry(applyId(i))->setEnabled(live && current != nullptr);
        const bool on = live && current != nullptr
            && (builtIn ? current->layout == m_choices.at(i).layoutId : current->layoutId == m_choices.at(i).layoutId);
        model->entry(applyId(i))->setChecked(on);
    }
    m_entries = qMax(count, m_entries);
    model->setEnabled(QString::fromLatin1(SlideActionId::kResetSlide), !builtIn && current != nullptr);
    if (changed) {
        Quick::MenuTree *tree = m_host->menuTree();
        const QList<MenuNode> items = menuItems();
        tree->replaceSubmenu(QStringLiteral("showNewSlideLayoutMenu"), items.at(0));
        tree->replaceSubmenu(QStringLiteral("showSlideLayoutMenu"), items.at(1));
    }
    if (counted) {
        emit idsChanged();
    }
    emit galleryChanged();
}

QList<MenuNode> ShowSlideLayouts::menuItems() const
{
    // One submenu per master when the deck has several, as PowerPoint's
    // gallery heads each master's layouts with its name.
    const auto build = [this](const QString &id, const QString &title, QString (*entryId)(int)) {
        MenuNode menu = MenuNode::submenu(id, title);
        QString master = QStringLiteral("\x01");
        QSet<QString> masters;
        for (const ShowModel::LayoutChoice &choice : m_choices) {
            masters.insert(choice.masterId);
        }
        for (int i = 0; i < m_choices.size(); ++i) {
            const ShowModel::LayoutChoice &choice = m_choices.at(i);
            if (masters.size() > 1 && choice.masterId != master) {
                master = choice.masterId;
                menu.children.append(MenuNode::submenu(id + QLatin1Char('_') + QString::number(i), choice.masterName));
            }
            (masters.size() > 1 ? menu.children.last().children : menu.children).append(MenuNode::action(entryId(i)));
        }
        return menu;
    };
    return {build(QStringLiteral("showNewSlideLayoutMenu"), tr("New Slide &Layout"), &newId),
            build(QStringLiteral("showSlideLayoutMenu"), tr("La&yout"), &applyId),
            MenuNode::action(QString::fromLatin1(SlideActionId::kResetSlide)),
            MenuNode::action(QString::fromLatin1(SlideActionId::kReuseSlides))};
}

QVariantList ShowSlideLayouts::gallery() const
{
    QVariantList out;
    const ShowModel::Slide *current = m_host->currentSlide();
    for (int i = 0; i < m_choices.size(); ++i) {
        const ShowModel::LayoutChoice &choice = m_choices.at(i);
        const bool on = current != nullptr && (m_builtIn ? current->layout == choice.layoutId : current->layoutId == choice.layoutId);
        out.append(QVariantMap{{QStringLiteral("newId"), newId(i)},
                               {QStringLiteral("applyId"), applyId(i)},
                               {QStringLiteral("name"), choice.name},
                               {QStringLiteral("master"), choice.masterName},
                               {QStringLiteral("current"), on}});
    }
    return out;
}

QStringList ShowSlideLayouts::newSlideIds() const
{
    QStringList ids;
    for (int i = 0; i < m_choices.size(); ++i) {
        ids.append(newId(i));
    }
    return ids;
}

QStringList ShowSlideLayouts::layoutIds() const
{
    QStringList ids;
    for (int i = 0; i < m_choices.size(); ++i) {
        ids.append(applyId(i));
    }
    return ids;
}

QString ShowSlideLayouts::entryLayout(int position) const
{
    return position >= 0 && position < m_choices.size() ? m_choices.at(position).layoutId : QString();
}

void ShowSlideLayouts::insertAt(const QString &layoutId, int index)
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    QString error;
    QString made;
    ShowModel::Presentation after;
    if (m_builtIn) {
        after = *deck;
        after.slides.insert(index, makeSlide(tr("Slide %1").arg(deck->slides.size() + 1), layoutId, deck->slideSize));
        ShowModel::normalizeSections(after);
    } else {
        after = ShowModel::withNewSlide(*deck, layoutId, index, &made, &error);
    }
    if (!error.isEmpty()) {
        m_host->showStatusMessage(tr("No new slide: %1").arg(error), 8000);
        return;
    }
    m_host->applyDeckChange(after, tr("New slide"));
    m_host->selectSlideRow(index);
}

void ShowSlideLayouts::newSlide()
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const ShowModel::Slide *current = m_host->currentSlide();
    const QString layout = m_builtIn ? QStringLiteral("title+content") : ShowModel::nextSlideLayout(*deck, current);
    insertAt(layout, m_host->currentSlideRow() + 1);
}

void ShowSlideLayouts::newSlideOn(int position)
{
    if (const QString layout = entryLayout(position); !layout.isEmpty()) {
        insertAt(layout, m_host->currentSlideRow() + 1);
    }
}

void ShowSlideLayouts::applyLayout(int position)
{
    const ShowModel::Slide *current = m_host->currentSlide();
    const QString layout = entryLayout(position);
    if (current == nullptr || layout.isEmpty()) {
        return;
    }
    if (m_builtIn) {
        m_host->setLayoutIndex(position); // the six kinds: the slide's kind
        return;
    }
    QString error;
    const ShowModel::Presentation after = ShowModel::withSlideLayout(*m_host->showDocument()->presentation(), current->id,
                                                                     layout, &error);
    if (!error.isEmpty()) {
        m_host->showStatusMessage(tr("The layout could not be applied: %1").arg(error), 8000);
        rebuild();
        return;
    }
    m_host->applyDeckChange(after, tr("Layout"));
}

void ShowSlideLayouts::resetSlide()
{
    const ShowModel::Slide *current = m_host->currentSlide();
    if (current == nullptr || m_builtIn) {
        return;
    }
    QString error;
    const ShowModel::Presentation after = ShowModel::withResetSlide(*m_host->showDocument()->presentation(), current->id, &error);
    if (!error.isEmpty()) {
        m_host->showStatusMessage(tr("The slide could not be reset: %1").arg(error), 8000);
        return;
    }
    if (!m_host->applyDeckChange(after, tr("Reset slide"))) {
        m_host->showStatusMessage(tr("The slide is as its layout has it"), 4000);
    }
}

bool ShowSlideLayouts::duplicateSlide()
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const ShowModel::Slide *current = m_host->currentSlide();
    if (current == nullptr || !deck->origin || !deck->origin->package) {
        return false; // QindaShow's own deck: the host copies the slide
    }
    const int row = m_host->currentSlideRow();
    QString error;
    QString made;
    const ShowModel::Presentation after = ShowModel::withDuplicatedSlide(*deck, current->id, &made, &error);
    if (!error.isEmpty()) {
        m_host->showStatusMessage(tr("The slide could not be duplicated: %1").arg(error), 8000);
        return true;
    }
    m_host->applyDeckChange(after, tr("Duplicate slide"));
    m_host->selectSlideRow(row + 1);
    return true;
}

bool ShowSlideLayouts::openReuseFile(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QString error;
    std::shared_ptr<const Ooxml::Package> package = Ooxml::Package::load(path, &error);
    ShowModel::Presentation deck = package ? ShowModel::readPptx(package, &error) : ShowModel::Presentation();
    if (!package || !error.isEmpty() || deck.slides.isEmpty()) {
        m_host->showStatusMessage(tr("No slides to reuse in %1%2").arg(path, error.isEmpty() ? QString() : QStringLiteral(": ") + error),
                                  8000);
        return false;
    }
    m_reusePackage = std::move(package);
    m_reuseDeck = std::make_unique<ShowModel::Presentation>(std::move(deck));
    m_reuseFile = path;
    emit reuseChanged();
    emit reuseDialogRequested();
    return true;
}

QVariantList ShowSlideLayouts::reuseSlides() const
{
    QVariantList out;
    if (m_reuseDeck) {
        for (const ShowModel::Slide &slide : m_reuseDeck->slides) {
            out.append(QVariantMap{{QStringLiteral("title"), slide.title}, {QStringLiteral("hidden"), slide.hidden}});
        }
    }
    return out;
}

const ShowModel::Presentation *ShowSlideLayouts::reuseDeck() const
{
    return m_reuseDeck.get();
}

void ShowSlideLayouts::reuse(const QVariantList &positions, bool keepSourceFormatting)
{
    if (!m_reusePackage) {
        return;
    }
    ShowModel::ReuseOptions options;
    for (const QVariant &position : positions) {
        options.slides.append(position.toInt());
    }
    const int row = m_host->currentSlideRow();
    options.index = row + 1;
    options.keepSourceFormatting = keepSourceFormatting;
    QString error;
    QStringList made;
    const ShowModel::Presentation after =
        ShowModel::withReusedSlides(*m_host->showDocument()->presentation(), m_reusePackage, options, &made, &error);
    if (!error.isEmpty()) {
        m_host->showStatusMessage(tr("The slides could not be reused: %1").arg(error), 8000);
        return;
    }
    m_host->applyDeckChange(after, tr("Reuse slides"));
    m_host->selectSlideRow(row + 1);
}

bool ShowSlideLayouts::applyTemplate(const QUrl &url, bool allSlides)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QString error;
    const std::shared_ptr<const Ooxml::Package> package = Ooxml::Package::load(path, &error);
    if (!package || !ShowModel::packageHasMasters(*package)) {
        return false;
    }
    const ShowModel::Slide *current = m_host->currentSlide();
    const QStringList slides = allSlides || current == nullptr ? QStringList() : QStringList{current->id};
    const ShowModel::Presentation after =
        ShowModel::withTemplateDesign(*m_host->showDocument()->presentation(), package, slides, &error);
    if (!error.isEmpty()) {
        m_host->showStatusMessage(tr("The design could not be applied: %1").arg(error), 8000);
        return true;
    }
    m_host->applyDeckChange(after, tr("Theme from file"));
    return true;
}

QHash<QString, ShowModel::TextRun> ShowSlideLayouts::emptyLooks(const ShowModel::Presentation &deck,
                                                                 const ShowModel::Slide &slide)
{
    QHash<QString, ShowModel::TextRun> out;
    const ShowModel::SlideLayout *layout = deck.layoutFor(slide);
    const ShowModel::SlideMaster *master = deck.masterFor(slide);
    const auto isTitle = [](const QString &type) { return type == u"title" || type == u"ctrTitle"; };
    for (const ShowModel::Shape &shape : slide.shapes) {
        if (!shape.placeholder.isPlaceholder()) {
            continue;
        }
        // Its layout's placeholder (by index, else by type), then the master's.
        const ShowModel::Shape *source = nullptr;
        for (const QList<ShowModel::Shape> *list : {layout != nullptr ? &layout->placeholders : nullptr,
                                                    master != nullptr ? &master->placeholders : nullptr}) {
            for (const ShowModel::Shape &candidate : list != nullptr ? *list : QList<ShowModel::Shape>()) {
                const ShowModel::Placeholder &ph = candidate.placeholder;
                const bool sameIndex = ph.index >= 0 && ph.index == shape.placeholder.index;
                const bool sameType = ph.type == shape.placeholder.type
                    || (isTitle(ph.type) && isTitle(shape.placeholder.type));
                if (source == nullptr && (sameIndex || sameType) && !candidate.paragraphs.isEmpty()
                    && !candidate.paragraphs.first().runs.isEmpty()) {
                    source = &candidate;
                }
            }
        }
        ShowModel::TextRun look;
        if (source != nullptr) {
            look = source->paragraphs.first().runs.first();
        } else if (master != nullptr) {
            const QList<ShowModel::TextParagraph> &style = isTitle(shape.placeholder.type) ? master->titleStyle
                                                                                          : master->bodyStyle;
            if (style.isEmpty() || style.first().runs.isEmpty()) {
                continue;
            }
            look = style.first().runs.first();
        } else {
            continue;
        }
        look.text.clear();
        look.hyperlink.clear();
        look.fieldType.clear();
        look.fieldId.clear();
        out.insert(shape.id, look);
    }
    return out;
}

void ShowSlideLayouts::renderEntry(int position, QPainter *painter, const QSizeF &size) const
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    if (position < 0 || position >= m_choices.size()) {
        return;
    }
    const ShowModel::LayoutChoice &choice = m_choices.at(position);
    if (m_builtIn) {
        const ShowModel::Slide slide = makeSlide(choice.name, choice.layoutId, deck->slideSize);
        painter->save();
        painter->scale(size.width() / deck->slideSize.width(), size.height() / deck->slideSize.height());
        ShowModel::renderSlide(painter, slide, deck->slideSize, ShowModel::backdropFor(nullptr));
        painter->restore();
        return;
    }
    ShowMasterView::renderTemplate(painter, *deck, choice.masterId, choice.layoutId, size);
}

} // namespace QindaOffice::Apps::Show
