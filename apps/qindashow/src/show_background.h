// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Format Background (M75, D-084), as PowerPoint's pane works: the fill of
// the current slide's background — solid, gradient (linear or radial, its
// stops and angle), picture or texture (tiled), pattern — with its
// transparency, Hide Background Graphics, Apply to All (the masters take
// it, the layouts and slides let go of their own) and Reset Background
// (the layout's or master's again). In the Slide Master view it edits the
// master's or layout's background. The pane (FormatBackgroundPane.qml)
// reads the properties and calls the setters; each change is ONE undo step
// (ShowHost::applyDeckChange).
//
// The textures are QindaShow's own tiles, drawn here (deterministically),
// written into the file as tiled pictures (a:blipFill/a:tile).

#include "presentation.h"

#include <QColor>
#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <QVariantList>

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Apps::Show {

class ShowHost;

class ShowBackground final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the QindaShow host.")
    // "solid", "gradient", "picture", "pattern".
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QColor color READ color NOTIFY changed)
    Q_PROPERTY(int transparency READ transparency NOTIFY changed) // percent
    // [{id, position (0..1), color}] for QindaTK's GradientEditor.
    Q_PROPERTY(QVariantList stops READ stops NOTIFY changed)
    Q_PROPERTY(qreal angle READ angle NOTIFY changed)
    Q_PROPERTY(bool radial READ radial NOTIFY changed)
    Q_PROPERTY(bool tile READ tile NOTIFY changed)
    Q_PROPERTY(bool hasPicture READ hasPicture NOTIFY changed)
    Q_PROPERTY(QString pattern READ pattern NOTIFY changed)
    Q_PROPERTY(QColor patternForeground READ patternForeground NOTIFY changed)
    Q_PROPERTY(QColor patternBackground READ patternBackground NOTIFY changed)
    // True when the slide (or layout) has a background of its own.
    Q_PROPERTY(bool own READ own NOTIFY changed)
    Q_PROPERTY(bool hideGraphics READ hideGraphics NOTIFY changed)
    // What the pane is editing: "slide", "master", "layout".
    Q_PROPERTY(QString target READ target NOTIFY changed)
    Q_PROPERTY(QStringList textures READ textures CONSTANT)
    Q_PROPERTY(QStringList patterns READ patterns CONSTANT)
public:
    explicit ShowBackground(ShowHost *host);

    [[nodiscard]] QString kind() const;
    [[nodiscard]] QColor color() const;
    [[nodiscard]] int transparency() const;
    [[nodiscard]] QVariantList stops() const;
    [[nodiscard]] qreal angle() const { return current().style.angle; }
    [[nodiscard]] bool radial() const { return !current().style.path.isEmpty(); }
    [[nodiscard]] bool tile() const { return current().style.tile; }
    [[nodiscard]] bool hasPicture() const { return !current().style.image.isEmpty(); }
    [[nodiscard]] QString pattern() const { return current().style.pattern; }
    [[nodiscard]] QColor patternForeground() const { return current().style.foreground; }
    [[nodiscard]] QColor patternBackground() const { return current().style.background; }
    [[nodiscard]] bool own() const;
    [[nodiscard]] bool hideGraphics() const;
    [[nodiscard]] QString target() const;
    [[nodiscard]] QStringList textures() const;
    [[nodiscard]] QStringList patterns() const;

    // The background as the pane shows it (the effective one).
    [[nodiscard]] ShowModel::Background current() const;

    Q_INVOKABLE void setKind(const QString &kind);
    Q_INVOKABLE void setColor(const QColor &color);
    Q_INVOKABLE void setTransparency(int percent);
    Q_INVOKABLE void setGradient(const QVariantList &stops, qreal angle, bool radial);
    Q_INVOKABLE void setPictureFile(const QUrl &url);
    Q_INVOKABLE void setTexture(int index);
    Q_INVOKABLE void setTile(bool tile);
    Q_INVOKABLE void setPattern(const QString &preset, const QColor &foreground, const QColor &background);
    Q_INVOKABLE void setHideGraphics(bool hide);
    Q_INVOKABLE void applyToAll();
    Q_INVOKABLE void reset();

    // A texture tile (PNG), drawn the same every time.
    [[nodiscard]] static QByteArray textureImage(int index);

signals:
    void changed();
    void pictureRequested();

private:
    void apply(const ShowModel::Background &background, const QString &label);

    ShowHost *m_host;
};

} // namespace QindaOffice::Apps::Show
