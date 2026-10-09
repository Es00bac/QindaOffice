// SPDX-License-Identifier: GPL-3.0-or-later
// Sections kept true to the deck (sections.h).

#include "sections.h"

#include <QUuid>

namespace QindaOffice::Show {

namespace {

QString newSectionId()
{
    return QUuid::createUuid().toString(QUuid::WithBraces).toUpper();
}

} // namespace

int sectionOf(const Presentation &deck, const QString &slideId)
{
    for (qsizetype i = 0; i < deck.sections.size(); ++i) {
        if (deck.sections.at(i).slides.contains(slideId)) {
            return int(i);
        }
    }
    return -1;
}

void normalizeSections(Presentation &deck)
{
    if (deck.sections.isEmpty()) {
        return;
    }
    QHash<QString, int> section;
    for (qsizetype i = 0; i < deck.sections.size(); ++i) {
        for (const QString &slide : deck.sections.at(i).slides) {
            if (!section.contains(slide)) {
                section.insert(slide, int(i));
            }
        }
    }
    // Each slide in deck order: its section, else the one before it; a
    // section never comes back once a later one started.
    QList<int> index;
    int current = 0;
    for (const Slide &slide : std::as_const(deck.slides)) {
        current = qMax(current, section.value(slide.id, current));
        index.append(current);
    }
    for (SlideSection &s : deck.sections) {
        s.slides.clear();
    }
    for (qsizetype i = 0; i < deck.slides.size(); ++i) {
        deck.sections[qBound(0, index.at(i), int(deck.sections.size()) - 1)].slides.append(deck.slides.at(i).id);
    }
}

void moveSlideInSections(Presentation &deck, const QString &slideId)
{
    for (SlideSection &s : deck.sections) {
        s.slides.removeAll(slideId);
    }
}

int addSection(Presentation &deck, const QString &slideId, const QString &name)
{
    qsizetype at = -1;
    for (qsizetype i = 0; i < deck.slides.size(); ++i) {
        if (deck.slides.at(i).id == slideId) {
            at = i;
        }
    }
    if (deck.sections.isEmpty()) {
        // The slides before it make PowerPoint's "Default Section".
        SlideSection first{newSectionId(), QStringLiteral("Default Section"), {}};
        for (qsizetype i = 0; i < deck.slides.size(); ++i) {
            first.slides.append(deck.slides.at(i).id);
        }
        deck.sections.append(first);
        if (at <= 0) {
            deck.sections.first().name = name;
            return 0;
        }
    }
    SlideSection added{newSectionId(), name, {}};
    if (at < 0) {
        deck.sections.append(added); // an empty section at the end
        return int(deck.sections.size()) - 1;
    }
    const int owner = sectionOf(deck, slideId);
    SlideSection &split = deck.sections[qMax(0, owner)];
    // From the slide on (a slide that starts its section already leaves
    // that section empty, as PowerPoint does).
    const qsizetype from = qMax<qsizetype>(0, split.slides.indexOf(slideId));
    added.slides = split.slides.mid(from);
    split.slides = split.slides.mid(0, from);
    deck.sections.insert(qMax(0, owner) + 1, added);
    return qMax(0, owner) + 1;
}

bool renameSection(Presentation &deck, int section, const QString &name)
{
    if (section < 0 || section >= deck.sections.size() || name.trimmed().isEmpty()
        || deck.sections.at(section).name == name.trimmed()) {
        return false;
    }
    deck.sections[section].name = name.trimmed();
    return true;
}

bool removeSection(Presentation &deck, int section)
{
    if (section < 0 || section >= deck.sections.size()) {
        return false;
    }
    if (deck.sections.size() == 1) {
        deck.sections.clear();
        return true;
    }
    const QStringList slides = deck.sections.at(section).slides;
    if (section == 0) {
        deck.sections[1].slides = slides + deck.sections.at(1).slides;
    } else {
        deck.sections[section - 1].slides += slides;
    }
    deck.sections.removeAt(section);
    return true;
}

void removeAllSections(Presentation &deck)
{
    deck.sections.clear();
}

bool moveSection(Presentation &deck, int section, int delta)
{
    const int target = section + delta;
    if (section < 0 || section >= deck.sections.size() || target < 0 || target >= deck.sections.size() || delta == 0) {
        return false;
    }
    deck.sections.move(section, target);
    // The slides follow their sections.
    QHash<QString, Slide> byId;
    for (const Slide &slide : std::as_const(deck.slides)) {
        byId.insert(slide.id, slide);
    }
    QList<Slide> ordered;
    for (const SlideSection &s : std::as_const(deck.sections)) {
        for (const QString &id : s.slides) {
            if (byId.contains(id)) {
                ordered.append(byId.take(id));
            }
        }
    }
    for (const Slide &slide : std::as_const(deck.slides)) {
        if (byId.contains(slide.id)) {
            ordered.append(byId.take(slide.id)); // not in any section (should not happen)
        }
    }
    deck.slides = ordered;
    return true;
}

} // namespace QindaOffice::Show
