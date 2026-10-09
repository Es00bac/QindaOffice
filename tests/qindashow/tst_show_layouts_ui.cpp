// SPDX-License-Identifier: GPL-3.0-or-later
// M82 (D-091) through the real QindaShow window: on a PowerPoint deck New
// Slide uses the deck's own layouts — the gallery (thumbnails), the Slide
// menu and the ribbon's New Slide and Layout menus list its masters and
// layouts by name; New Slide picks the layout PowerPoint would; a slide
// made on a layout, Layout, Reset Slide, Duplicate Slide and Reuse Slides
// are ONE undo step each and save as PowerPoint keeps them; an empty
// placeholder shows its prompt; Browse for Themes with a template asks All
// or Selected and brings the template's masters; the panel export takes
// the deck's entries. QQO_GRAB_DIR saves the gallery and the dialogs at
// 1280 and 520.

#include "show_test_support.h"

#include "show_model/pptx_fixture_m82.h"
#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include "qindashow/src/show_design_tab.h"
#include "qindashow/src/show_slide_layouts.h"

#include "menu_source.h"
#include "pptx_reader.h"

#include <qindaqt/shell/global_menu/protocol/menu_validation.h>

#include <QUndoStack>
#include <QUuid>

using namespace ShowTest;
using QindaOffice::Quick::ActionModelMenuSource;
namespace Protocol = QindaQt::Shell::GlobalMenu::Protocol;

namespace {

QString writeFile(QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        return {};
    }
    return path;
}

bool openDeck(Scene &scene, QTemporaryDir &dir, const QByteArray &zip = PptxFixture::M82::layoutsDeck())
{
    const QString path = writeFile(dir, QStringLiteral("deck.pptx"), zip);
    return !path.isEmpty() && scene.host.loadPath(path);
}

bool trigger(Scene &scene, const QString &id)
{
    return scene.host.actions()->trigger(id);
}

QString layoutName(const ShowModel::Presentation &deck, const ShowModel::Slide &slide)
{
    const ShowModel::SlideLayout *layout = deck.findLayout(slide.layoutId);
    return layout != nullptr ? layout->name : QString();
}

void grab(Scene &scene, const QString &name)
{
    QTest::qWait(300);
    QindaOffice::Test::saveGrab(scene.window, name);
}

bool dialogOpened(Scene &scene, const char *name)
{
    QObject *dialog = scene.window->findChild<QObject *>(QString::fromLatin1(name));
    return dialog != nullptr && QTest::qWaitFor([dialog] { return dialog->property("opened").toBool(); });
}

// How many pixels of `item` differ from its first one (a painted thumbnail
// is not one flat colour).
int varied(Scene &scene, QQuickItem *item)
{
    const QImage shot = scene.window->grabWindow();
    const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    const QImage part = shot.copy(r.toAlignedRect());
    int count = 0;
    for (int y = 0; y < part.height(); ++y) {
        for (int x = 0; x < part.width(); ++x) {
            count += part.pixel(x, y) != part.pixel(0, 0) ? 1 : 0;
        }
    }
    return count;
}

} // namespace

class ShowLayoutsUiTest final : public QObject {
    Q_OBJECT
private slots:
    void galleryAndMenusListTheDecksLayouts()
    {
        for (const int width : {1280, 520}) {
            QTemporaryDir dir;
            Scene scene(QSize(width, 800));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QVERIFY(openDeck(scene, dir));
            const QVariantList gallery = scene.host.slideLayouts()->gallery();
            QCOMPARE(gallery.size(), 4);
            QStringList names;
            for (const QVariant &entry : gallery) {
                names.append(entry.toMap().value(QStringLiteral("name")).toString());
            }
            QCOMPARE(names, (QStringList{QStringLiteral("Title and Content"), QStringLiteral("Title Slide"),
                                         QStringLiteral("Two Content"), QStringLiteral("Blank")}));
            QVERIFY(gallery.first().toMap().value(QStringLiteral("current")).toBool());
            for (int i = 1; i <= 4; ++i) {
                QCOMPARE(scene.host.actions()->entry(QStringLiteral("showNewSlideLayout_%1").arg(i))->text(), names.at(i - 1));
                QVERIFY(scene.host.actions()->entry(QStringLiteral("showSlideLayout_%1").arg(i))->isEnabled());
            }
            // The six slide kinds' entries past the deck's four are gone.
            QVERIFY(!scene.host.actions()->entry(QStringLiteral("showNewSlideLayout_5"))->isVisible());
            // The thumbnails are painted.
            QTRY_VERIFY(scene.item(QStringLiteral("layoutThumb_3")) != nullptr);
            QTRY_VERIFY(varied(scene, scene.item(QStringLiteral("layoutThumb_3"))) > 50);
            // Every command on the ribbon, every item inside it.
            QTest::qWait(150);
            const QStringList problems = QindaOffice::Test::ribbonProblems(
                scene.window, scene.host.menuTree(), {QStringLiteral("showFillColorAction"), QStringLiteral("showStrokeColorAction")});
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
            QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
            QMetaObject::invokeMethod(ribbon, "activateTab", Q_ARG(QVariant, QStringLiteral("home")));
            grab(scene, QStringLiteral("qindashow_m82_layout_gallery_%1").arg(width));
        }
    }

    void newSlideLayoutResetAndDuplicateAreOneStepEach()
    {
        QTemporaryDir dir;
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(openDeck(scene, dir));
        ShowModel::Presentation *deck = scene.doc->presentation();
        const int slides = int(deck->slides.size());
        const int steps = scene.doc->undoStack()->count();
        // New Slide after slide 1: its own layout (it is not a title slide).
        QVERIFY(trigger(scene, QStringLiteral("slideNewAction")));
        QCOMPARE(int(deck->slides.size()), slides + 1);
        QCOMPARE(scene.host.currentSlideRow(), 1);
        QCOMPARE(layoutName(*deck, deck->slides.at(1)), QStringLiteral("Title and Content"));
        // A new slide on Two Content from the gallery's entry.
        QVERIFY(trigger(scene, QStringLiteral("showNewSlideLayout_3")));
        QCOMPARE(scene.host.currentSlideRow(), 2);
        QCOMPARE(layoutName(*deck, deck->slides.at(2)), QStringLiteral("Two Content"));
        QCOMPARE(scene.doc->undoStack()->count(), steps + 2);
        // The empty placeholders show their prompts on the canvas.
        QTest::qWait(200);
        grab(scene, QStringLiteral("qindashow_m82_new_slide_on_layout_1280"));
        QVERIFY(varied(scene, scene.canvas) > 200);
        // Layout: the current slide onto Title Slide.
        QVERIFY(trigger(scene, QStringLiteral("showSlideLayout_2")));
        QCOMPARE(layoutName(*deck, deck->slides.at(2)), QStringLiteral("Title Slide"));
        QVERIFY(scene.host.actions()->entry(QStringLiteral("showSlideLayout_2"))->isChecked());
        scene.doc->undoStack()->undo();
        QCOMPARE(layoutName(*deck, deck->slides.at(2)), QStringLiteral("Two Content"));
        // Reset Slide: the title moved on slide 1 goes back.
        scene.host.selectSlideRow(0);
        ShowModel::Presentation moved = *deck;
        for (ShowModel::Shape &shape : moved.slides[0].shapes) {
            if (shape.placeholder.type == u"title") {
                shape.rect.translate(60, 40);
            }
        }
        QVERIFY(scene.host.applyDeckChange(moved, QStringLiteral("Move")));
        QVERIFY(trigger(scene, QStringLiteral("showResetSlideAction")));
        for (const ShowModel::Shape &shape : deck->slides.at(0).shapes) {
            if (shape.placeholder.type == u"title") {
                QCOMPARE(shape.rect, QRectF(36, 18, 888, 72));
            }
        }
        // Duplicate Slide: the chart slide's copy right after it.
        scene.host.selectSlideRow(5);
        const QString chartTitle = deck->slides.at(5).title;
        QVERIFY(trigger(scene, QStringLiteral("slideDuplicateAction")));
        QCOMPARE(scene.host.currentSlideRow(), 6);
        QCOMPARE(deck->slides.at(6).title, chartTitle);
        QCOMPARE(deck->slides.at(6).shapes.first().kind, ShowModel::ShapeKind::Chart);
        const int count = int(deck->slides.size());
        // Saved and opened again: the slides keep their layouts.
        const QString path = dir.filePath(QStringLiteral("saved.pptx"));
        QVERIFY(scene.doc->save(path));
        const ShowModel::Presentation back = ShowModel::readPptxFile(path);
        QCOMPARE(int(back.slides.size()), count);
        QCOMPARE(layoutName(back, back.slides.at(2)), QStringLiteral("Two Content"));
        // Every step undoes.
        while (scene.doc->undoStack()->index() > steps) {
            scene.doc->undoStack()->undo();
        }
        QCOMPARE(int(deck->slides.size()), slides);
    }

    void reuseSlidesFromAnotherDeck()
    {
        for (const int width : {1280, 520}) {
            QTemporaryDir dir;
            Scene scene(QSize(width, 800));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QVERIFY(openDeck(scene, dir));
            ShowModel::Presentation *deck = scene.doc->presentation();
            const int slides = int(deck->slides.size());
            const QString other = writeFile(dir, QStringLiteral("other.pptx"), PptxFixture::build());
            QVERIFY(scene.host.slideLayouts()->openReuseFile(QUrl::fromLocalFile(other)));
            QVERIFY(dialogOpened(scene, "reuseSlidesDialog"));
            QCOMPARE(scene.host.slideLayouts()->reuseSlides().size(), 5);
            QTRY_VERIFY(scene.item(QStringLiteral("reuseSlide_3")) != nullptr || width == 520);
            grab(scene, QStringLiteral("qindashow_m82_reuse_slides_%1").arg(width));
            QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("reuseSlidesDialog"));
            dialog->setProperty("chosen", QVariantList{1, 2}); // the table and the picture
            const int steps = scene.doc->undoStack()->count();
            QMetaObject::invokeMethod(dialog, "accept");
            QTRY_COMPARE(int(deck->slides.size()), slides + 2);
            QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
            QCOMPARE(scene.host.currentSlideRow(), 1);
            QCOMPARE(deck->slides.at(2).shapes.first().kind, ShowModel::ShapeKind::Image);
            QCOMPARE(int(deck->masters.size()), 1); // this deck's formatting
            scene.doc->undoStack()->undo();
            QCOMPARE(int(deck->slides.size()), slides);
            // Keep source formatting: the other deck's master comes along.
            scene.host.slideLayouts()->reuse(QVariantList{0}, true);
            QCOMPARE(int(deck->masters.size()), 2);
        }
    }

    void browseForThemesTakesTheTemplatesMasters()
    {
        QTemporaryDir dir;
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(openDeck(scene, dir));
        ShowModel::Presentation *deck = scene.doc->presentation();
        const QString potx = writeFile(dir, QStringLiteral("design.potx"), PptxFixture::M82::templatePackage());
        QVERIFY(scene.host.designTab()->applyThemeFile(QUrl::fromLocalFile(potx)));
        QVERIFY(dialogOpened(scene, "templateScopeDialog"));
        grab(scene, QStringLiteral("qindashow_m82_template_scope_1280"));
        // Apply to Selected Slide: the current slide only.
        scene.host.selectSlideRow(1);
        QObject *ask = scene.window->findChild<QObject *>(QStringLiteral("templateScopeDialog"));
        QMetaObject::invokeMethod(ask, "tertiary");
        QTRY_COMPARE(int(deck->masters.size()), 2);
        QCOMPARE(deck->slides.at(1).master, deck->masters.last().id);
        QCOMPARE(deck->masters.last().name, QStringLiteral("Template Master"));
        scene.doc->undoStack()->undo();
        QCOMPARE(int(deck->masters.size()), 1);
        // Apply to All: the deck's master gives way to the template's.
        QVERIFY(scene.host.slideLayouts()->applyTemplate(QUrl::fromLocalFile(potx), true));
        QCOMPARE(int(deck->masters.size()), 1);
        QCOMPARE(deck->masters.first().name, QStringLiteral("Template Master"));
        QCOMPARE(scene.host.slideLayouts()->gallery().size(), 2);
        QCOMPARE(scene.host.actions()->entry(QStringLiteral("showNewSlideLayout_2"))->text(), QStringLiteral("Title and Content"));
        // A .thmx-like file (no masters) keeps changing only the theme.
        QVERIFY(!scene.host.slideLayouts()->applyTemplate(QUrl::fromLocalFile(writeFile(dir, QStringLiteral("x.thmx"), "PK")), true));
    }

    void panelExportTakesTheDecksEntries()
    {
        QTemporaryDir dir;
        ShowHost host;
        const QString path = writeFile(dir, QStringLiteral("deck.pptx"), PptxFixture::M82::layoutsDeck());
        QVERIFY(host.loadPath(path));
        ActionModelMenuSource source(host.menuTree(), host.actions(), QUuid::createUuid());
        const auto snapshot = source.snapshot();
        QVERIFY2(snapshot.complete, qPrintable(snapshot.defectCode));
        const Protocol::ValidationResult result = Protocol::validateMenuTree(snapshot.tree);
        QVERIFY2(result.accepted, qPrintable(QStringLiteral("rejected: %1 at %2").arg(result.reasonCode, result.path)));
        QStringList ids;
        const std::function<void(const Protocol::MenuItem &)> collect = [&](const Protocol::MenuItem &item) {
            if (item.kind == Protocol::MenuItemKind::Action) {
                ids.append(item.id);
            }
            for (const Protocol::MenuItem &child : item.children) {
                collect(child);
            }
        };
        for (const Protocol::MenuItem &item : snapshot.tree.items) {
            collect(item);
        }
        for (int i = 1; i <= 4; ++i) {
            QVERIFY(ids.contains(QStringLiteral("showNewSlideLayout_%1").arg(i)));
            QVERIFY(ids.contains(QStringLiteral("showSlideLayout_%1").arg(i)));
        }
        QVERIFY(!ids.contains(QStringLiteral("showNewSlideLayout_5")));
        QVERIFY(ids.contains(QStringLiteral("showResetSlideAction")));
        QVERIFY(ids.contains(QStringLiteral("showReuseSlidesAction")));
    }
};

SHOW_TEST_MAIN(ShowLayoutsUiTest)

#include "tst_show_layouts_ui.moc"
