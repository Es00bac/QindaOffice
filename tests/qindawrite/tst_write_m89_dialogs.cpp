// SPDX-License-Identifier: GPL-3.0-or-later
// M89 (D-097) through the real QindaWrite window at 1280 and 520 px: the
// dialogs that adopted QindaTK's M85 controls. Font's colours and Borders
// and Shading's border colour take "Automatic" from QindaTK ColorButton's
// own entry and the fill "No Color" (D-318) — a colour mode, shown again
// when the dialog loads; Insert ▸ Special Character is a CharacterMap
// (D-327) whose Insert puts the character at the caret and remembers it;
// Define New Bullet's Symbol… opens one in the bullet's font.
// QQO_GRAB_DIR saves the windows, popovers open.

#include "write_test_support.h"

#include "qindawrite/src/write_font.h"
#include "standard_actions.h"
#include "support/quick_dump.h"

using namespace WriteTest;

namespace {

QObject *openDialog(Scene &scene, const char *action, const char *name)
{
    if (!scene.host.actions()->trigger(QString::fromLatin1(action))) {
        return nullptr;
    }
    QObject *dialog = scene.window->findChild<QObject *>(QString::fromLatin1(name));
    if (dialog == nullptr || !QTest::qWaitFor([dialog] { return dialog->property("opened").toBool(); })) {
        return nullptr;
    }
    QTest::qWait(150);
    return dialog;
}

// A dialog's part: Popups' contents are not in the window's item tree.
QQuickItem *part(QObject *dialog, const char *name)
{
    return dialog->findChild<QQuickItem *>(QString::fromLatin1(name));
}

void grab(Scene &scene, const char *name, int width)
{
    QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindawrite_m89_%1_%2").arg(QLatin1String(name)).arg(width));
}

} // namespace

class WriteM89DialogsTest : public QObject {
    Q_OBJECT
private slots:
    void colourModesAndCharacterMaps_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }
    void colourModesAndCharacterMaps()
    {
        QFETCH(int, width);
        Scene scene(QSize(width, width == 1280 ? 800 : 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.selectFirstWord();

        // Font: Automatic is the button's entry; a colour, then Automatic.
        QObject *font = openDialog(scene, "writeFontAction", "fontDialog");
        QVERIFY(font != nullptr);
        QQuickItem *fontColor = part(font, "fontColor");
        QVERIFY(fontColor != nullptr);
        QVERIFY(fontColor->property("automaticEntry").toBool());
        QCOMPARE(fontColor->property("colorMode").toString(), QStringLiteral("automatic"));
        QVERIFY(part(font, "fontColorAuto") == nullptr); // the separate button is gone
        QMetaObject::invokeMethod(fontColor, "colorSelected", Q_ARG(QColor, QColor(QStringLiteral("#c00000"))));
        QCOMPARE(font->property("color").toString(), QStringLiteral("#c00000"));
        QTRY_COMPARE(fontColor->property("colorMode").toString(), QStringLiteral("color"));
        QMetaObject::invokeMethod(fontColor, "click");
        QTest::qWait(200);
        grab(scene, "font_colour", width);
        QMetaObject::invokeMethod(fontColor, "automaticSelected");
        QCOMPARE(font->property("color").toString(), QString());
        QTRY_COMPARE(fontColor->property("colorMode").toString(), QStringLiteral("automatic"));
        // A colour loaded with the selection shows as a colour.
        QVariantMap info = scene.host.fontController()->fontInfo();
        info.insert(QStringLiteral("color"), QStringLiteral("#2f5496"));
        info.insert(QStringLiteral("underlineColor"), QString());
        QMetaObject::invokeMethod(font, "load", Q_ARG(QVariant, info));
        QTRY_COMPARE(fontColor->property("colorMode").toString(), QStringLiteral("color"));
        QCOMPARE(fontColor->property("color").value<QColor>(), QColor(QStringLiteral("#2f5496")));
        QCOMPARE(part(font, "fontUnderlineColor")->property("colorMode").toString(), QStringLiteral("automatic"));
        QMetaObject::invokeMethod(font, "close");

        // Borders and Shading: an automatic border again; No Color for the fill.
        QObject *borders = openDialog(scene, "writeBordersShadingAction", "bordersShadingDialog");
        QVERIFY(borders != nullptr);
        QQuickItem *pen = part(borders, "bordersColor");
        QQuickItem *fill = part(borders, "shadingFill");
        QVERIFY(pen != nullptr && fill != nullptr);
        QVERIFY(part(borders, "shadingNoColor") == nullptr);
        QMetaObject::invokeMethod(pen, "colorSelected", Q_ARG(QColor, QColor(QStringLiteral("#4472c4"))));
        QVERIFY(!borders->property("penAuto").toBool());
        QTRY_COMPARE(pen->property("colorMode").toString(), QStringLiteral("color"));
        QMetaObject::invokeMethod(pen, "automaticSelected");
        QVERIFY(borders->property("penAuto").toBool());
        QTRY_COMPARE(pen->property("colorMode").toString(), QStringLiteral("automatic"));
        QMetaObject::invokeMethod(fill, "colorSelected", Q_ARG(QColor, QColor(QStringLiteral("#fff2cc"))));
        QCOMPARE(borders->property("fill").toString(), QStringLiteral("#fff2cc"));
        QTRY_COMPARE(fill->property("colorMode").toString(), QStringLiteral("color"));
        QMetaObject::invokeMethod(fill, "click");
        QTest::qWait(200);
        grab(scene, "shading_fill", width);
        QMetaObject::invokeMethod(fill, "noColorSelected");
        QCOMPARE(borders->property("fill").toString(), QString());
        QVERIFY(borders->property("fillChanged").toBool());
        QTRY_COMPARE(fill->property("colorMode").toString(), QStringLiteral("none"));
        QMetaObject::invokeMethod(borders, "close");

        // Insert ▸ Special Character: a character map; Insert inserts.
        QObject *symbols = openDialog(scene, QindaOffice::Quick::ActionId::kInsertSymbol, "symbolDialog");
        QVERIFY(symbols != nullptr);
        QQuickItem *map = part(symbols, "symbolCharacterMap");
        QVERIFY(map != nullptr && map->isVisible());
        QVERIFY(!map->property("recent").toList().isEmpty()); // Word's special characters
        grab(scene, "symbol", width);
        QVariant chosen;
        QMetaObject::invokeMethod(map, "choose", Q_RETURN_ARG(QVariant, chosen), Q_ARG(QVariant, QStringLiteral("→")));
        QVERIFY(chosen.toBool());
        QTRY_VERIFY(scene.doc->textDocument()->toPlainText().contains(QStringLiteral("→")));
        QCOMPARE(map->property("recent").toList().first().toString(), QStringLiteral("→"));
        QVERIFY(symbols->property("opened").toBool()); // stays open for the next one
        QMetaObject::invokeMethod(symbols, "close");

        // Define New Bullet ▸ Symbol…: the map in the bullet's font.
        QObject *bullet = openDialog(scene, "writeDefineBulletAction", "defineBulletDialog");
        QVERIFY(bullet != nullptr);
        QMetaObject::invokeMethod(part(bullet, "bulletSymbolMore"), "clicked");
        QObject *popover = bullet->findChild<QObject *>(QStringLiteral("bulletSymbolPopover"));
        QVERIFY(popover != nullptr);
        QTRY_VERIFY(popover->property("opened").toBool());
        QTest::qWait(150);
        grab(scene, "bullet_symbol", width);
        QQuickItem *bulletMap = popover->findChild<QQuickItem *>(QStringLiteral("bulletCharacterMap"));
        QVERIFY(bulletMap != nullptr);
        QMetaObject::invokeMethod(bulletMap, "choose", Q_RETURN_ARG(QVariant, chosen), Q_ARG(QVariant, QStringLiteral("★")));
        QCOMPARE(bullet->property("glyph").toString(), QStringLiteral("★"));
        QTRY_VERIFY(!popover->property("opened").toBool());
        QMetaObject::invokeMethod(bullet, "accept");
        QTRY_VERIFY(!bullet->property("visible").toBool());
        // The paragraph's bullet is the character chosen.
        QObject *lists = bullet->property("lists").value<QObject *>();
        QVariantMap bulletNow;
        QVERIFY(QMetaObject::invokeMethod(lists, "bulletInfo", Q_RETURN_ARG(QVariantMap, bulletNow)));
        QCOMPARE(bulletNow.value(QStringLiteral("glyph")).toString(), QStringLiteral("★"));
    }
};

WRITE_TEST_MAIN(WriteM89DialogsTest)

#include "tst_write_m89_dialogs.moc"
