// SPDX-License-Identifier: GPL-3.0-or-later
// Audio and video in QindaShow (show_media_actions.h).

#include "show_media_actions.h"

#include "action_model.h"
#include "menu_tree.h"
#include "ooxml_package.h"
#include "show_document.h"
#include "show_host.h"
#include "slide_canvas_item.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLibraryInfo>
#include <QUuid>

namespace QindaOffice::Apps::Show {

using Quick::ActionEntry;
using Quick::MenuNode;
namespace MediaId = MediaActionId;

namespace {

// The tests' silent stand-in for MediaSurface.qml (show_media_actions.h).
QString standInSurface()
{
    return qEnvironmentVariable("QQO_SHOW_MEDIA_SURFACE");
}

// Qt Multimedia's QML module and a backend, or why not.
bool multimediaPresent(QString *reason)
{
    if (!standInSurface().isEmpty()) {
        return true;
    }
    if (qEnvironmentVariable("QQO_SHOW_MEDIA") == u"off") {
        *reason = QObject::tr("Media playback is turned off (QQO_SHOW_MEDIA=off)");
        return false;
    }
    QStringList roots{QLibraryInfo::path(QLibraryInfo::QmlImportsPath)};
    roots += qEnvironmentVariable("QML_IMPORT_PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    const bool qml = std::any_of(roots.cbegin(), roots.cend(),
                                 [](const QString &root) { return QFile::exists(root + QStringLiteral("/QtMultimedia/qmldir")); });
    if (!qml) {
        *reason = QObject::tr("Qt Multimedia is not installed: the poster frame shows instead");
        return false;
    }
    const QDir backends(QLibraryInfo::path(QLibraryInfo::PluginsPath) + QStringLiteral("/multimedia"));
    if (backends.entryList(QDir::Files).isEmpty()) {
        *reason = QObject::tr("Qt Multimedia has no media backend: the poster frame shows instead");
        return false;
    }
    return true;
}

MenuNode a(const char *id)
{
    return MenuNode::action(QString::fromLatin1(id));
}

// The bookmarks' times for the show (M91).
QVariantList bookmarkTimes(const ShowModel::MediaInfo &media)
{
    QVariantList times;
    for (const ShowModel::MediaBookmark &bookmark : media.bookmarks) {
        times.append(bookmark.timeMs);
    }
    return times;
}

int nextStep(const ShowModel::Slide &slide)
{
    int highest = 0;
    for (const ShowModel::Shape &shape : slide.shapes) {
        highest = qMax(highest, shape.buildStep);
    }
    return highest + 1;
}

} // namespace

ShowMedia::ShowMedia(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    m_available = multimediaPresent(&m_reason);
    // AGENT-GUARD: decided once, with `available` — a stand-in named for the
    // tests must never give way to the real player later in the process.
    const QString standIn = standInSurface();
    m_surface = standIn.isEmpty() ? QUrl(QStringLiteral("qrc:/qindashow/media/MediaSurface.qml")) : QUrl::fromUserInput(standIn);
    Quick::ActionModel *model = host->actions();
    const auto make = [this, model](const char *id, const QString &text, const char *icon) {
        ActionEntry *e = model->add(QString::fromLatin1(id), text, {}, QString::fromLatin1(icon));
        m_actions.insert(QString::fromLatin1(id), e);
        return e;
    };
    connect(make(MediaId::kInsertVideo, tr("&Video…"), "film"), &ActionEntry::triggered, this, [this] { emit fileRequested(false); });
    connect(make(MediaId::kInsertAudio, tr("&Audio…"), "music"), &ActionEntry::triggered, this, [this] { emit fileRequested(true); });
    ActionEntry *play = make(MediaId::kPlay, tr("&Play"), "play");
    play->setCheckable(true);
    connect(play, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            setPlaying(on);
        }
    });
    ActionEntry *onClick = make(MediaId::kOnClick, tr("Start On &Click"), "mouse-pointer");
    ActionEntry *automatic = make(MediaId::kAutomatically, tr("Start &Automatically"), "timer");
    for (ActionEntry *entry : {onClick, automatic}) {
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("showMediaStart"));
    }
    connect(onClick, &ActionEntry::triggered, this, [this] {
        editSelected(tr("Start on click"), [](ShowModel::Shape &shape) { shape.media.autoPlay = false; });
    });
    connect(automatic, &ActionEntry::triggered, this, [this] {
        editSelected(tr("Start automatically"), [](ShowModel::Shape &shape) {
            shape.media.autoPlay = true;
            shape.buildStep = 0;
        });
    });
    ActionEntry *loop = make(MediaId::kLoop, tr("&Loop until Stopped"), "repeat");
    loop->setCheckable(true);
    connect(loop, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            editSelected(tr("Loop"), [on](ShowModel::Shape &shape) { shape.media.loop = on; });
        }
    });
    ActionEntry *hide = make(MediaId::kHide, tr("&Hide While Not Playing"), "eye-off");
    hide->setCheckable(true);
    connect(hide, &ActionEntry::toggled, this, [this](bool on) {
        if (!m_syncing) {
            editSelected(tr("Hide while not playing"), [on](ShowModel::Shape &shape) { shape.media.hideWhenStopped = on; });
        }
    });
    connect(make(MediaId::kPoster, tr("Poster &Frame from File…"), "image-plus"), &ActionEntry::triggered, this,
            &ShowMedia::posterRequested);
    connect(make(MediaId::kTrim, tr("&Trim, Fade and Bookmarks…"), "scissors"), &ActionEntry::triggered, this,
            &ShowMedia::editRequested);
    connect(host->canvas(), &SlideCanvasItem::selectionChanged, this, [this] {
        setPlaying(false);
        sync();
    });
    connect(host, &ShowHost::deckChanged, this, [this] { sync(); });
    sync();
}

QList<MenuNode> ShowMedia::insertMenuItems() const
{
    return {a(MediaId::kInsertVideo), a(MediaId::kInsertAudio)};
}

QList<MenuNode> ShowMedia::playbackMenuItems() const
{
    return {MenuNode::submenu(QStringLiteral("showPlaybackMenu"), tr("Pla&yback"),
                              {a(MediaId::kPlay), MenuNode::separator(), a(MediaId::kOnClick), a(MediaId::kAutomatically),
                               a(MediaId::kLoop), a(MediaId::kHide), MenuNode::separator(), a(MediaId::kTrim),
                               a(MediaId::kPoster)})};
}

QUrl ShowMedia::surface() const
{
    return m_surface;
}

const ShowModel::Shape *ShowMedia::selectedMedia() const
{
    const ShowModel::Shape *shape = m_host->canvas()->selectionCount() == 1 ? m_host->canvas()->selectedShape() : nullptr;
    return shape != nullptr && shape->media.isMedia() ? shape : nullptr;
}

QUrl ShowMedia::urlFor(const ShowModel::MediaInfo &media) const
{
    if (media.data.isEmpty()) {
        return QUrl::fromUserInput(media.link);
    }
    if (!m_files) {
        const_cast<ShowMedia *>(this)->m_files = std::make_unique<QTemporaryDir>();
    }
    QString extension = ShowModel::mediaExtensionForMime(media.mime);
    if (extension.isEmpty()) {
        extension = QFileInfo(media.name).suffix();
    }
    const QString path = m_files->filePath(QString::fromLatin1(QCryptographicHash::hash(media.data, QCryptographicHash::Sha1).toHex())
                                           + QLatin1Char('.') + (extension.isEmpty() ? QStringLiteral("bin") : extension));
    if (!QFile::exists(path)) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(media.data);
        }
    }
    return QUrl::fromLocalFile(path);
}

QVariantMap ShowMedia::selected() const
{
    const ShowModel::Shape *shape = selectedMedia();
    if (shape == nullptr) {
        return {};
    }
    const QRectF r = shape->rect.normalized();
    return {{QStringLiteral("shapeId"), shape->id},
            {QStringLiteral("url"), urlFor(shape->media)},
            {QStringLiteral("kind"), shape->media.kind == ShowModel::MediaKind::Audio ? QStringLiteral("audio") : QStringLiteral("video")},
            {QStringLiteral("x"), r.x()},
            {QStringLiteral("y"), r.y()},
            {QStringLiteral("width"), r.width()},
            {QStringLiteral("height"), r.height()},
            {QStringLiteral("loop"), shape->media.loop},
            {QStringLiteral("volume"), shape->media.volume}};
}

void ShowMedia::setPlaying(bool playing)
{
    if (playing && !m_available) {
        m_host->showStatusMessage(m_reason, 8000);
        playing = false;
    }
    if (playing && selectedMedia() == nullptr) {
        playing = false;
    }
    if (m_playing != playing) {
        m_playing = playing;
        emit playingChanged();
    }
    sync();
}

QVariantList ShowMedia::slideMedia(int row) const
{
    QVariantList list;
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    if (row < 0 || row >= deck->slides.size()) {
        return list;
    }
    for (const ShowModel::Shape &shape : deck->slides.at(row).shapes) {
        if (!shape.media.isMedia() || (shape.media.narration && !deck->showSettings.narration)) {
            continue;
        }
        const QRectF r = shape.rect.normalized();
        list.append(QVariantMap{{QStringLiteral("shapeId"), shape.id},
                                {QStringLiteral("url"), urlFor(shape.media)},
                                {QStringLiteral("kind"), shape.media.kind == ShowModel::MediaKind::Audio ? QStringLiteral("audio")
                                                                                                         : QStringLiteral("video")},
                                {QStringLiteral("x"), r.x()},
                                {QStringLiteral("y"), r.y()},
                                {QStringLiteral("width"), r.width()},
                                {QStringLiteral("height"), r.height()},
                                {QStringLiteral("autoPlay"), shape.media.autoPlay},
                                {QStringLiteral("step"), shape.media.autoPlay ? 0 : shape.buildStep},
                                {QStringLiteral("loop"), shape.media.loop},
                                {QStringLiteral("hideWhenStopped"), shape.media.hideWhenStopped},
                                {QStringLiteral("volume"), shape.media.volume},
                                // M91: where it plays from and to, its fades, its bookmarks.
                                {QStringLiteral("startMs"), shape.media.trimStartMs},
                                {QStringLiteral("endMs"), shape.media.trimEndMs > 0 && shape.media.durationMs > 0
                                                              ? qMax(0, shape.media.durationMs - shape.media.trimEndMs) : 0},
                                {QStringLiteral("trimEndMs"), shape.media.trimEndMs},
                                {QStringLiteral("fadeInMs"), shape.media.fadeInMs},
                                {QStringLiteral("fadeOutMs"), shape.media.fadeOutMs},
                                {QStringLiteral("bookmarks"), bookmarkTimes(shape.media)}});
    }
    return list;
}

QVariantMap ShowMedia::mediaEdits() const
{
    const ShowModel::Shape *shape = selectedMedia();
    if (shape == nullptr) {
        return {};
    }
    QVariantList bookmarks;
    for (const ShowModel::MediaBookmark &bookmark : shape->media.bookmarks) {
        bookmarks.append(QVariantMap{{QStringLiteral("name"), bookmark.name}, {QStringLiteral("timeMs"), bookmark.timeMs}});
    }
    const ShowModel::MediaInfo &m = shape->media;
    return {{QStringLiteral("name"), m.name},
            {QStringLiteral("durationMs"), m.durationMs},
            {QStringLiteral("trimStartMs"), m.trimStartMs},
            {QStringLiteral("trimEndMs"), m.trimEndMs},
            {QStringLiteral("fadeInMs"), m.fadeInMs},
            {QStringLiteral("fadeOutMs"), m.fadeOutMs},
            {QStringLiteral("bookmarks"), bookmarks}};
}

bool ShowMedia::applyMediaEdits(const QVariantMap &edits)
{
    const ShowModel::Shape *shape = selectedMedia();
    if (shape == nullptr) {
        return false;
    }
    const int duration = shape->media.durationMs > 0 ? shape->media.durationMs : 24 * 3600 * 1000;
    ShowModel::MediaInfo media = shape->media;
    media.trimStartMs = qBound(0, edits.value(QStringLiteral("trimStartMs"), media.trimStartMs).toInt(), duration);
    media.trimEndMs = qBound(0, edits.value(QStringLiteral("trimEndMs"), media.trimEndMs).toInt(), duration - media.trimStartMs);
    media.fadeInMs = qMax(0, edits.value(QStringLiteral("fadeInMs"), media.fadeInMs).toInt());
    media.fadeOutMs = qMax(0, edits.value(QStringLiteral("fadeOutMs"), media.fadeOutMs).toInt());
    if (edits.contains(QStringLiteral("bookmarks"))) {
        media.bookmarks.clear();
        for (const QVariant &value : edits.value(QStringLiteral("bookmarks")).toList()) {
            const QVariantMap bookmark = value.toMap();
            media.bookmarks.append({bookmark.value(QStringLiteral("name")).toString(),
                                    qBound(0, bookmark.value(QStringLiteral("timeMs")).toInt(), duration)});
        }
        std::stable_sort(media.bookmarks.begin(), media.bookmarks.end(),
                         [](const ShowModel::MediaBookmark &a, const ShowModel::MediaBookmark &b) { return a.timeMs < b.timeMs; });
    }
    return editSelected(tr("Trim and fade"), [&media](ShowModel::Shape &edited) { edited.media = media; });
}

bool ShowMedia::insertFile(const QUrl &url, bool audio)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > Ooxml::Package::kMaxPartBytes) {
        m_host->showStatusMessage(tr("Cannot insert %1").arg(QFileInfo(path).fileName()), 8000);
        return false;
    }
    ShowModel::Slide *slide = m_host->canvas()->slide();
    if (slide == nullptr) {
        return false;
    }
    const QString name = QFileInfo(path).fileName();
    ShowModel::Shape shape;
    shape.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    shape.kind = ShowModel::ShapeKind::Image;
    shape.name = QFileInfo(path).completeBaseName();
    shape.strokeWidth = 0;
    shape.media.data = file.readAll();
    const ShowModel::MediaProbe probe = ShowModel::probeMedia(shape.media.data, name);
    shape.media.kind = probe.kind != ShowModel::MediaKind::None ? probe.kind
        : audio                                                 ? ShowModel::MediaKind::Audio
                                                                : ShowModel::MediaKind::Video;
    shape.media.mime = probe.mime.isEmpty() ? ShowModel::mediaMimeForExtension(QFileInfo(path).suffix()) : probe.mime;
    shape.media.name = name;
    shape.media.durationMs = probe.durationMs;
    // PowerPoint's default: played in the click sequence, after what is there.
    shape.buildStep = nextStep(*slide);
    const QSizeF size = m_host->showDocument()->presentation()->slideSize;
    if (shape.media.kind == ShowModel::MediaKind::Video) {
        const qreal width = size.width() * 0.5;
        shape.rect = QRectF((size.width() - width) / 2, (size.height() - width * 9 / 16) / 2, width, width * 9 / 16);
        shape.imagePng = ShowModel::mediaPoster(ShowModel::MediaKind::Video, name, QSize(640, 360));
    } else {
        shape.rect = QRectF(size.width() / 2 - 24, size.height() / 2 - 24, 48, 48);
        shape.imagePng = ShowModel::mediaPoster(ShowModel::MediaKind::Audio, name, QSize(96, 96));
    }
    shape.image.mime = QStringLiteral("image/png");
    m_host->canvas()->insertShape(shape);
    if (!m_available) {
        m_host->showStatusMessage(m_reason, 8000);
    }
    return true;
}

bool ShowMedia::setPosterFile(const QUrl &url)
{
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    QImage image;
    if (!file.open(QIODevice::ReadOnly) || !image.loadFromData(file.peek(file.size()))) {
        m_host->showStatusMessage(tr("%1 is not a picture").arg(file.fileName()), 8000);
        return false;
    }
    const QByteArray bytes = file.readAll();
    const QString mime = QStringLiteral("image/") + QFileInfo(file.fileName()).suffix().toLower().replace(QStringLiteral("jpg"), QStringLiteral("jpeg"));
    return editSelected(tr("Poster frame"), [&](ShowModel::Shape &shape) {
        shape.imagePng = bytes;
        shape.image = ShowModel::ImageInfo();
        shape.image.mime = mime;
        shape.image.pixels = image.size();
    });
}

bool ShowMedia::editSelected(const QString &label, const std::function<void(ShowModel::Shape &)> &mutate)
{
    const ShowModel::Shape *shape = selectedMedia();
    if (shape == nullptr) {
        return false;
    }
    ShowModel::Slide *slide = m_host->canvas()->slide();
    const int step = slide != nullptr ? nextStep(*slide) : 1;
    const bool done = m_host->canvas()->editShape(shape->id, label, [&](ShowModel::Shape &edited) {
        mutate(edited);
        if (!edited.media.autoPlay && edited.buildStep == 0) {
            edited.buildStep = step; // On Click: the next click plays it
        }
        return true;
    });
    sync();
    return done;
}

void ShowMedia::sync()
{
    m_syncing = true;
    const ShowModel::Shape *shape = selectedMedia();
    for (const char *id : {MediaId::kPlay, MediaId::kOnClick, MediaId::kAutomatically, MediaId::kLoop, MediaId::kHide, MediaId::kPoster,
                           MediaId::kTrim}) {
        m_actions.value(QString::fromLatin1(id))->setEnabled(shape != nullptr);
    }
    const bool canInsert = m_host->canvas()->slide() != nullptr;
    m_actions.value(QString::fromLatin1(MediaId::kInsertVideo))->setEnabled(canInsert);
    m_actions.value(QString::fromLatin1(MediaId::kInsertAudio))->setEnabled(canInsert);
    m_actions.value(QString::fromLatin1(MediaId::kPlay))->setChecked(m_playing);
    m_actions.value(QString::fromLatin1(MediaId::kOnClick))->setChecked(shape != nullptr && !shape->media.autoPlay);
    m_actions.value(QString::fromLatin1(MediaId::kAutomatically))->setChecked(shape != nullptr && shape->media.autoPlay);
    m_actions.value(QString::fromLatin1(MediaId::kLoop))->setChecked(shape != nullptr && shape->media.loop);
    m_actions.value(QString::fromLatin1(MediaId::kHide))->setChecked(shape != nullptr && shape->media.hideWhenStopped);
    m_syncing = false;
    emit selectionChanged();
}

} // namespace QindaOffice::Apps::Show
