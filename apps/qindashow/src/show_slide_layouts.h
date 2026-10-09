// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaShow's slide commands on the deck's own layouts (M82, D-091), named
// as PowerPoint's Home ▸ Slides group names them: New Slide (the layout
// PowerPoint would pick, after the current slide) and its gallery of the
// deck's masters and layouts, Layout (the current slide moved onto another),
// Reset, Duplicate Slide and Reuse Slides…; and Browse for Themes with a
// template's masters and layouts (Apply to All or to the selected slide).
// Each is ONE undo step (ShowHost::applyDeckChange); the model work is
// show_model's pptx_slides.h. A deck without layouts (QindaShow's own until
// it is saved as .pptx or opened in the Slide Master view) keeps the six
// slide kinds of earlier versions in the same gallery.
//
// AGENT-CONTRACT: the gallery's commands are app-scoped ids by POSITION —
// showNewSlideLayout_<n> (a new slide on entry n) and showSlideLayout_<n>
// (the current slide onto entry n), n from 1 in layoutChoices' order — and
// are re-labelled whenever the deck's layouts change, as the Open Recent
// entries are (document_host.cpp); entries past the end are hidden and
// disabled. The fixed ones: showResetSlideAction, showReuseSlidesAction.
// Never renamed, never reused for another command.

#include "action_model.h"
#include "pptx_slides.h"

#include <QObject>
#include <QSizeF>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

class QPainter;

namespace QindaOffice::Ooxml {
class Package;
}

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Show {

class ShowHost;

namespace SlideActionId {
inline const char kResetSlide[] = "showResetSlideAction";
inline const char kReuseSlides[] = "showReuseSlidesAction";
} // namespace SlideActionId

class ShowSlideLayouts final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the QindaShow host.")
    // The gallery: [{newId, applyId, name, master, current}] in order.
    Q_PROPERTY(QVariantList gallery READ gallery NOTIFY galleryChanged)
    // AGENT-GUARD: these two change ONLY when the number of entries does —
    // the ribbon re-resolves its tabs on every change of them, and a tab
    // list changing under Tk.Ribbon can send it back to its first tab.
    Q_PROPERTY(QStringList newSlideIds READ newSlideIds NOTIFY idsChanged)
    Q_PROPERTY(QStringList layoutIds READ layoutIds NOTIFY idsChanged)
    // Reuse Slides: the chosen deck's slides [{title, hidden}] and its file.
    Q_PROPERTY(QVariantList reuseSlides READ reuseSlides NOTIFY reuseChanged)
    Q_PROPERTY(QString reuseFile READ reuseFile NOTIFY reuseChanged)
public:
    explicit ShowSlideLayouts(ShowHost *host);
    ~ShowSlideLayouts() override;

    [[nodiscard]] QVariantList gallery() const;
    [[nodiscard]] QStringList newSlideIds() const;
    [[nodiscard]] QStringList layoutIds() const;
    [[nodiscard]] QVariantList reuseSlides() const;
    [[nodiscard]] QString reuseFile() const { return m_reuseFile; }
    // New Slide ▸ and Layout ▸ (the Slide menu), Reset and Reuse Slides….
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;
    // Whether the deck has layouts of its own to offer.
    [[nodiscard]] bool deckHasLayouts() const;

    // New Slide: after the current slide, on the layout PowerPoint picks.
    void newSlide();
    Q_INVOKABLE void newSlideOn(int position);
    Q_INVOKABLE void applyLayout(int position);
    void resetSlide();
    // Duplicate Slide for a deck with PowerPoint structure (its own part);
    // false when the host's own copy applies.
    bool duplicateSlide();
    // Reuse Slides…: the file's slides, then the chosen ones inserted after
    // the current slide.
    Q_INVOKABLE bool openReuseFile(const QUrl &url);
    Q_INVOKABLE void reuse(const QVariantList &positions, bool keepSourceFormatting);
    [[nodiscard]] const ShowModel::Presentation *reuseDeck() const;
    // Browse for Themes with a .potx / .pptx: its masters and layouts, for
    // every slide or the current one.
    Q_INVOKABLE bool applyTemplate(const QUrl &url, bool allSlides);
    // A thumbnail of gallery entry `position` in `size`.
    void renderEntry(int position, QPainter *painter, const QSizeF &size) const;
    // What each empty placeholder of `slide` types in: its layout
    // placeholder's look, else its master's text style (the canvas's
    // prompts and in-place editor; SlideCanvasItem::setPlaceholderLooks).
    [[nodiscard]] static QHash<QString, ShowModel::TextRun> emptyLooks(const ShowModel::Presentation &deck,
                                                                       const ShowModel::Slide &slide);

signals:
    void galleryChanged();
    // idsAboutToChange comes first: the window keeps the ribbon's tab across
    // the change (ShowWindow.qml says why).
    void idsAboutToChange();
    void idsChanged();
    void reuseChanged();
    void reuseFileRequested();
    void reuseDialogRequested();

private:
    void createActions();
    // The gallery again when the deck's layouts changed: labels, the menus.
    void rebuild();
    void insertAt(const QString &layoutId, int index);
    [[nodiscard]] QString entryLayout(int position) const;

    ShowHost *m_host;
    QList<ShowModel::LayoutChoice> m_choices;
    bool m_builtIn = true; // the six slide kinds stand in for layouts
    int m_entries = 0;     // positional entries created so far
    std::shared_ptr<const Ooxml::Package> m_reusePackage;
    std::unique_ptr<ShowModel::Presentation> m_reuseDeck;
    QString m_reuseFile;
};

} // namespace QindaOffice::Apps::Show
