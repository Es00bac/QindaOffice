// SPDX-License-Identifier: GPL-3.0-or-later
// M75 (D-084) through the real window: the Design tab — themes and their
// variants, slide size with PowerPoint's Maximize / Ensure Fit question,
// Format Background — and the Slide Master view — layouts, placeholders,
// text styles. Every change is ONE undo step, and what a master, layout or
// theme says reaches every slide that inherits it (the deck re-resolves
// through the pptx reader, pptx_design.h). Grabs land in QQO_GRAB_DIR.

#include "show_test_support.h"

#include "show_model/pptx_fixture_m75.h"
#include "support/quick_dump.h"

#include "qindashow/src/show_background.h"
#include "qindashow/src/show_design_tab.h"
#include "qindashow/src/show_master_view.h"

#include "action_model.h"
#include "pptx_design.h"
#include "pptx_reader.h"
#include "show_theme.h"

#include <QSignalSpy>
#include <QUndoStack>

using namespace ShowTest;

namespace {

QString sid(const char *id)
{
    return QString::fromLatin1(id);
}

// The hand-built PowerPoint deck (tests/show_model/pptx_fixture*.h), open.
bool openFixture(Scene &scene, QTemporaryDir &dir, const QByteArray &zip = PptxFixture::build())
{
    const QString path = dir.filePath(QStringLiteral("deck.pptx"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(zip) != zip.size()) {
        return false;
    }
    file.close();
    return scene.host.loadPath(path);
}

const ShowModel::Shape *placeholderOf(const ShowModel::Slide &slide, const QString &type)
{
    for (const ShowModel::Shape &shape : slide.shapes) {
        if (shape.placeholder.type == type) {
            return &shape;
        }
    }
    return nullptr;
}

bool panelShown(Scene &scene, const QString &item)
{
    QQuickItem *pane = scene.item(item);
    return pane != nullptr && pane->isVisible();
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

class ShowDesignTabTest : public QObject {
    Q_OBJECT
private slots:
    void themesAndVariantsAreOneUndoStepEach();
    void slideSizeAsksMaximizeOrEnsureFit();
    void customSlideSizeDialogAnswersInPoints();
    void formatBackgroundPaneEditsTheSlide();
    void slideMasterViewEditsFlowToSlides();
    void masterViewTextStylesReachTheSlides();
    void placeholderMovedOnTheLayoutMovesItsSlides();
};

void ShowDesignTabTest::themesAndVariantsAreOneUndoStepEach()
{
    QTemporaryDir dir;
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(openFixture(scene, dir));
    const ShowModel::Presentation *deck = scene.doc->presentation();
    const QString master = deck->masters.first().id;
    const QString before = ShowModel::deckTheme(*deck, master).name;
    const int steps = scene.doc->undoStack()->count();

    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showTheme_Harbor")));
    QCOMPARE(ShowModel::deckTheme(*deck, master).name, QStringLiteral("Harbor"));
    QVERIFY(scene.host.actions()->entry(QStringLiteral("showTheme_Harbor"))->isChecked());
    QCOMPARE(scene.doc->undoStack()->count(), steps + 1);

    ShowModel::ThemeColors ember;
    for (const ShowModel::ThemeColors &set : ShowModel::builtInColorSets()) {
        if (set.name == u"Ember") {
            ember = set;
        }
    }
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showThemeColors_Ember")));
    QCOMPARE(ShowModel::deckTheme(*deck, master).colors.values, ember.values);
    QVERIFY(scene.host.actions()->entry(QStringLiteral("showThemeColors_Ember"))->isChecked());

    // Fonts: the slides' title follows the theme's heading font (+mj-lt).
    ShowModel::ThemeFonts bookish;
    for (const ShowModel::ThemeFonts &set : ShowModel::builtInFontSets()) {
        if (set.name == u"Bookish") {
            bookish = set;
        }
    }
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showThemeFonts_Bookish")));
    QCOMPARE(ShowModel::deckTheme(*deck, master).fonts.major, bookish.major);
    const ShowModel::Shape *title = placeholderOf(deck->slides.first(), QStringLiteral("title"));
    if (title == nullptr) {
        title = placeholderOf(deck->slides.first(), QStringLiteral("ctrTitle"));
    }
    QVERIFY(title != nullptr && !title->paragraphs.isEmpty() && !title->paragraphs.first().runs.isEmpty());
    QCOMPARE(title->paragraphs.first().runs.first().fontFamily, bookish.major);

    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showThemeEffects_Flat")));
    QCOMPARE(ShowModel::deckTheme(*deck, master).effects, QStringLiteral("Flat"));
    QCOMPARE(scene.doc->undoStack()->count(), steps + 4);
    grab(scene, QStringLiteral("qindashow_m75_theme"), QStringLiteral("design"));

    for (int i = 0; i < 4; ++i) {
        scene.doc->undoStack()->undo();
    }
    QCOMPARE(ShowModel::deckTheme(*deck, master).name, before);
    QVERIFY(!scene.host.actions()->entry(QStringLiteral("showTheme_Harbor"))->isChecked());
}

void ShowDesignTabTest::slideSizeAsksMaximizeOrEnsureFit()
{
    QTemporaryDir dir;
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(openFixture(scene, dir));
    ShowModel::Presentation *deck = scene.doc->presentation();
    QCOMPARE(deck->slideSize, QSizeF(960, 540));
    QVERIFY(scene.host.actions()->entry(sid(QindaOffice::Apps::Show::DesignActionId::kSizeWidescreen))->isChecked());
    const QRectF wide = deck->slides.first().shapes.first().rect;
    QSignalSpy asked(scene.host.designTab(), &QindaOffice::Apps::Show::ShowDesignTab::scaleChoiceRequested);

    // Widescreen to 4:3 shrinks the content: PowerPoint asks how.
    QVERIFY(scene.host.actions()->trigger(sid(QindaOffice::Apps::Show::DesignActionId::kSizeStandard)));
    QCOMPARE(asked.count(), 1);
    QCOMPARE(deck->slideSize, QSizeF(960, 540)); // nothing changes before the answer
    QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("slideScaleDialog"));
    QVERIFY(dialog != nullptr);
    QTRY_VERIFY(dialog->property("opened").toBool());
    grab(scene, QStringLiteral("qindashow_m75_scale_choice"));
    QMetaObject::invokeMethod(dialog, "tertiary"); // Ensure Fit
    QTRY_VERIFY(!dialog->property("opened").toBool());
    QCOMPARE(deck->slideSize, QSizeF(720, 540));
    const QRectF fit = deck->slides.first().shapes.first().rect;
    QVERIFY2(qAbs(fit.width() - wide.width() * 0.75) < 1.0, qPrintable(QString::number(fit.width())));
    QVERIFY(scene.host.actions()->entry(sid(QindaOffice::Apps::Show::DesignActionId::kSizeStandard))->isChecked());

    // One undo step brings the deck back as it was.
    scene.doc->undoStack()->undo();
    QCOMPARE(deck->slideSize, QSizeF(960, 540));
    QCOMPARE(deck->slides.first().shapes.first().rect, wide);

    // Maximize keeps the content's size (the height fills the new slide).
    scene.host.designTab()->applySlideSize(720, 540, true);
    QCOMPARE(deck->slideSize, QSizeF(720, 540));
    QVERIFY(deck->slides.first().shapes.first().rect.width() > fit.width());

    // The size is written as PowerPoint writes it and reads back.
    const QString saved = dir.filePath(QStringLiteral("sized.pptx"));
    QVERIFY(scene.doc->save(saved));
    QFile file(saved);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString error;
    const ShowModel::Presentation back = ShowModel::readPptxData(file.readAll(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.slideSize, QSizeF(720, 540));
}

void ShowDesignTabTest::customSlideSizeDialogAnswersInPoints()
{
    QTemporaryDir dir;
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(openFixture(scene, dir));
    QVERIFY(scene.host.actions()->trigger(sid(QindaOffice::Apps::Show::DesignActionId::kSizeCustom)));
    QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("slideSizeDialog"));
    QVERIFY(dialog != nullptr);
    QTRY_VERIFY(dialog->property("opened").toBool());
    grab(scene, QStringLiteral("qindashow_m75_slide_size"));
    QQuickItem *preset = qobject_cast<QQuickItem *>(scene.window->findChild<QObject *>(QStringLiteral("slideSizePreset")));
    QVERIFY(preset != nullptr);
    QCOMPARE(preset->property("currentIndex").toInt(), 0); // Widescreen

    // A banner (8 x 1 in) is smaller in both directions: the question comes.
    QSignalSpy asked(scene.host.designTab(), &QindaOffice::Apps::Show::ShowDesignTab::scaleChoiceRequested);
    QMetaObject::invokeMethod(dialog, "sizeChosen", Q_ARG(double, 576.0), Q_ARG(double, 72.0));
    QCOMPARE(asked.count(), 1);
    QCOMPARE(asked.first().at(0).toDouble(), 576.0);
    QCOMPARE(asked.first().at(1).toDouble(), 72.0);

    // A larger size needs no question: content keeps its size.
    const int steps = scene.doc->undoStack()->count();
    scene.host.designTab()->requestSlideSize(1080, 540);
    QCOMPARE(asked.count(), 1);
    QCOMPARE(scene.doc->presentation()->slideSize, QSizeF(1080, 540));
    QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
}

void ShowDesignTabTest::formatBackgroundPaneEditsTheSlide()
{
    QTemporaryDir dir;
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(openFixture(scene, dir));
    ShowModel::Presentation *deck = scene.doc->presentation();
    QindaOffice::Apps::Show::ShowBackground *bg = scene.host.background();
    QVERIFY(!panelShown(scene, QStringLiteral("formatBackgroundPane")));
    QVERIFY(scene.host.actions()->trigger(sid(QindaOffice::Apps::Show::DesignActionId::kFormatBackground)));
    QTRY_VERIFY(panelShown(scene, QStringLiteral("formatBackgroundPane")));
    QVERIFY(scene.host.actions()->entry(sid(QindaOffice::Apps::Show::DesignActionId::kFormatBackground))->isChecked());

    const int steps = scene.doc->undoStack()->count();
    bg->setColor(QColor(0x22, 0x66, 0xAA));
    QVERIFY(deck->slides.first().background.set);
    QCOMPARE(deck->slides.first().background.color.rgba(), QColor(0x22, 0x66, 0xAA).rgba());
    QCOMPARE(bg->kind(), QStringLiteral("solid"));
    bg->setTransparency(40);
    QCOMPARE(bg->transparency(), 40);
    bg->setKind(QStringLiteral("gradient"));
    QCOMPARE(bg->kind(), QStringLiteral("gradient"));
    QCOMPARE(int(bg->stops().size()), 2);
    bg->setKind(QStringLiteral("pattern"));
    QCOMPARE(deck->slides.first().background.style.pattern, QStringLiteral("pct50"));
    bg->setTexture(1);
    QCOMPARE(bg->kind(), QStringLiteral("picture"));
    QVERIFY(bg->tile());
    QCOMPARE(scene.doc->undoStack()->count(), steps + 5);
    grab(scene, QStringLiteral("qindashow_m75_format_background"), QStringLiteral("design"));

    // Hide Background Graphics: the slide stops showing the master's shapes.
    QVERIFY(scene.host.actions()->trigger(sid(QindaOffice::Apps::Show::DesignActionId::kHideBackgroundGraphics)));
    QVERIFY(!deck->slides.first().showMasterShapes);

    // Apply to All: the master takes it; no slide keeps one of its own.
    bg->setColor(QColor(0x40, 0x80, 0x40));
    QVERIFY(scene.host.actions()->trigger(sid(QindaOffice::Apps::Show::DesignActionId::kBackgroundApplyToAll)));
    for (const ShowModel::Slide &slide : deck->slides) {
        QVERIFY(!slide.background.set);
    }
    QCOMPARE(deck->masters.first().background.rgb(), QColor(0x40, 0x80, 0x40).rgb());
    scene.host.selectSlideRow(3);
    QCOMPARE(bg->color().rgb(), QColor(0x40, 0x80, 0x40).rgb());

    // Reset Background lets a slide's own background go again.
    bg->setColor(QColor(Qt::yellow));
    QVERIFY(deck->slides.at(3).background.set);
    QVERIFY(scene.host.actions()->trigger(sid(QindaOffice::Apps::Show::DesignActionId::kBackgroundReset)));
    QVERIFY(!deck->slides.at(3).background.set);

    // Closing the panel unchecks the command.
    QVERIFY(scene.host.actions()->trigger(sid(QindaOffice::Apps::Show::DesignActionId::kFormatBackground)));
    QTRY_VERIFY(!panelShown(scene, QStringLiteral("formatBackgroundPane")));
}

void ShowDesignTabTest::slideMasterViewEditsFlowToSlides()
{
    QTemporaryDir dir;
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(openFixture(scene, dir));
    ShowModel::Presentation *deck = scene.doc->presentation();
    QindaOffice::Apps::Show::ShowMasterView *view = scene.host.masterView();
    QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
    QVERIFY(ribbon != nullptr);

    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showSlideMasterViewAction")));
    QVERIFY(view->active());
    QTRY_COMPARE(ribbon->property("currentTabId").toString(), QStringLiteral("slideMaster"));
    QTRY_VERIFY(ribbon->property("contextIds").toStringList().contains(QStringLiteral("masterView")));
    QTRY_VERIFY(panelShown(scene, QStringLiteral("masterPane")));
    QVERIFY(!panelShown(scene, QStringLiteral("slideSorter")));
    const int listed = int(view->items().size());
    QCOMPARE(listed, int(deck->masters.size() + deck->layouts.size()));
    QVERIFY(scene.item(QStringLiteral("masterThumb_0")) != nullptr);
    grab(scene, QStringLiteral("qindashow_m75_slide_master"));

    // A layout: insert one after it, rename it, delete it (no slide uses it).
    view->setCurrentIndex(1);
    const QString layout = view->currentLayoutId();
    QVERIFY(!layout.isEmpty());
    const int steps = scene.doc->undoStack()->count();
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showInsertLayoutAction")));
    QCOMPARE(int(view->items().size()), listed + 1);
    QVERIFY(view->currentLayoutId() != layout);
    view->renameCurrent(QStringLiteral("Quote"));
    QCOMPARE(view->currentName(), QStringLiteral("Quote"));
    QVERIFY(scene.host.actions()->entry(QStringLiteral("showDeleteLayoutAction"))->isEnabled());
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showDeleteLayoutAction")));
    QCOMPARE(int(view->items().size()), listed);
    QCOMPARE(scene.doc->undoStack()->count(), steps + 3);

    // The layout slides use cannot be deleted.
    view->setCurrentIndex(1);
    QVERIFY(!scene.host.actions()->entry(QStringLiteral("showDeleteLayoutAction"))->isEnabled());

    // A picture placeholder on the layout reaches the layout's model.
    const qsizetype before = deck->findLayout(layout)->placeholders.size();
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showInsertPicturePlaceholderAction")));
    QCOMPARE(deck->findLayout(layout)->placeholders.size(), before + 1);
    scene.doc->undoStack()->undo();
    QCOMPARE(deck->findLayout(layout)->placeholders.size(), before);

    // Close Master View: the slides again.
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showCloseMasterViewAction")));
    QVERIFY(!view->active());
    QTRY_VERIFY(panelShown(scene, QStringLiteral("slideSorter")));
    QTRY_VERIFY(!ribbon->property("contextIds").toStringList().contains(QStringLiteral("masterView")));
    QTRY_COMPARE(ribbon->property("currentTabId").toString(), QStringLiteral("home")); // the tab before
}

void ShowDesignTabTest::masterViewTextStylesReachTheSlides()
{
    QTemporaryDir dir;
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(openFixture(scene, dir));
    ShowModel::Presentation *deck = scene.doc->presentation();
    QindaOffice::Apps::Show::ShowMasterView *view = scene.host.masterView();
    view->enter();
    view->setCurrentIndex(0); // the master
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showMasterTextStylesAction")));
    QTRY_VERIFY(panelShown(scene, QStringLiteral("textStylesPane")));
    QVERIFY(scene.item(QStringLiteral("textStyleSize_0")) != nullptr);
    grab(scene, QStringLiteral("qindashow_m75_text_styles"));
    QCOMPARE(int(view->styleLevels().size()), 6); // Title + Text levels 1-5

    view->setStyleLevel(0, {{QStringLiteral("size"), 50.0}, {QStringLiteral("bold"), true}});
    view->close();
    QTRY_VERIFY(!panelShown(scene, QStringLiteral("textStylesPane")));
    const ShowModel::Shape *title = placeholderOf(deck->slides.first(), QStringLiteral("title"));
    if (title == nullptr) {
        title = placeholderOf(deck->slides.first(), QStringLiteral("ctrTitle"));
    }
    QVERIFY(title != nullptr && !title->paragraphs.first().runs.isEmpty());
    // The fixture's slide title sets no size or weight of its own.
    QCOMPARE(title->paragraphs.first().runs.first().pointSize, 50.0);
    QVERIFY(title->paragraphs.first().runs.first().bold);
    scene.doc->undoStack()->undo();
    title = placeholderOf(deck->slides.first(), QStringLiteral("title"));
    if (title == nullptr) {
        title = placeholderOf(deck->slides.first(), QStringLiteral("ctrTitle"));
    }
    QCOMPARE(title->paragraphs.first().runs.first().pointSize, 44.0);
}

// The Slide Master view's canvas edits the layout: a placeholder moved there
// is ONE undo step (committed after the canvas's gesture), and the slides
// whose placeholder inherits its place follow.
void ShowDesignTabTest::placeholderMovedOnTheLayoutMovesItsSlides()
{
    QTemporaryDir dir;
    Scene scene(QSize(1280, 800));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(openFixture(scene, dir));
    ShowModel::Presentation *deck = scene.doc->presentation();
    QindaOffice::Apps::Show::ShowMasterView *view = scene.host.masterView();
    const QRectF slideTitle = placeholderOf(deck->slides.first(), QStringLiteral("title"))->rect;
    view->enter();
    view->setCurrentIndex(1); // "Title and Content", every slide's layout
    const QString layout = view->currentLayoutId();
    const ShowModel::Shape *onCanvas = placeholderOf(*scene.canvas->slide(), QStringLiteral("title"));
    QVERIFY(onCanvas != nullptr);
    const QString id = onCanvas->id;
    const int steps = scene.doc->undoStack()->count();
    QVERIFY(scene.canvas->editShape(id, QStringLiteral("Move"), [](ShowModel::Shape &shape) {
        shape.rect.translate(0, 30);
        return true;
    }));
    QTRY_COMPARE(scene.doc->undoStack()->count(), steps + 1);
    const ShowModel::SlideLayout *edited = deck->findLayout(layout);
    QVERIFY(edited != nullptr);
    const ShowModel::Shape *placeholder = nullptr;
    for (const ShowModel::Shape &shape : edited->placeholders) {
        placeholder = shape.placeholder.type == u"title" ? &shape : placeholder;
    }
    QVERIFY(placeholder != nullptr);
    QCOMPARE(placeholder->rect.top(), slideTitle.top() + 30);
    view->close();
    QCOMPARE(placeholderOf(deck->slides.first(), QStringLiteral("title"))->rect, slideTitle.translated(0, 30));
    scene.doc->undoStack()->undo();
    QCOMPARE(placeholderOf(deck->slides.first(), QStringLiteral("title"))->rect, slideTitle);
}

SHOW_TEST_MAIN(ShowDesignTabTest)

#include "tst_show_design_tab.moc"
