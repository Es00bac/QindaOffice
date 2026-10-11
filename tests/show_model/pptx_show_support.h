// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// M91 (D-099) PowerPoint interchange gates over hand-built PowerPoint-
// shaped packages (pptx_fixture_m91.h): every Action Settings choice on
// click and on mouse-over, linked text, custom shows and Set Up Slide
// Show, Advance Slide timings, a video's Trim / Fade Duration / bookmarks,
// and Effect Options directions with exit effects — read as PowerPoint
// writes them; written unchanged (presProps.xml byte for byte, the action
// markup as read); written changed and new, schema-valid with
// PowerPoint's id rules, and read back equal. Also the order a show plays
// (showPlaylist), what a click hits (slide_hit.h) and directions drawn.

#include "pptx_fixture_m91.h"
#include "pptx_origin.h"
#include "pptx_reader.h"
#include "pptx_rules.h"
#include "pptx_writer.h"
#include "slide_hit.h"
#include "slide_renderer.h"

#include "support/ooxml_schema.h"

#include <QGuiApplication>
#include <QPainter>
#include <QtTest>

using namespace QindaOffice::Show;
namespace Ooxml = QindaOffice::Ooxml;

namespace PptxShowSupport {

inline Presentation fixture()
{
    QString error;
    Presentation deck = readPptxData(PptxFixture::M91::deck(), &error);
    if (!error.isEmpty()) {
        qWarning() << error;
    }
    return deck;
}

inline const Shape *byOfficeId(const Slide &slide, quint32 id)
{
    for (const Shape &shape : slide.shapes) {
        if (shape.nvId == id) {
            return &shape;
        }
    }
    return nullptr;
}

inline Shape *byOfficeId(Slide &slide, quint32 id)
{
    return const_cast<Shape *>(byOfficeId(std::as_const(slide), id));
}

inline QByteArray part(const QByteArray &zip, const QString &name)
{
    const auto package = Ooxml::Package::fromData(zip);
    return package ? package->data(name) : QByteArray();
}

inline QStringList problems(const QByteArray &zip)
{
    QStringList out = PptxFixture::ruleViolations(zip);
    if (QindaOffice::Test::schemaValidationAvailable()) {
        out += QindaOffice::Test::validatePackage(*Ooxml::Package::fromData(zip));
    }
    return out;
}

inline Shape box(const QString &id, const QRectF &rect)
{
    Shape shape;
    shape.id = id;
    shape.kind = ShapeKind::Rectangle;
    shape.rect = rect;
    shape.fill = QColor(0x2E, 0x75, 0xB6);
    return shape;
}

// A deck made in QindaShow with every action kind, a custom show, show
// settings and timings.
inline Presentation madeHere()
{
    Presentation deck;
    deck.title = QStringLiteral("Actions made here");
    for (const char *title : {"One", "Two", "Three"}) {
        Q_UNUSED(deck.addSlide(QLatin1String(title)));
    }
    deck.slides[2].hidden = true;
    const QString twoId = deck.slides.at(1).id;
    const QString threeId = deck.slides.at(2).id;
    Slide &first = deck.slides[0];
    first.advanceAfterMs = 4200;
    first.advanceOnClick = false;
    first.transition = QStringLiteral("fade");
    deck.slides[1].advanceAfterMs = 0;
    const QList<std::pair<QString, ShowAction>> actions = [&] {
        QList<std::pair<QString, ShowAction>> list;
        const auto add = [&list](const QString &id, const QString &link, const QString &address = QString()) {
            ShowAction action;
            action.link = link;
            action.address = address;
            list.append({id, action});
        };
        add(QStringLiteral("next"), jumpLink(ActionKind::NextSlide));
        add(QStringLiteral("previous"), jumpLink(ActionKind::PreviousSlide));
        add(QStringLiteral("firstSlide"), jumpLink(ActionKind::FirstSlide));
        add(QStringLiteral("lastSlide"), jumpLink(ActionKind::LastSlide));
        add(QStringLiteral("viewed"), jumpLink(ActionKind::LastViewed));
        add(QStringLiteral("end"), jumpLink(ActionKind::EndShow));
        add(QStringLiteral("slide"), slideLink(twoId));
        add(QStringLiteral("show"), customShowLink(3, true));
        add(QStringLiteral("web"), QStringLiteral("https://example.org/made"));
        add(QStringLiteral("mail"), QStringLiteral("mailto:show@example.org"));
        add(QStringLiteral("file"), QStringLiteral("ppaction://hlinkfile"), QStringLiteral("handout.pdf"));
        add(QStringLiteral("program"), QStringLiteral("ppaction://program"), QStringLiteral("/usr/bin/true"));
        return list;
    }();
    qreal x = 10;
    for (const auto &[id, action] : actions) {
        Shape shape = box(id, QRectF(x, 300, 40, 40));
        shape.click = action;
        first.shapes.append(shape);
        x += 50;
    }
    Shape sound = box(QStringLiteral("sound"), QRectF(10, 200, 40, 40));
    sound.kind = ShapeKind::AutoShape;
    sound.geometry = QStringLiteral("actionButtonSound");
    sound.click.sound = builtInSound(QStringLiteral("Chime"));
    sound.click.soundName = QStringLiteral("Chime");
    sound.click.highlight = true;
    sound.click.tooltip = QStringLiteral("A chime");
    sound.hover.link = slideLink(threeId);
    sound.hover.stopSound = true;
    first.shapes.append(sound);
    Shape text;
    text.id = QStringLiteral("text");
    text.kind = ShapeKind::TextBox;
    text.rect = QRectF(10, 20, 400, 60);
    TextParagraph paragraph;
    TextRun linked;
    linked.text = QStringLiteral("Go to two");
    linked.hyperlink = slideLink(twoId);
    TextRun web;
    web.text = QStringLiteral(" or the web");
    web.hyperlink = QStringLiteral("https://example.org/");
    paragraph.runs = {linked, web};
    text.paragraphs = {paragraph};
    first.shapes.append(text);
    deck.customShows = {{3, QStringLiteral("Middle"), {twoId, threeId, twoId}}};
    deck.showSettings.type = ShowType::Window;
    deck.showSettings.showScrollbar = false;
    deck.showSettings.range = ShowRange::Slides;
    deck.showSettings.from = 2;
    deck.showSettings.to = 3;
    deck.showSettings.loop = true;
    deck.showSettings.animation = false;
    deck.showSettings.penColor = QColor(0x00, 0x70, 0xC0);
    deck.showSettings.laserColor = QColor(0xFF, 0xC0, 0x00);
    return deck;
}

} // namespace PptxShowSupport
