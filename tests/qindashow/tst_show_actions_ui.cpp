// SPDX-License-Identifier: GPL-3.0-or-later
// M91 contracts through real windows and their persistent commands. Synthetic
// decks only; the player uses the offscreen platform and never launches URLs.
#include "show_test_support.h"
#include "support/quick_dump.h"
#include "support/ribbon_gate.h"
#include "qindashow/src/show_action_settings.h"
#include "qindashow/src/show_export.h"
#include "qindashow/src/show_comments.h"
#include "pptx_reader.h"
#include "qindashow/src/show_media_actions.h"
#include "qindashow/src/show_rehearsal.h"
#include "qindashow/src/show_slide_show.h"
#include <QUndoStack>
using namespace ShowTest;
using namespace QindaOffice::Apps::Show;
namespace {
ShowModel::Presentation deck()
{
    ShowModel::Presentation out;
    for (const char *title : {"Opening", "Detail", "Hidden", "Closing"}) {
        auto &slide = out.addSlide(QString::fromLatin1(title));
        ShowModel::Shape text;
        text.id = QString::fromLatin1(title);
        text.kind = ShowModel::ShapeKind::TextBox;
        text.rect = QRectF(80, 180, 550, 90);
        ShowModel::TextRun run;
        run.text = QString::fromLatin1(title);
        run.pointSize = 36;
        ShowModel::TextParagraph paragraph;
        paragraph.runs = {run};
        text.paragraphs = {paragraph};
        slide.shapes.append(text);
    }
    out.slides[2].hidden = true;
    ShowModel::Shape button;
    button.id = QStringLiteral("button");
    button.kind = ShowModel::ShapeKind::Rectangle;
    button.rect = QRectF(50, 50, 100, 50);
    button.click.link = ShowModel::jumpLink(ShowModel::ActionKind::NextSlide);
    out.slides[0].shapes.append(button);
    out.customShows = {{7, QStringLiteral("Closing first"), {out.slides[3].id, out.slides[1].id}}};
    return out;
}
void seed(Scene &scene)
{
    scene.host.applyDeckChange(deck(), QStringLiteral("Synthetic deck"));
    scene.host.selectSlideRow(0);
    scene.canvas->setSelected(QStringLiteral("button"), true);
}
QObject *dialog(Scene &scene, const char *name)
{
    return scene.window->findChild<QObject *>(QString::fromLatin1(name));
}
bool open(Scene &scene, const char *action, const char *name)
{
    if (!scene.host.actions()->trigger(QString::fromLatin1(action))) return false;
    QObject *popup = dialog(scene, name);
    return popup != nullptr && QTest::qWaitFor([popup] { return popup->property("opened").toBool(); });
}
void grab(Scene &scene, const QString &name)
{
    QTest::qWait(250);
    QindaOffice::Test::saveGrab(scene.window, name);
}
}
class ShowActionsUiTest final : public QObject {
    Q_OBJECT
private slots:
    void actionDialogsAndRibbon()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 800));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            seed(scene);
            QVERIFY(open(scene, "showInsertActionAction", "actionSettingsDialog"));
            QObject *popup = dialog(scene, "actionSettingsDialog");
            const QVariantList pages{QVariantMap{{"kind", "slide"}, {"slide", 3}, {"highlight", true}},
                                     QVariantMap{{"kind", "first"}}};
            QVERIFY(popup->setProperty("pages", pages));
            QVERIFY(QMetaObject::invokeMethod(popup, "refresh"));
            grab(scene, QStringLiteral("qindashow_m91_action_%1").arg(width));
            QVERIFY(QMetaObject::invokeMethod(popup, "accept"));
            QCOMPARE(ShowModel::parseAction(scene.host.currentSlide()->shapes.last().click).slideId,
                     scene.doc->presentation()->slides[3].id);
            QCOMPARE(ShowModel::parseAction(scene.host.currentSlide()->shapes.last().hover).kind, ShowModel::ActionKind::FirstSlide);
            scene.doc->undoStack()->undo();
            QCOMPARE(ShowModel::parseAction(scene.host.currentSlide()->shapes.last().click).kind, ShowModel::ActionKind::NextSlide);
            QVERIFY(open(scene, "showInsertLinkAction", "linkDialog"));
            QObject *link = dialog(scene, "linkDialog");
            QVERIFY(link->setProperty("kind", QStringLiteral("place")));
            QVERIFY(link->setProperty("placeIndex", 5)); // second slide
            QVERIFY(QMetaObject::invokeMethod(link, "accept"));
            QCOMPARE(ShowModel::parseAction(scene.host.currentSlide()->shapes.last().click).slideId,
                     scene.doc->presentation()->slides[1].id);
            QTest::qWait(200);
            const QStringList problems = QindaOffice::Test::ribbonProblems(scene.window, scene.host.menuTree(),
                {QStringLiteral("showFillColorAction"), QStringLiteral("showStrokeColorAction")});
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        }
    }
    void textLinksPreserveSelectionAndUndo()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        seed(scene);
        const QString id = scene.canvas->slide()->shapes.first().id;
        scene.canvas->beginTextEdit(id);
        QObject *editor = scene.host.editorItem();
        QVERIFY(editor != nullptr);
        QVERIFY(QMetaObject::invokeMethod(editor, "select", Q_ARG(int, 0), Q_ARG(int, 7)));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showInsertLinkAction")));
        const int before = scene.doc->undoStack()->count();
        QVERIFY(scene.host.actionSettings()->applyLink({{"kind", "place"}, {"place", "slide"},
                                                        {"slide", 1}, {"text", "Read detail"}}));
        const auto *shape = scene.canvas->slide()->findShape(id);
        QVERIFY(shape != nullptr);
        QCOMPARE(shape->displayText(), QStringLiteral("Read detail"));
        QCOMPARE(shape->paragraphs.first().runs.first().hyperlink,
                 ShowModel::slideLink(scene.doc->presentation()->slides[1].id));
        QCOMPARE(scene.doc->undoStack()->count(), before + 1);
        scene.doc->undoStack()->undo();
        QCOMPARE(scene.canvas->slide()->findShape(id)->displayText(), QStringLiteral("Opening"));
    }
    void externalActionsRequireConsentAndNeverRunPrograms()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("brief.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Synthetic handout");
        file.close();
        ShowHost host;
        QVERIFY(host.applyDeckChange(deck(), QStringLiteral("Synthetic deck")));
        host.startShowFromBeginning();
        QVERIFY(host.player() != nullptr);
        QSignalSpy consent(&host, &ShowHost::fileConsentRequested);
        QSignalSpy notice(&host, &ShowHost::showNotice);
        ShowModel::ShowAction action;
        action.link = QUrl::fromLocalFile(path).toString();
        QVERIFY(host.player()->runAction(action));
        QCOMPARE(consent.size(), 1);
        QCOMPARE(consent.first().first().toString(), path);
        action.link = QStringLiteral("javascript:alert(1)");
        QVERIFY(host.player()->runAction(action));
        QCOMPARE(notice.size(), 1);
        QCOMPARE(consent.size(), 1);
        QVERIFY(file.setPermissions(file.permissions() | QFileDevice::ExeOwner));
        action.link = QUrl::fromLocalFile(path).toString();
        QVERIFY(host.player()->runAction(action));
        QCOMPARE(consent.size(), 1);
        QCOMPARE(notice.size(), 2);
        host.endShow();
        auto hidden = deck();
        for (auto &slide : hidden.slides) slide.hidden = true;
        QVERIFY(host.applyDeckChange(hidden, QStringLiteral("Hide all")));
        host.startShowFromBeginning();
        QVERIFY(host.player() == nullptr);
    }
    void customShowsAndSetUpDialogs()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 800));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            seed(scene);
            auto *shows = scene.host.slideShow();
            QCOMPARE(shows->addCustomShow(QStringLiteral("Demo"), {1, 0, 3}), 1);
            QCOMPARE(shows->copyCustomShow(1), 2);
            QVERIFY(shows->editCustomShow(2, QStringLiteral("Reordered"), {3, 0}));
            QVERIFY(shows->removeCustomShow(1));
            QCOMPARE(shows->customShows().size(), 2);
            QVERIFY(open(scene, "showCustomShowsAction", "customShowsDialog"));
            grab(scene, QStringLiteral("qindashow_m91_custom_%1").arg(width));
            QVERIFY(QMetaObject::invokeMethod(dialog(scene, "customShowsDialog"), "define", Q_ARG(QVariant, -1)));
            QObject *define = dialog(scene, "defineShowDialog");
            QVERIFY(define != nullptr);
            QTRY_VERIFY(define->property("opened").toBool());
            QVERIFY(define->setProperty("picked", QVariantList{0, 3}));
            QObject *add = scene.item(QStringLiteral("defineAdd"));
            QVERIFY(add != nullptr);
            QVERIFY(QMetaObject::invokeMethod(add, "clicked"));
            QCOMPARE(define->property("chosen").toList(), (QVariantList{0, 3}));
            QVERIFY(define->setProperty("current", 1));
            QVERIFY(QMetaObject::invokeMethod(define, "move", Q_ARG(QVariant, -1)));
            QCOMPARE(define->property("chosen").toList(), (QVariantList{3, 0}));
            grab(scene, QStringLiteral("qindashow_m91_define_%1").arg(width));
            QVERIFY(QMetaObject::invokeMethod(define, "accept"));
            QCOMPARE(shows->customShows().size(), 3);
            QTRY_VERIFY(dialog(scene, "customShowsDialog")->property("opened").toBool());
            QVERIFY(QMetaObject::invokeMethod(dialog(scene, "customShowsDialog"), "close"));
            QVERIFY(open(scene, "showSetUpAction", "setUpShowDialog"));
            QObject *setup = dialog(scene, "setUpShowDialog");
            QVariantMap settings = shows->settings();
            settings.insert(QStringLiteral("type"), QStringLiteral("kiosk"));
            settings.insert(QStringLiteral("range"), QStringLiteral("custom"));
            settings.insert(QStringLiteral("customShow"), 1);
            settings.insert(QStringLiteral("animation"), false);
            QVERIFY(setup->setProperty("settings", settings));
            grab(scene, QStringLiteral("qindashow_m91_setup_%1").arg(width));
            QVERIFY(QMetaObject::invokeMethod(setup, "accept"));
            const auto *model = scene.doc->presentation();
            QCOMPARE(model->showSettings.type, ShowModel::ShowType::Kiosk);
            QCOMPARE(ShowModel::showPlaylist(*model), (QList<int>{3, 0}));
            scene.doc->undoStack()->undo();
            QCOMPARE(scene.doc->presentation()->showSettings.type, ShowModel::ShowType::Speaker);
        }
    }
    void effectCommandsChangeTheSelectedShape()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        seed(scene);
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showAnimWipe")));
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showEffectFromRightAction")));
        const auto shape = [&scene] { return scene.host.currentSlide()->shapes.last(); };
        QCOMPARE(shape().animation, ShowModel::ShapeAnimation::Wipe);
        QCOMPARE(shape().animationDirection, ShowModel::Direction::Right);
        const int before = scene.doc->undoStack()->count();
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showExitFlyOutAction")));
        QVERIFY(shape().exit.isSet());
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showExitToTopAction")));
        QCOMPARE(ShowModel::effectiveDirection(shape().exit.animation, shape().exit.direction), ShowModel::Direction::Top);
        QCOMPARE(scene.doc->undoStack()->count(), before + 2);
        scene.doc->undoStack()->undo();
        scene.doc->undoStack()->undo();
        QVERIFY(!shape().exit.isSet());
    }
    void navigationActionsAndCustomReturn()
    {
        auto model = deck();
        PresentationController player(&model);
        QCOMPARE(player.playlist(), (QList<int>{0, 1, 3}));
        QVERIFY(player.click(60, 60));
        QCOMPARE(player.slide(), 1);
        ShowModel::ShowAction action;
        action.link = ShowModel::customShowLink(7, true);
        QVERIFY(player.runAction(action));
        QCOMPARE(player.slide(), 3);
        player.advance(1);
        QCOMPARE(player.slide(), 1);
        player.advance(1); // return to the invoking slide and its old playlist
        QCOMPARE(player.playlist(), (QList<int>{0, 1, 3}));
        QCOMPARE(player.slide(), 1);
        action.link = ShowModel::slideLink(model.slides[2].id);
        QVERIFY(player.runAction(action));
        QCOMPARE(player.slide(), 2); // an explicit link can show a hidden slide
        action.link = ShowModel::jumpLink(ShowModel::ActionKind::LastViewed);
        QVERIFY(player.runAction(action));
        QCOMPARE(player.slide(), 1);
        QSignalSpy notices(&player, &PresentationController::noticeRequested);
        QSignalSpy external(&player, &PresentationController::openRequested);
        action.link = QStringLiteral("ppaction://program");
        action.address = QStringLiteral("/usr/bin/true");
        QVERIFY(player.runAction(action));
        QCOMPARE(notices.size(), 1);
        QCOMPARE(external.size(), 0);
        model.showSettings.type = ShowModel::ShowType::Kiosk;
        PresentationController kiosk(&model);
        QVERIFY(!kiosk.clickAdvances());
        kiosk.goTo(3);
        kiosk.timedAdvance();
        QCOMPARE(kiosk.slide(), 0);
        PresentationController::Options options;
        options.startSlide = 2;
        PresentationController hiddenStart(&model, options);
        QCOMPARE(hiddenStart.slide(), 3);
    }
    void rehearsalAndOneStepUndo()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 800));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            seed(scene);
            QVERIFY(scene.host.actions()->trigger(QStringLiteral("showRehearseTimingsAction")));
            auto *player = scene.host.player();
            QVERIFY(player != nullptr);
            auto *clock = player->rehearsal();
            QVERIFY(clock != nullptr);
            qint64 now = 0;
            clock->setClock([&now] { return now; });
            now = 1200;
            clock->togglePause();
            now = 9000;
            QCOMPARE(clock->slideMs(), 1200);
            clock->togglePause();
            now = 9300;
            player->advance(1);
            QCOMPARE(clock->timings().value(0), 1500);
            now = 10300;
            clock->repeat();
            now = 12500;
            QQuickWindow *window = scene.childWindow(QStringLiteral("showPlayer"));
            QVERIFY(window != nullptr);
            window->setVisibility(QWindow::Windowed);
            window->resize(width, 800); // test-only squeeze, never production geometry
            QObject *timer = window->findChild<QObject *>(QStringLiteral("playerAutoAdvance"));
            QVERIFY(timer != nullptr);
            QVERIFY(!timer->property("running").toBool());
            QTest::qWait(250);
            QindaOffice::Test::saveGrab(window, QStringLiteral("qindashow_m91_rehearsal_%1").arg(width));
            scene.host.endShow();
            const int before = scene.doc->undoStack()->count();
            scene.host.slideShow()->keepRehearsal(true);
            QCOMPARE(scene.doc->presentation()->slides[0].advanceAfterMs, 1500);
            QCOMPARE(scene.doc->presentation()->slides[1].advanceAfterMs, 2200);
            QCOMPARE(scene.doc->undoStack()->count(), before + 1);
            scene.doc->undoStack()->undo();
            QCOMPARE(scene.doc->presentation()->slides[0].advanceAfterMs, -1);
        }
    }
    void commentPositionsAreVisibleUndoableAndSaved()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        seed(scene);
        auto *comments = scene.host.comments();
        const QString id = comments->add(QStringLiteral("Move this anchor"));
        QVERIFY(!id.isEmpty());
        const int before = scene.doc->undoStack()->count();
        QVERIFY(comments->setPosition(id, 260, 180));
        QCOMPARE(scene.doc->undoStack()->count(), before + 1);
        QTRY_VERIFY(scene.item(QStringLiteral("commentPin_") + id) != nullptr);
        auto *pin = scene.item(QStringLiteral("commentPin_") + id);
        QCOMPARE(pin->x(), 260);
        QCOMPARE(pin->y(), 180);
        QVERIFY(QMetaObject::invokeMethod(pin, "clicked"));
        QTRY_VERIFY(scene.item(QStringLiteral("commentPositionX_") + id) != nullptr);
        auto *x = scene.item(QStringLiteral("commentPositionX_") + id);
        QVERIFY(QMetaObject::invokeMethod(x, "commit", Q_ARG(QVariant, 300)));
        QTRY_COMPARE(scene.host.currentSlide()->comments.first().position, QPointF(300, 180));
        grab(scene, QStringLiteral("qindashow_m91_comment_positions_1280"));
        QTemporaryDir dir;
        QVERIFY(scene.doc->save(dir.filePath(QStringLiteral("comments.pptx"))));
        const auto back = ShowModel::readPptxFile(dir.filePath(QStringLiteral("comments.pptx")));
        QCOMPARE(back.slides[0].comments.first().position, QPointF(300, 180));
        scene.doc->undoStack()->undo();
        scene.doc->undoStack()->undo();
        QCOMPARE(scene.host.currentSlide()->comments.first().shapeId, QStringLiteral("button"));
    }
    void exportAndMediaDialogs()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 800));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            seed(scene);
            QObject *video = dialog(scene, "videoExportDialog");
            QVERIFY(video != nullptr);
            QVERIFY(QMetaObject::invokeMethod(video, "open"));
            QTRY_VERIFY(video->property("opened").toBool());
            QObject *seconds = scene.item(QStringLiteral("videoSeconds"));
            QVERIFY(seconds != nullptr);
            QVERIFY(QMetaObject::invokeMethod(seconds, "commit", Q_ARG(QVariant, 9)));
            QCOMPARE(seconds->property("value").toInt(), 9);
            grab(scene, QStringLiteral("qindashow_m91_video_%1").arg(width));
            QVERIFY(QMetaObject::invokeMethod(video, "close"));
            QTemporaryDir dir;
            QCOMPARE(scene.host.exporter()->exportPictures(QStringLiteral("png"), false, 320,
                QUrl::fromLocalFile(dir.filePath(QStringLiteral("current.png")))), 1);
            QVERIFY(!QImage(dir.filePath(QStringLiteral("current.png"))).isNull());
            auto model = *scene.doc->presentation();
            auto &shape = model.slides[0].shapes.last();
            shape.media.kind = ShowModel::MediaKind::Audio;
            shape.media.data = ShowModel::builtInSound(QStringLiteral("Chime"));
            shape.media.mime = QStringLiteral("audio/wav");
            shape.media.name = QStringLiteral("Synthetic chime");
            shape.media.durationMs = 640;
            QVERIFY(scene.host.applyDeckChange(model, QStringLiteral("Synthetic audio")));
            scene.canvas->setSelected(QStringLiteral("button"), true);
            QVERIFY(scene.host.media()->applyMediaEdits({{"trimStartMs", 100}, {"trimEndMs", 40},
                {"fadeInMs", 50}, {"fadeOutMs", 80}, {"bookmarks", QVariantList{QVariantMap{{"name", "Cue"}, {"timeMs", 300}}}}}));
            const QVariantMap edits = scene.host.media()->mediaEdits();
            QCOMPARE(edits.value(QStringLiteral("trimStartMs")).toInt(), 100);
            QObject *media = dialog(scene, "mediaEditDialog");
            QVERIFY(media != nullptr);
            QVERIFY(QMetaObject::invokeMethod(media, "open"));
            QTRY_VERIFY(media->property("opened").toBool());
            grab(scene, QStringLiteral("qindashow_m91_media_%1").arg(width));
            QObject *start = scene.item(QStringLiteral("mediaTrimStart"));
            QVERIFY(start != nullptr);
            QVERIFY(QMetaObject::invokeMethod(start, "commit", Q_ARG(QVariant, 0.2)));
            QVERIFY(QMetaObject::invokeMethod(media, "accept"));
            QCOMPARE(scene.host.media()->mediaEdits().value(QStringLiteral("trimStartMs")).toInt(), 200);
            scene.doc->undoStack()->undo();
            QCOMPARE(scene.host.media()->mediaEdits(), edits);
            auto narrated = *scene.doc->presentation();
            narrated.slides[0].shapes.last().media.narration = true;
            QVERIFY(scene.host.applyDeckChange(narrated, QStringLiteral("Synthetic narration")));
            QCOMPARE(scene.host.media()->slideMedia(0).size(), 1);
            QVERIFY(scene.host.slideShow()->applySettings({{"narration", false}}));
            QCOMPARE(scene.host.media()->slideMedia(0).size(), 0);
            scene.doc->undoStack()->undo();
            QCOMPARE(scene.host.media()->slideMedia(0).size(), 1);
        }
    }
};
SHOW_TEST_MAIN(ShowActionsUiTest)
#include "tst_show_actions_ui.moc"
