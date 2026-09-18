// SPDX-License-Identifier: GPL-3.0-or-later
#include "write_actions.h"

#include "standard_actions.h"

#include <QKeySequence>

namespace QindaOffice::Apps::Write {

using Quick::ActionEntry;

QList<ActionEntry *> WriteActions::all() const
{
    return {bold,        italic,     underline,    strikeOut,      textColor,
            clearFormat, alignLeft,  alignCenter,  alignRight,     alignJustify,
            listBullet,  listNumbered, indentMore, indentLess,
            insertTable, insertImage, insertColumns, insertPageBreak};
}

namespace {

ActionEntry *make(Quick::ActionModel *model, const char *id, const QString &text,
                  const QKeySequence &shortcut = {}, const char *iconName = nullptr)
{
    return model->add(QString::fromLatin1(id), text, shortcut,
                      iconName != nullptr ? QString::fromLatin1(iconName) : QString());
}

} // namespace

WriteActions createWriteActions(Quick::ActionModel *model)
{
    using namespace Quick::ActionId;
    WriteActions a;

    a.bold = make(model, kFormatBold, QObject::tr("&Bold"), QKeySequence::Bold, "bold");
    a.italic = make(model, kFormatItalic, QObject::tr("&Italic"), QKeySequence::Italic, "italic");
    a.underline = make(model, kFormatUnderline, QObject::tr("&Underline"), QKeySequence::Underline,
                       "underline");
    a.strikeOut = make(model, kFormatStrikeOut, QObject::tr("&Strikethrough"), {}, "strikethrough");
    for (ActionEntry *entry : {a.bold, a.italic, a.underline, a.strikeOut}) {
        entry->setCheckable(true);
    }
    a.textColor = make(model, kFormatTextColor, QObject::tr("Text &Colour…"), {}, "baseline");
    a.clearFormat = make(model, kFormatClearFormat, QObject::tr("Clear Direct &Formatting"));

    a.alignLeft = make(model, kFormatAlignLeft, QObject::tr("Align &Left"),
                       QKeySequence(QStringLiteral("Ctrl+L")), "align-left");
    a.alignCenter = make(model, kFormatAlignCenter, QObject::tr("Align &Center"),
                         QKeySequence(QStringLiteral("Ctrl+E")), "align-center");
    a.alignRight = make(model, kFormatAlignRight, QObject::tr("Align &Right"),
                        QKeySequence(QStringLiteral("Ctrl+R")), "align-right");
    a.alignJustify = make(model, kFormatAlignJustify, QObject::tr("&Justify"),
                          QKeySequence(QStringLiteral("Ctrl+J")), "align-justify");
    for (ActionEntry *entry : {a.alignLeft, a.alignCenter, a.alignRight, a.alignJustify}) {
        entry->setCheckable(true);
        entry->setRadioGroup(QStringLiteral("writeAlign"));
    }

    a.listBullet = make(model, kFormatListBullet, QObject::tr("&Bullet List"), {}, "list");
    a.listNumbered = make(model, kFormatListNumbered, QObject::tr("&Numbered List"), {}, "list-ordered");
    a.listBullet->setCheckable(true);
    a.listNumbered->setCheckable(true);

    a.indentMore = make(model, kFormatIndentMore, QObject::tr("&Increase Indent"), {}, "indent-increase");
    a.indentLess = make(model, kFormatIndentLess, QObject::tr("&Decrease Indent"), {}, "indent-decrease");

    a.insertTable = make(model, kInsertTable, QObject::tr("&Table…"), {}, "table");
    a.insertImage = make(model, kInsertImage, QObject::tr("&Image…"), {}, "image");
    a.insertColumns = make(model, kInsertColumns, QObject::tr("&Columns…"), {}, "columns-2");
    a.insertPageBreak = make(model, kInsertPageBreak, QObject::tr("&Page Break"),
                             QKeySequence(QStringLiteral("Ctrl+Return")));
    return a;
}

} // namespace QindaOffice::Apps::Write
