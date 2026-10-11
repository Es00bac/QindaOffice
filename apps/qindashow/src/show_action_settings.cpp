// SPDX-License-Identifier: GPL-3.0-or-later
// Action Settings and the action buttons (show_action_settings.h); Insert
// ▸ Link is show_links.cpp.

#include "show_action_settings.h"

#include "action_model.h"
#include "menu_tree.h"
#include "show_document.h"
#include "show_host.h"
#include "slide_canvas_item.h"

#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;

namespace {

// The action-button command ids, by preset (never renamed, never reused).
QString buttonId(const QString &geometry)
{
    static const QHash<QString, QString> ids{
        {QStringLiteral("actionButtonBackPrevious"), QStringLiteral("showActionButtonBackAction")},
        {QStringLiteral("actionButtonForwardNext"), QStringLiteral("showActionButtonForwardAction")},
        {QStringLiteral("actionButtonBeginning"), QStringLiteral("showActionButtonBeginningAction")},
        {QStringLiteral("actionButtonEnd"), QStringLiteral("showActionButtonEndAction")},
        {QStringLiteral("actionButtonHome"), QStringLiteral("showActionButtonHomeAction")},
        {QStringLiteral("actionButtonInformation"), QStringLiteral("showActionButtonInformationAction")},
        {QStringLiteral("actionButtonReturn"), QStringLiteral("showActionButtonReturnAction")},
        {QStringLiteral("actionButtonMovie"), QStringLiteral("showActionButtonVideoAction")},
        {QStringLiteral("actionButtonDocument"), QStringLiteral("showActionButtonDocumentAction")},
        {QStringLiteral("actionButtonSound"), QStringLiteral("showActionButtonSoundAction")},
        {QStringLiteral("actionButtonHelp"), QStringLiteral("showActionButtonHelpAction")},
        {QStringLiteral("actionButtonBlank"), QStringLiteral("showActionButtonCustomAction")},
    };
    return ids.value(geometry);
}

struct Kind {
    ShowModel::ActionKind kind;
    const char *name;
};
constexpr Kind kKinds[] = {{ShowModel::ActionKind::None, "none"},          {ShowModel::ActionKind::NextSlide, "next"},
                           {ShowModel::ActionKind::PreviousSlide, "previous"}, {ShowModel::ActionKind::FirstSlide, "first"},
                           {ShowModel::ActionKind::LastSlide, "last"},     {ShowModel::ActionKind::LastViewed, "viewed"},
                           {ShowModel::ActionKind::EndShow, "end"},        {ShowModel::ActionKind::Slide, "slide"},
                           {ShowModel::ActionKind::CustomShow, "custom"},  {ShowModel::ActionKind::Url, "url"},
                           {ShowModel::ActionKind::File, "file"},          {ShowModel::ActionKind::Presentation, "file"},
                           {ShowModel::ActionKind::Program, "program"},    {ShowModel::ActionKind::Macro, "macro"},
                           {ShowModel::ActionKind::Media, "other"},        {ShowModel::ActionKind::Other, "other"}};

} // namespace

QColor hyperlinkColor(const ShowModel::Presentation &deck, const ShowModel::Slide *slide)
{
    const ShowModel::SlideMaster *master = slide != nullptr ? deck.masterFor(*slide) : nullptr;
    const QColor themed = master != nullptr ? master->theme.colors.slot(QStringLiteral("hlink")) : QColor();
    return themed.isValid() ? themed : QColor(0x05, 0x63, 0xC1); // the Office theme's
}

ShowActionSettings::ShowActionSettings(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    Quick::ActionModel *model = host->actions();
    ActionEntry *action = model->add(QString::fromLatin1(ActionUiId::kInsertAction), tr("A&ction…"), {}, QStringLiteral("zap"));
    connect(action, &ActionEntry::triggered, this, &ShowActionSettings::actionDialogRequested);
    m_actions.insert(action->id(), action);
    ActionEntry *link = model->add(QString::fromLatin1(ActionUiId::kInsertLink), tr("&Link…"),
                                   QKeySequence(QStringLiteral("Ctrl+K")), QStringLiteral("link"));
    connect(link, &ActionEntry::triggered, this, [this] {
        captureSelection(); // before the dialog takes the focus (show_links.cpp)
        emit linkDialogRequested();
    });
    m_actions.insert(link->id(), link);
    for (const ShowModel::ActionButtonPreset &preset : ShowModel::actionButtonPresets()) {
        ActionEntry *entry = model->add(buttonId(preset.geometry), tr("Action Button: %1").arg(preset.label), {}, preset.iconName);
        connect(entry, &ActionEntry::triggered, this, [this, geometry = preset.geometry] {
            if (insertButton(geometry)) {
                emit actionDialogRequested(); // as PowerPoint opens it on a new button
            }
        });
        m_actions.insert(entry->id(), entry);
        m_buttons.append(entry->id());
    }
    connect(host->canvas(), &SlideCanvasItem::selectionChanged, this, &ShowActionSettings::sync);
    connect(host->canvas(), &SlideCanvasItem::editingChanged, this, &ShowActionSettings::sync);
    connect(host, &ShowHost::deckChanged, this, [this] {
        sync();
        emit choicesChanged();
    });
    sync();
}

QStringList ShowActionSettings::buttonIds() const
{
    return m_buttons;
}

QList<MenuNode> ShowActionSettings::insertMenuItems() const
{
    QList<MenuNode> buttons;
    for (const QString &id : m_buttons) {
        buttons.append(MenuNode::action(id));
    }
    return {MenuNode::action(QString::fromLatin1(ActionUiId::kInsertLink)),
            MenuNode::action(QString::fromLatin1(ActionUiId::kInsertAction)),
            MenuNode::submenu(QStringLiteral("showActionButtonMenu"), tr("Action &Buttons"), buttons)};
}

void ShowActionSettings::sync()
{
    const bool slide = m_host->canvas()->slide() != nullptr && !m_host->masterViewActive();
    m_actions.value(QString::fromLatin1(ActionUiId::kInsertAction))->setEnabled(target() != nullptr);
    m_actions.value(QString::fromLatin1(ActionUiId::kInsertLink))
        ->setEnabled(target() != nullptr || !m_host->canvas()->editingShapeId().isEmpty());
    for (const QString &id : m_buttons) {
        m_actions.value(id)->setEnabled(slide);
    }
}

const ShowModel::Shape *ShowActionSettings::target() const
{
    SlideCanvasItem *canvas = m_host->canvas();
    if (m_host->masterViewActive() || canvas->selectionCount() != 1) {
        return nullptr;
    }
    return canvas->selectedShape();
}

QVariantList ShowActionSettings::slideChoices() const
{
    QVariantList out;
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    for (qsizetype i = 0; i < deck->slides.size(); ++i) {
        const QString title = deck->slides.at(i).title;
        out.append(QVariantMap{{QStringLiteral("index"), int(i)},
                               {QStringLiteral("title"), QStringLiteral("%1. %2").arg(i + 1).arg(title.isEmpty() ? tr("Slide %1").arg(i + 1) : title)}});
    }
    return out;
}

QStringList ShowActionSettings::customShowChoices() const
{
    QStringList out;
    for (const ShowModel::CustomShow &show : m_host->showDocument()->presentation()->customShows) {
        out.append(show.name);
    }
    return out;
}

QStringList ShowActionSettings::soundChoices() const
{
    QStringList out{QString(), QStringLiteral("stop")};
    out += ShowModel::builtInSounds();
    for (auto it = m_sounds.cbegin(); it != m_sounds.cend(); ++it) {
        out.append(QStringLiteral("file:") + it.key());
    }
    const ShowModel::Shape *shape = target();
    for (const ShowModel::ShowAction *action : {shape != nullptr ? &shape->click : nullptr, shape != nullptr ? &shape->hover : nullptr}) {
        if (action != nullptr && !action->sound.isEmpty() && !out.contains(QStringLiteral("keep"))) {
            out.append(QStringLiteral("keep"));
        }
    }
    return out;
}

QStringList ShowActionSettings::soundLabels() const
{
    QStringList out;
    for (const QString &choice : soundChoices()) {
        if (choice.isEmpty()) {
            out.append(tr("[No Sound]"));
        } else if (choice == u"stop") {
            out.append(tr("[Stop Previous Sound]"));
        } else if (choice == u"keep") {
            out.append(tr("The sound it has"));
        } else if (choice.startsWith(QLatin1String("file:"))) {
            out.append(choice.mid(5));
        } else {
            out.append(choice);
        }
    }
    return out;
}

QString ShowActionSettings::addSoundFile(const QUrl &url)
{
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    const QByteArray bytes = file.open(QIODevice::ReadOnly) && file.size() < 64 * 1024 * 1024 ? file.readAll() : QByteArray();
    if (!ShowModel::isWave(bytes)) {
        m_host->showStatusMessage(tr("%1 is not a WAV sound (an action plays WAV sounds only)").arg(QFileInfo(file.fileName()).fileName()), 8000);
        return {};
    }
    const QString name = QFileInfo(file.fileName()).fileName();
    m_sounds.insert(name, bytes);
    emit choicesChanged();
    return QStringLiteral("file:") + name;
}

// ---- the dialog's maps ------------------------------------------------------------

QVariantMap ShowActionSettings::actionMap(const ShowModel::ShowAction &action) const
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const ShowModel::ActionTarget target = ShowModel::parseAction(action);
    QString kind = QStringLiteral("other");
    for (const Kind &entry : kKinds) {
        if (entry.kind == target.kind) {
            kind = QLatin1String(entry.name);
            break;
        }
    }
    int slide = -1;
    for (qsizetype i = 0; i < deck->slides.size(); ++i) {
        slide = deck->slides.at(i).id == target.slideId ? int(i) : slide;
    }
    QString sound;
    if (action.stopSound) {
        sound = QStringLiteral("stop");
    } else if (!action.sound.isEmpty()) {
        sound = ShowModel::builtInSounds().contains(action.soundName) && ShowModel::builtInSound(action.soundName) == action.sound
            ? action.soundName
            : QStringLiteral("keep");
    }
    return {{QStringLiteral("kind"), kind},
            {QStringLiteral("slide"), slide},
            {QStringLiteral("custom"), ShowModel::customShowIndex(*deck, target.customShow)},
            {QStringLiteral("returnAfter"), target.returnAfter},
            {QStringLiteral("url"), target.kind == ShowModel::ActionKind::Url ? target.address : QString()},
            {QStringLiteral("file"), target.kind == ShowModel::ActionKind::File || target.kind == ShowModel::ActionKind::Presentation
                                         ? target.address : QString()},
            {QStringLiteral("program"), target.kind == ShowModel::ActionKind::Program ? target.address : QString()},
            {QStringLiteral("macro"), target.kind == ShowModel::ActionKind::Macro ? target.address : QString()},
            {QStringLiteral("sound"), sound},
            {QStringLiteral("highlight"), action.highlight}};
}

ShowModel::ShowAction ShowActionSettings::actionFrom(const QVariantMap &map, const ShowModel::ShowAction &was) const
{
    using ShowModel::ActionKind;
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    ShowModel::ShowAction action;
    action.tooltip = was.tooltip;
    const QString kind = map.value(QStringLiteral("kind")).toString();
    const int slide = map.value(QStringLiteral("slide"), -1).toInt();
    const int custom = map.value(QStringLiteral("custom"), -1).toInt();
    const QHash<QString, ActionKind> jumps{{QStringLiteral("next"), ActionKind::NextSlide},
                                           {QStringLiteral("previous"), ActionKind::PreviousSlide},
                                           {QStringLiteral("first"), ActionKind::FirstSlide},
                                           {QStringLiteral("last"), ActionKind::LastSlide},
                                           {QStringLiteral("viewed"), ActionKind::LastViewed},
                                           {QStringLiteral("end"), ActionKind::EndShow}};
    if (jumps.contains(kind)) {
        action.link = ShowModel::jumpLink(jumps.value(kind));
    } else if (kind == u"slide" && slide >= 0 && slide < deck->slides.size()) {
        action.link = ShowModel::slideLink(deck->slides.at(slide).id);
    } else if (kind == u"custom" && custom >= 0 && custom < deck->customShows.size()) {
        action.link = ShowModel::customShowLink(deck->customShows.at(custom).number,
                                                map.value(QStringLiteral("returnAfter")).toBool());
    } else if (kind == u"url") {
        QString address = map.value(QStringLiteral("url")).toString().trimmed();
        if (!address.isEmpty() && ShowModel::webScheme(address).isEmpty() && !address.startsWith(QLatin1String("file:"))) {
            address.prepend(address.contains(QLatin1Char('@')) && !address.contains(QLatin1Char('/')) ? QStringLiteral("mailto:")
                                                                                                    : QStringLiteral("https://"));
        }
        action.link = address;
    } else if (kind == u"file") {
        const QString file = map.value(QStringLiteral("file")).toString().trimmed();
        if (!file.isEmpty()) {
            action.link = QStringLiteral("ppaction://hlinkfile");
            action.address = file;
        }
    } else if (kind == u"program") {
        // AGENT-GUARD (show_action.h): written for PowerPoint, never run here.
        action.link = QStringLiteral("ppaction://program");
        action.address = map.value(QStringLiteral("program")).toString().trimmed();
    } else if (kind == u"macro" || kind == u"other") {
        action.link = was.link;
        action.address = was.address;
    }
    const QString sound = map.value(QStringLiteral("sound")).toString();
    if (sound == u"stop") {
        action.stopSound = true;
    } else if (sound == u"keep") {
        action.sound = was.sound;
        action.soundName = was.soundName;
    } else if (sound.startsWith(QLatin1String("file:"))) {
        action.sound = m_sounds.value(sound.mid(5));
        action.soundName = sound.mid(5);
    } else if (!sound.isEmpty()) {
        action.sound = ShowModel::builtInSound(sound);
        action.soundName = action.sound.isEmpty() ? QString() : sound;
    }
    action.highlight = map.value(QStringLiteral("highlight")).toBool();
    return action;
}

QVariantMap ShowActionSettings::load() const
{
    const ShowModel::Shape *shape = target();
    if (shape == nullptr) {
        return {};
    }
    return {{QStringLiteral("name"), shape->name.isEmpty() ? tr("the selected shape") : shape->name},
            {QStringLiteral("click"), actionMap(shape->click)},
            {QStringLiteral("hover"), actionMap(shape->hover)}};
}

bool ShowActionSettings::apply(const QVariantMap &click, const QVariantMap &hover)
{
    const ShowModel::Shape *shape = target();
    if (shape == nullptr) {
        return false;
    }
    const ShowModel::ShowAction onClick = actionFrom(click, shape->click);
    const ShowModel::ShowAction onHover = actionFrom(hover, shape->hover);
    return m_host->canvas()->editShape(shape->id, tr("Action settings"), [&](ShowModel::Shape &edited) {
        edited.click = onClick;
        edited.hover = onHover;
        return true;
    });
}

bool ShowActionSettings::insertButton(const QString &geometry)
{
    SlideCanvasItem *canvas = m_host->canvas();
    if (canvas->slide() == nullptr || m_host->masterViewActive()) {
        return false;
    }
    QString link;
    for (const ShowModel::ActionButtonPreset &preset : ShowModel::actionButtonPresets()) {
        link = preset.geometry == geometry ? preset.link : link;
    }
    const QSizeF size = m_host->showDocument()->presentation()->slideSize;
    // PowerPoint draws them where you drag; a new one sits in the lower
    // right, a little further left for each one already there.
    int already = 0;
    for (const ShowModel::Shape &shape : canvas->slide()->shapes) {
        already += shape.geometry.startsWith(QLatin1String("actionButton")) ? 1 : 0;
    }
    const qreal side = qMax<qreal>(36.0, size.height() / 10.0);
    ShowModel::Shape shape;
    shape.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    shape.kind = ShowModel::ShapeKind::AutoShape;
    shape.geometry = geometry;
    shape.rect = QRectF(size.width() - side * (1.5 + 1.25 * (already % 8)), size.height() - side * 1.5, side, side);
    shape.fill = QColor(91, 155, 213);
    shape.stroke = QColor(65, 113, 156);
    shape.click.link = link;
    canvas->insertShape(shape);
    return true;
}

} // namespace QindaOffice::Apps::Show
