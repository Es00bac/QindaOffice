// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_actions.h"

#include "standard_actions.h"

#include <QActionGroup>
#include <QIcon>
#include <QKeySequence>
#include <QObject>

namespace QindaOffice::Apps::Write {

QList<QAction *> WriteActions::all() const
{
    return {bold,        italic,     underline,    strikeOut,      textColor,
            clearFormat, alignLeft,  alignCenter,  alignRight,     alignJustify,
            listBullet,  listNumbered, indentMore, indentLess,
            insertTable, insertImage, insertColumns, insertPageBreak};
}

namespace {

QAction *make(QObject *parent, const char *id, const QString &text,
              const QKeySequence &shortcut = {}, const char *iconName = nullptr)
{
    auto *action = new QAction(text, parent);
    action->setObjectName(QString::fromLatin1(id));
    if (iconName != nullptr) {
        action->setIcon(QIcon::fromTheme(QString::fromLatin1(iconName)));
    }
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    return action;
}

} // namespace

WriteActions createWriteActions(QObject *parent)
{
    using namespace Shell::ActionId;
    WriteActions a;

    a.bold = make(parent, kFormatBold, QObject::tr("&Bold"),
                  QKeySequence::Bold, "format-text-bold");
    a.italic = make(parent, kFormatItalic, QObject::tr("&Italic"),
                    QKeySequence::Italic, "format-text-italic");
    a.underline = make(parent, kFormatUnderline, QObject::tr("&Underline"),
                       QKeySequence::Underline, "format-text-underline");
    a.strikeOut = make(parent, kFormatStrikeOut, QObject::tr("&Strikethrough"), {},
                       "format-text-strikethrough");
    for (QAction *action : {a.bold, a.italic, a.underline, a.strikeOut}) {
        action->setCheckable(true);
    }
    a.textColor = make(parent, kFormatTextColor, QObject::tr("Text &Color…"), {},
                       "format-text-color");
    a.clearFormat = make(parent, kFormatClearFormat, QObject::tr("Clear Direct &Formatting"));

    auto *alignGroup = new QActionGroup(parent);
    alignGroup->setObjectName(QStringLiteral("alignGroup"));
    alignGroup->setExclusive(true);
    a.alignLeft = make(parent, kFormatAlignLeft, QObject::tr("Align &Left"),
                       QKeySequence(QStringLiteral("Ctrl+L")), "format-justify-left");
    a.alignCenter = make(parent, kFormatAlignCenter, QObject::tr("Align &Center"),
                         QKeySequence(QStringLiteral("Ctrl+E")), "format-justify-center");
    a.alignRight = make(parent, kFormatAlignRight, QObject::tr("Align &Right"),
                        QKeySequence(QStringLiteral("Ctrl+R")), "format-justify-right");
    a.alignJustify = make(parent, kFormatAlignJustify, QObject::tr("&Justify"),
                          QKeySequence(QStringLiteral("Ctrl+J")), "format-justify-fill");
    for (QAction *action : {a.alignLeft, a.alignCenter, a.alignRight, a.alignJustify}) {
        action->setCheckable(true);
        alignGroup->addAction(action);
    }

    // "format-list-unordered/-ordered" are Breeze names (not in the base
    // freedesktop spec); the text label is the fallback when absent.
    a.listBullet = make(parent, kFormatListBullet, QObject::tr("&Bullet List"), {},
                        "format-list-unordered");
    a.listNumbered = make(parent, kFormatListNumbered, QObject::tr("&Numbered List"), {},
                          "format-list-ordered");
    a.listBullet->setCheckable(true);
    a.listNumbered->setCheckable(true);

    a.indentMore = make(parent, kFormatIndentMore, QObject::tr("&Increase Indent"), {},
                        "format-indent-more");
    a.indentLess = make(parent, kFormatIndentLess, QObject::tr("&Decrease Indent"), {},
                        "format-indent-less");

    a.insertTable = make(parent, kInsertTable, QObject::tr("&Table…"), {}, "insert-table");
    a.insertImage = make(parent, kInsertImage, QObject::tr("&Image…"), {}, "insert-image");
    a.insertColumns = make(parent, kInsertColumns, QObject::tr("&Columns…"));
    a.insertPageBreak =
        make(parent, kInsertPageBreak, QObject::tr("&Page Break"),
             QKeySequence(QStringLiteral("Ctrl+Return")));

    return a;
}

} // namespace QindaOffice::Apps::Write
