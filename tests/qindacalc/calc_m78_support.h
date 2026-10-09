// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared by the M78 (D-087) window tests: commands by id, the Tk.Dialog
// popups by objectName (waited for, since a dialog has an enter
// transition), fields set the way a user's typing ends up, and dialogs
// answered with their own accept/reject.

#include "calc_test_support.h"

#include "standard_actions.h"
#include "support/quick_dump.h"

namespace CalcTest::M78 {

namespace ActionId = QindaOffice::Quick::ActionId;

inline void trigger(Scene &scene, const char *id)
{
    scene.host.actions()->trigger(QString::fromLatin1(id));
}

inline QObject *popup(Scene &scene, const char *name)
{
    return scene.window->findChild<QObject *>(QString::fromLatin1(name));
}

inline bool dialogOpened(Scene &scene, const char *name)
{
    QObject *dialog = popup(scene, name);
    if (dialog == nullptr || !QTest::qWaitFor([dialog] { return dialog->property("opened").toBool(); })) {
        return false;
    }
    QTest::qWait(250);
    return true;
}

inline bool dialogClosed(Scene &scene, const char *name)
{
    QObject *dialog = popup(scene, name);
    return dialog != nullptr && QTest::qWaitFor([dialog] { return !dialog->property("visible").toBool(); });
}

// accept() / reject() and wait for the exit transition (the modal scrim
// takes clicks until it ends).
inline bool answer(Scene &scene, const char *name, bool accept)
{
    QObject *dialog = popup(scene, name);
    if (dialog == nullptr) {
        return false;
    }
    QMetaObject::invokeMethod(dialog, accept ? "accept" : "reject");
    return dialogClosed(scene, name);
}

// A text field's text, as typing leaves it (QML handlers that listen to
// textEdited are told through `edited` handlers the tests call where needed).
inline bool setText(Scene &scene, const char *name, const QString &text)
{
    QQuickItem *field = scene.item(QString::fromLatin1(name));
    return field != nullptr && field->setProperty("text", text);
}

inline QString text(Scene &scene, const char *name)
{
    QQuickItem *item = scene.item(QString::fromLatin1(name));
    return item != nullptr ? item->property("text").toString() : QString();
}

inline QindaOffice::Calc::Value cell(Scene &scene, int sheet, int row, int col)
{
    return scene.doc->workbook()->cellValue(sheet, row, col);
}

inline void type(Scene &scene, int row, int col, const QString &input)
{
    scene.doc->applyInput({row, col}, input);
}

// The grabs the reviewer looks at (QQO_GRAB_DIR), after the window settled.
inline void grab(Scene &scene, const QString &name)
{
    QTest::qWait(150);
    QindaOffice::Test::saveGrab(scene.window, name);
}

} // namespace CalcTest::M78
