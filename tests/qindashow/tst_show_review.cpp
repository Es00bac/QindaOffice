// SPDX-License-Identifier: GPL-3.0-or-later
// M75 (D-084) through the real window: sections in the slide sorter
// (headers, collapse, add / rename / move / remove), the Comments pane
// (new, reply, resolve, delete, previous / next, anchored to a shape or
// the slide) and audio / video (Insert ▸ Video / Audio, the Playback tab,
// a deck's own media) — each change one undo step, each kept by a .pptx
// round trip. Nothing plays: QQO_SHOW_MEDIA=off (SHOW_TEST_MAIN), so the
// tests are silent; MediaSurface.qml is only compiled. Grabs land in
// QQO_GRAB_DIR.

#include "show_test_support.h"

#include "show_model/pptx_fixture_m75.h"
#include "support/quick_dump.h"

#include "qindashow/src/presentation_controller.h"
#include "qindashow/src/show_comments.h"
#include "qindashow/src/show_media_actions.h"
#include "qindashow/src/show_sections.h"

#include "action_model.h"
#include "pptx_reader.h"

#include <QLibraryInfo>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QUndoStack>

using namespace ShowTest;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool openFixture(Scene &scene, QTemporaryDir &dir, const QByteArray &zip)
{
    const QString path = dir.filePath(QStringLiteral("deck.pptx"));
    return writeFile(path, zip) && scene.host.loadPath(path);
}

// What a save writes, read back.
ShowModel::Presentation savedAndRead(Scene &scene, QTemporaryDir &dir, const QString &name)
{
    const QString path = dir.filePath(name);
    if (!scene.doc->save(path)) {
        return {};
    }
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? ShowModel::readPptxData(file.readAll()) : ShowModel::Presentation();
}

bool shown(Scene &scene, const QString &name)
{
    QQuickItem *item = scene.item(name);
    return item != nullptr && item->isVisible();
}

// A grab once the controls' animations settle, on ribbon tab `tab` (only
// when QQO_GRAB_DIR asks for grabs).
void grab(Scene &scene, const QString &name, const QString &tab = QString())
{
    if (!qEnvironmentVariableIsSet("QQO_GRAB_DIR")) {
        return;
    }
    if (QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon")); ribbon != nullptr && !tab.isEmpty()) {
        QVariant ok;
        QMetaObject::invokeMethod(ribbon, "activateTab", Q_RETURN_ARG(QVariant, ok), Q_ARG(QVariant, tab));
    }
    QTest::qWait(400);
    QindaOffice::Test::saveGrab(scene.window, name);
}

} // namespace

class ShowReviewTest : public QObject {
    Q_OBJECT
private slots:
    void sectionsShowInTheSorterAndCollapse();
    void sectionCommandsAreOneUndoStepEach();
    void commentsPaneThreadsAndRoundTrips();
    void deckCommentsWalkWithPreviousAndNext();
    void insertedVideoTakesPlaybackOptions();
    void deckMediaReachesTheShowSilently();
    void showPlaysMediaWithTheSlideAndAtItsClick();
    void mediaSurfaceCompilesWhereQtMultimediaIs();
};

void ShowReviewTest::sectionsShowInTheSorterAndCollapse()
{
    QTemporaryDir dir;
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(openFixture(scene, dir, PptxFixture::edited([](QindaOffice::Ooxml::PackageWriter &w) {
        PptxFixture::presentationExtension(w, PptxFixture::kSections);
    })));
    QindaOffice::Apps::Show::ShowSections *sections = scene.host.sections();
    QCOMPARE(int(scene.doc->presentation()->sections.size()), 2);
    QCOMPARE(int(sections->rows().size()), 2 + 5);
    QTRY_VERIFY(shown(scene, QStringLiteral("sectionHeader_0")));
    QVERIFY(shown(scene, QStringLiteral("sectionHeader_1")));
    QVERIFY(shown(scene, QStringLiteral("slideThumb_0")));
    grab(scene, QStringLiteral("qindashow_m75_sections"), QStringLiteral("home"));

    // Collapsing "Intro" hides its two slides; it is a view state, no edit.
    sections->toggleCollapsed(0);
    QTRY_VERIFY(!shown(scene, QStringLiteral("slideThumb_0")));
    QVERIFY(scene.item(QStringLiteral("slideThumb_2")) != nullptr);
    QVERIFY(!scene.doc->isDirty());
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showExpandSectionsAction")));
    QTRY_VERIFY(shown(scene, QStringLiteral("slideThumb_0")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showCollapseSectionsAction")));
    QCOMPARE(int(sections->rows().size()), 2);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showExpandSectionsAction")));

    // A slide moved across a header joins that section.
    scene.host.moveSlide(3, 0);
    QCOMPARE(int(scene.doc->presentation()->sections.first().slides.size()), 3);
    QCOMPARE(int(scene.doc->presentation()->sections.last().slides.size()), 2);
    const ShowModel::Presentation back = savedAndRead(scene, dir, QStringLiteral("sections.pptx"));
    QCOMPARE(int(back.sections.size()), 2);
    QCOMPARE(back.sections.first().name, QStringLiteral("Intro"));
    QCOMPARE(int(back.sections.first().slides.size()), 3);
}

void ShowReviewTest::sectionCommandsAreOneUndoStepEach()
{
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    for (int i = 0; i < 3; ++i) {
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("slideNewAction")));
    }
    ShowModel::Presentation *deck = scene.doc->presentation();
    const int slides = int(deck->slides.size());
    QVERIFY(!scene.host.actions()->entry(QStringLiteral("showRemoveSectionAction"))->isEnabled());
    const int steps = scene.doc->undoStack()->count();

    // Add Section at slide 3: PowerPoint names the first part, asks for
    // the new one's name.
    scene.host.selectSlideRow(2);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showAddSectionAction")));
    QCOMPARE(int(deck->sections.size()), 2);
    QCOMPARE(int(deck->sections.first().slides.size()), 2);
    QObject *prompt = scene.window->findChild<QObject *>(QStringLiteral("renameSectionDialog"));
    QVERIFY(prompt != nullptr);
    QTRY_VERIFY(prompt->property("opened").toBool());
    prompt->setProperty("text", QStringLiteral("Results"));
    QMetaObject::invokeMethod(prompt, "accept");
    QCOMPARE(deck->sections.last().name, QStringLiteral("Results"));
    QCOMPARE(scene.doc->undoStack()->count(), steps + 2);

    // Move Section Up: "Results" leads, its slides first.
    QVERIFY(scene.host.actions()->entry(QStringLiteral("showMoveSectionUpAction"))->isEnabled());
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showMoveSectionUpAction")));
    QCOMPARE(deck->sections.first().name, QStringLiteral("Results"));
    QCOMPARE(int(deck->slides.size()), slides);
    QCOMPARE(scene.host.currentSlideRow(), 0); // the current slide moved with it

    // Remove Section: its slides join the section before (or after).
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showRemoveSectionAction")));
    QCOMPARE(int(deck->sections.size()), 1);
    QCOMPARE(int(deck->slides.size()), slides);
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showRemoveAllSectionsAction")));
    QVERIFY(deck->sections.isEmpty());
    QCOMPARE(scene.doc->undoStack()->count(), steps + 5);
    for (int i = 0; i < 5; ++i) {
        scene.doc->undoStack()->undo();
    }
    QVERIFY(deck->sections.isEmpty());
    scene.doc->undoStack()->redo();
    QCOMPARE(int(deck->sections.size()), 2);
}

void ShowReviewTest::commentsPaneThreadsAndRoundTrips()
{
    QTemporaryDir dir;
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(openFixture(scene, dir, PptxFixture::build()));
    QindaOffice::Apps::Show::ShowComments *comments = scene.host.comments();
    QVERIFY(!shown(scene, QStringLiteral("commentsPane")));

    // New Comment opens the pane; with nothing selected it goes on the slide.
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showNewCommentAction")));
    QTRY_VERIFY(shown(scene, QStringLiteral("commentsPane")));
    QVERIFY(scene.host.actions()->entry(QStringLiteral("showCommentsPaneAction"))->isChecked());
    const int steps = scene.doc->undoStack()->count();
    const QString onSlide = comments->add(QStringLiteral("Tighten the title"));
    QVERIFY(!onSlide.isEmpty());
    QVERIFY(scene.doc->presentation()->slides.first().comments.first().shapeId.isEmpty());

    // With a shape selected, the comment is anchored to it.
    const QString box = scene.doc->presentation()->slides.first().shapes.at(2).id;
    scene.canvas->setSelected(box, true);
    QCOMPARE(comments->anchorName(), QStringLiteral("TextBox 3"));
    const QString onShape = comments->add(QStringLiteral("Too dark?"));
    QCOMPARE(scene.doc->presentation()->slides.first().comments.last().shapeId, box);
    QVERIFY(!comments->reply(onShape, QStringLiteral("No, it is fine")).isEmpty());
    QVERIFY(comments->setDone(onShape, true));
    QCOMPARE(comments->count(), 2);
    const QVariantList threads = comments->comments();
    QCOMPARE(int(threads.last().toMap().value(QStringLiteral("replies")).toList().size()), 1);
    QVERIFY(threads.last().toMap().value(QStringLiteral("done")).toBool());
    QTRY_VERIFY(shown(scene, QStringLiteral("commentCard_") + onShape));
    grab(scene, QStringLiteral("qindashow_m75_comments"), QStringLiteral("review"));
    QCOMPARE(scene.doc->undoStack()->count(), steps + 4);

    // A resolved thread is written as PowerPoint's modern comments.
    const ShowModel::Presentation back = savedAndRead(scene, dir, QStringLiteral("comments.pptx"));
    QCOMPARE(int(back.slides.first().comments.size()), 3);
    int resolved = 0;
    for (const ShowModel::SlideComment &c : back.slides.first().comments) {
        resolved += c.resolved ? 1 : 0;
        QCOMPARE(c.author, comments->author());
    }
    QCOMPARE(resolved, 2); // the thread and its reply
    QVERIFY(!back.legacyComments);

    // Delete takes the thread with its replies; undo brings both back.
    QVERIFY(comments->remove(onShape));
    QCOMPARE(int(scene.doc->presentation()->slides.first().comments.size()), 1);
    scene.doc->undoStack()->undo();
    QCOMPARE(int(scene.doc->presentation()->slides.first().comments.size()), 3);

    // Hiding the pane through its command unchecks it.
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showCommentsPaneAction")));
    QTRY_VERIFY(!shown(scene, QStringLiteral("commentsPane")));
}

void ShowReviewTest::deckCommentsWalkWithPreviousAndNext()
{
    QTemporaryDir dir;
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(openFixture(scene, dir, PptxFixture::edited([](QindaOffice::Ooxml::PackageWriter &w) {
        PptxFixture::addModernComments(w);
        PptxFixture::addLegacyComments(w);
    })));
    QindaOffice::Apps::Show::ShowComments *comments = scene.host.comments();
    QCOMPARE(comments->total(), 3); // two threads on slide 1, one on slide 2
    QVERIFY(scene.host.actions()->entry(QStringLiteral("showNextCommentAction"))->isEnabled());

    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showNextCommentAction")));
    QCOMPARE(scene.host.currentSlideRow(), 0);
    const QString first = comments->activeId();
    // The first thread is on the text box: Next selects it on the canvas.
    QCOMPARE(scene.canvas->selectionCount(), 1);
    QCOMPARE(scene.canvas->selectedShape()->name, QStringLiteral("TextBox 3"));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showNextCommentAction")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showNextCommentAction")));
    QCOMPARE(scene.host.currentSlideRow(), 1); // the legacy thread on slide 2
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showNextCommentAction")));
    QCOMPARE(comments->activeId(), first); // wraps around
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showPreviousCommentAction")));
    QCOMPARE(scene.host.currentSlideRow(), 1);
    QVERIFY(scene.host.actions()->entry(QStringLiteral("showDeleteCommentAction"))->isEnabled());
}

void ShowReviewTest::insertedVideoTakesPlaybackOptions()
{
    QTemporaryDir dir;
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(openFixture(scene, dir, PptxFixture::build()));
    QindaOffice::Apps::Show::ShowMedia *media = scene.host.media();
    QVERIFY(!media->available()); // QQO_SHOW_MEDIA=off
    QVERIFY(!media->unavailableReason().isEmpty());
    QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
    QVERIFY(ribbon != nullptr);

    const QString clip = dir.filePath(QStringLiteral("clip.mp4"));
    QVERIFY(writeFile(clip, PptxFixture::tinyMp4(2500)));
    const int steps = scene.doc->undoStack()->count();
    QVERIFY(media->insertFile(QUrl::fromLocalFile(clip), false));
    QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
    const ShowModel::Shape *shape = scene.canvas->selectedShape();
    QVERIFY(shape != nullptr && shape->media.isMedia());
    QCOMPARE(shape->media.kind, ShowModel::MediaKind::Video);
    QCOMPARE(shape->media.durationMs, 2500);
    QVERIFY(!shape->imagePng.isEmpty()); // a poster frame of its own
    QVERIFY(shape->buildStep > 0);       // On Click: in the click sequence
    QTRY_VERIFY(ribbon->property("contextIds").toStringList().contains(QStringLiteral("media")));
    QVERIFY(scene.host.actions()->entry(QStringLiteral("showMediaStartOnClickAction"))->isChecked());

    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showMediaStartAutomaticallyAction")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showMediaLoopAction")));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showMediaHideAction")));
    shape = scene.canvas->selectedShape();
    QVERIFY(shape->media.autoPlay);
    QCOMPARE(shape->buildStep, 0);
    QVERIFY(shape->media.loop);
    QVERIFY(shape->media.hideWhenStopped);
    QCOMPARE(scene.doc->undoStack()->count(), steps + 4);

    // Play without Qt Multimedia says why and stays off.
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showMediaPlayAction")));
    QVERIFY(!media->playing());
    QVERIFY(!scene.host.actions()->entry(QStringLiteral("showMediaPlayAction"))->isChecked());
    QVERIFY(scene.item(QStringLiteral("mediaSurface")) == nullptr);
    grab(scene, QStringLiteral("qindashow_m75_video"), QStringLiteral("playback"));

    // A sound, then the deck through PowerPoint's format and back.
    const QString sound = dir.filePath(QStringLiteral("chime.wav"));
    QVERIFY(writeFile(sound, PptxFixture::tinyWav(1500)));
    QVERIFY(media->insertFile(QUrl::fromLocalFile(sound), true));
    QCOMPARE(scene.canvas->selectedShape()->media.kind, ShowModel::MediaKind::Audio);
    QCOMPARE(scene.canvas->selectedShape()->media.durationMs, 1500);
    const ShowModel::Presentation back = savedAndRead(scene, dir, QStringLiteral("media.pptx"));
    int videos = 0;
    int sounds = 0;
    for (const ShowModel::Shape &s : back.slides.first().shapes) {
        if (s.media.kind == ShowModel::MediaKind::Video) {
            ++videos;
            QVERIFY(s.media.autoPlay && s.media.loop && s.media.hideWhenStopped);
            QCOMPARE(s.media.data, PptxFixture::tinyMp4(2500));
        } else if (s.media.kind == ShowModel::MediaKind::Audio) {
            ++sounds;
            QVERIFY(!s.media.autoPlay);
        }
    }
    QCOMPARE(videos, 1);
    QCOMPARE(sounds, 1);
}

void ShowReviewTest::deckMediaReachesTheShowSilently()
{
    QTemporaryDir dir;
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(openFixture(scene, dir, PptxFixture::edited([](QindaOffice::Ooxml::PackageWriter &w) { PptxFixture::addVideo(w); })));
    const QVariantList media = scene.host.media()->slideMedia(2);
    QCOMPARE(int(media.size()), 1);
    const QVariantMap clip = media.first().toMap();
    QCOMPARE(clip.value(QStringLiteral("kind")).toString(), QStringLiteral("video"));
    QVERIFY(clip.value(QStringLiteral("autoPlay")).toBool());
    QVERIFY(clip.value(QStringLiteral("loop")).toBool());
    QVERIFY(clip.value(QStringLiteral("hideWhenStopped")).toBool());
    // The medium's bytes are a file the player can open.
    const QUrl url = clip.value(QStringLiteral("url")).toUrl();
    QFile file(url.toLocalFile());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), PptxFixture::tinyMp4(4321));

    // The show places nothing without Qt Multimedia: the poster is the show.
    scene.host.selectSlideRow(2);
    scene.host.startShow();
    QVERIFY(scene.host.player() != nullptr);
    QQuickWindow *player = scene.childWindow(QStringLiteral("showPlayer"));
    QVERIFY(player != nullptr);
    QQuickItem *layer = findItem(player->contentItem(), QStringLiteral("playerMedia"));
    QVERIFY(layer != nullptr);
    QCOMPARE(layer->property("count").toInt(), 0);
    scene.host.endShow();
}

// The show's media logic through a silent stand-in for MediaSurface.qml
// (QQO_SHOW_MEDIA_SURFACE; it plays nothing): Start Automatically plays
// with the slide, On Click at its click, Hide While Not Playing keeps a
// medium out of sight until then, a click on a video pauses it, and the
// next slide stops everything.
void ShowReviewTest::showPlaysMediaWithTheSlideAndAtItsClick()
{
    QTemporaryDir dir;
    const QString standIn = dir.filePath(QStringLiteral("StandInSurface.qml"));
    QVERIFY(writeFile(standIn, R"(import QtQuick
Item {
    objectName: "mediaSurface"
    property url source
    property bool video: true
    property bool loop: false
    property real volume: 0.8
    property bool playing: false
    property bool started: false
    signal finished()
    function stop() { started = false }
    onPlayingChanged: if (playing && String(source).length > 0) started = true
    onSourceChanged: if (playing && String(source).length > 0) started = true
}
)"));
    // Set for the whole case (the host decides at construction, and the
    // real player must never load here).
    qputenv("QQO_SHOW_MEDIA_SURFACE", QUrl::fromLocalFile(standIn).toString().toUtf8());
    const auto unset = qScopeGuard([] { qunsetenv("QQO_SHOW_MEDIA_SURFACE"); });
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(scene.host.media()->available());
    QCOMPARE(scene.host.media()->surface(), QUrl::fromLocalFile(standIn));
    QVERIFY(openFixture(scene, dir, PptxFixture::edited([](QindaOffice::Ooxml::PackageWriter &w) { PptxFixture::addVideo(w); })));
    // Slide 3: the deck's video (automatic, looping, hidden while stopped)
    // and an inserted sound (On Click), hidden while not playing too.
    scene.host.selectSlideRow(2);
    const QString sound = dir.filePath(QStringLiteral("chime.wav"));
    QVERIFY(writeFile(sound, PptxFixture::tinyWav(800)));
    QVERIFY(scene.host.media()->insertFile(QUrl::fromLocalFile(sound), true));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showMediaHideAction")));
    int soundStep = -1;
    for (const QVariant &value : scene.host.media()->slideMedia(2)) {
        const QVariantMap medium = value.toMap();
        soundStep = medium.value(QStringLiteral("kind")).toString() == u"audio" ? medium.value(QStringLiteral("step")).toInt() : soundStep;
    }
    QVERIFY(soundStep > 0);

    scene.host.startShow();
    QQuickWindow *player = scene.childWindow(QStringLiteral("showPlayer"));
    QVERIFY(player != nullptr);
    QVERIFY(QTest::qWaitForWindowExposed(player));
    QQuickItem *layer = findItem(player->contentItem(), QStringLiteral("playerMedia"));
    QVERIFY(layer != nullptr);
    QTRY_VERIFY(layer->width() > 0);
    QTRY_COMPARE(layer->property("count").toInt(), 2);
    QQuickItem *video = findItem(layer, QStringLiteral("playerMedia_0"));
    QQuickItem *audio = findItem(layer, QStringLiteral("playerMedia_1"));
    QVERIFY(video != nullptr && audio != nullptr);
    QTRY_VERIFY(findItem(video, QStringLiteral("mediaSurface")) != nullptr);
    QQuickItem *videoSurface = findItem(video, QStringLiteral("mediaSurface"));
    QQuickItem *audioSurface = findItem(audio, QStringLiteral("mediaSurface"));
    QVERIFY(audioSurface != nullptr);
    QVERIFY(videoSurface->property("playing").toBool()); // with the slide
    QVERIFY(videoSurface->property("loop").toBool());
    QVERIFY(video->isVisible());
    QVERIFY(!audioSurface->property("playing").toBool()); // not before its click
    QVERIFY(!audio->isVisible());

    while (scene.host.player()->build() < soundStep) {
        QVERIFY(QMetaObject::invokeMethod(player, "advance", Q_ARG(QVariant, QVariant(1))));
    }
    QCOMPARE(scene.host.player()->slide(), 2);
    QTRY_VERIFY(audioSurface->property("playing").toBool());
    QVERIFY(audio->isVisible());

    // A click on the video pauses it, another plays it on.
    const QPointF centre = video->mapToItem(layer, QPointF(video->width() / 2, video->height() / 2));
    QVariant hit;
    QVERIFY(QMetaObject::invokeMethod(layer, "toggleAt", Q_RETURN_ARG(QVariant, hit), Q_ARG(QVariant, QVariant(centre.x())),
                                      Q_ARG(QVariant, QVariant(centre.y()))));
    QVERIFY(hit.toBool());
    QVERIFY(!videoSurface->property("playing").toBool());
    QVERIFY(QMetaObject::invokeMethod(layer, "toggleAt", Q_RETURN_ARG(QVariant, hit), Q_ARG(QVariant, QVariant(centre.x())),
                                      Q_ARG(QVariant, QVariant(centre.y()))));
    QVERIFY(videoSurface->property("playing").toBool());

    // The next slide: nothing of slide 3 plays on.
    QVERIFY(QMetaObject::invokeMethod(player, "advance", Q_ARG(QVariant, QVariant(1))));
    QCOMPARE(scene.host.player()->slide(), 3);
    QTRY_COMPARE(layer->property("count").toInt(), 0);
    scene.host.endShow();
}

void ShowReviewTest::mediaSurfaceCompilesWhereQtMultimediaIs()
{
    if (!QFile::exists(QLibraryInfo::path(QLibraryInfo::QmlImportsPath) + QStringLiteral("/QtMultimedia/qmldir"))) {
        QSKIP("Qt Multimedia's QML module is not installed");
    }
    // Compiled, never instantiated: nothing is played or opened.
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qindashow/media/MediaSurface.qml")));
    QTRY_VERIFY(!component.isLoading());
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
}

SHOW_TEST_MAIN(ShowReviewTest)

#include "tst_show_review.moc"
