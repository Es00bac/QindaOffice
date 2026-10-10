// SPDX-License-Identifier: GPL-3.0-or-later
#include "note_templates.h"

#include "note_rich_import.h"

#include <qindatk/rich_text_document.h>

#include <QCoreApplication>
#include <QLocale>

namespace QindaOffice::Apps::Notes::Templates {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaOffice::Apps::Notes::Templates", text);
}

// U+E010 marks an unticked To Do line (note_rich_import.cpp's sentinel).
const QString kTodo = QString(QChar(0xE010));

QJsonArray richBlocks(const QString &html)
{
    return {RichImport::textBlock(RichImport::fromHtml(html))};
}

} // namespace

QStringList ids()
{
    return {QStringLiteral("blank"), QStringLiteral("lined"), QStringLiteral("grid"), QStringLiteral("meeting"),
            QStringLiteral("todo")};
}

QString label(const QString &id)
{
    if (id == QLatin1String("lined")) {
        return tr("&Lined");
    }
    if (id == QLatin1String("grid")) {
        return tr("&Grid");
    }
    if (id == QLatin1String("meeting")) {
        return tr("&Meeting Notes");
    }
    if (id == QLatin1String("todo")) {
        return tr("&To-Do List");
    }
    return tr("&Blank");
}

QString iconName(const QString &id)
{
    if (id == QLatin1String("lined")) {
        return QStringLiteral("rows-3");
    }
    if (id == QLatin1String("grid")) {
        return QStringLiteral("grid-3x3");
    }
    if (id == QLatin1String("meeting")) {
        return QStringLiteral("users");
    }
    if (id == QLatin1String("todo")) {
        return QStringLiteral("list-todo");
    }
    return QStringLiteral("file");
}

PageTemplate make(const QString &id, const QDate &date)
{
    PageTemplate page;
    page.background = QStringLiteral("blank");
    if (id == QLatin1String("lined")) {
        page.background = QStringLiteral("ruled");
    } else if (id == QLatin1String("grid")) {
        page.background = QStringLiteral("grid");
    } else if (id == QLatin1String("meeting")) {
        const QString day = QLocale().toString(date, QLocale::LongFormat);
        page.title = tr("Meeting notes — %1").arg(day);
        page.blocks = richBlocks(QStringLiteral("<p><b>%1</b> %2</p><p><b>%3</b> </p>"
                                                "<h2>%4</h2><ul><li>%5</li></ul>"
                                                "<h2>%6</h2><p>%5</p>"
                                                "<h2>%7</h2><ul><li>%5</li></ul>"
                                                "<h2>%8</h2><p>%9%5</p><p>%9%5</p>")
                                     .arg(tr("Date:").toHtmlEscaped(), day.toHtmlEscaped(),
                                          tr("Attendees:").toHtmlEscaped(), tr("Agenda").toHtmlEscaped(),
                                          QStringLiteral("&#8203;"), tr("Notes").toHtmlEscaped(),
                                          tr("Decisions").toHtmlEscaped(), tr("Action items").toHtmlEscaped(),
                                          kTodo));
    } else if (id == QLatin1String("todo")) {
        page.title = tr("To-do list");
        page.blocks = richBlocks(QStringLiteral("<h2>%1</h2><p>%2&#8203;</p><p>%2&#8203;</p><p>%2&#8203;</p>")
                                     .arg(tr("To do").toHtmlEscaped(), kTodo));
    }
    return page;
}

} // namespace QindaOffice::Apps::Notes::Templates
