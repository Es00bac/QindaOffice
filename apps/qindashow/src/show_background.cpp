// SPDX-License-Identifier: GPL-3.0-or-later
// Format Background (show_background.h).

#include "show_background.h"

#include "pptx_design.h"
#include "show_document.h"
#include "show_host.h"
#include "show_master_view.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QRandomGenerator>
#include <QUrl>

namespace QindaOffice::Apps::Show {

namespace {

struct Texture {
    const char *name;
    QColor base;
    QColor ink;
    int kind; // 0 speckle, 1 lines, 2 weave, 3 grid
};

const Texture kTextures[] = {
    {QT_TR_NOOP("Paper"), QColor(0xF6, 0xF1, 0xE6), QColor(0xD9, 0xCF, 0xBA), 0},
    {QT_TR_NOOP("Linen"), QColor(0xEE, 0xE8, 0xDA), QColor(0xD2, 0xC8, 0xB2), 1},
    {QT_TR_NOOP("Canvas"), QColor(0xD8, 0xCC, 0xB0), QColor(0xB8, 0xA8, 0x88), 2},
    {QT_TR_NOOP("Slate"), QColor(0x4A, 0x50, 0x58), QColor(0x5E, 0x66, 0x70), 0},
    {QT_TR_NOOP("Sand"), QColor(0xE9, 0xD8, 0xA6), QColor(0xCF, 0xBA, 0x84), 0},
    {QT_TR_NOOP("Blueprint"), QColor(0x1E, 0x4F, 0x8A), QColor(0x5D, 0x8C, 0xC4), 3},
};

const char *const kPatterns[] = {"pct10", "pct25", "pct50", "pct75", "ltHorz", "ltVert", "dkHorz", "dkVert",
                                 "ltDnDiag", "ltUpDiag", "smGrid", "lgGrid", "smCheck", "diagCross"};

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(qBound(0.0, alpha, 1.0));
    return color;
}

} // namespace

ShowBackground::ShowBackground(ShowHost *host)
    : QObject(host)
    , m_host(host)
{
    connect(host, &ShowHost::deckChanged, this, &ShowBackground::changed);
    connect(host, &ShowHost::currentSlideChanged, this, &ShowBackground::changed);
}

QStringList ShowBackground::textures() const
{
    QStringList names;
    for (const Texture &texture : kTextures) {
        names.append(tr(texture.name));
    }
    return names;
}

QStringList ShowBackground::patterns() const
{
    QStringList names;
    for (const char *pattern : kPatterns) {
        names.append(QString::fromLatin1(pattern));
    }
    return names;
}

QByteArray ShowBackground::textureImage(int index)
{
    const Texture &texture = kTextures[qBound(0, index, int(std::size(kTextures)) - 1)];
    QImage image(128, 128, QImage::Format_RGB32);
    image.fill(texture.base);
    QPainter painter(&image);
    QRandomGenerator random(quint32(index + 1) * 7919u); // the same tile every time
    painter.setPen(QPen(texture.ink, 1));
    switch (texture.kind) {
    case 0:
        for (int i = 0; i < 900; ++i) {
            painter.drawPoint(int(random.bounded(128)), int(random.bounded(128)));
        }
        break;
    case 1:
        for (int i = 0; i < 128; i += 3) {
            painter.drawLine(0, i, 127, i);
            if (i % 6 == 0) {
                painter.drawLine(i, 0, i, 127);
            }
        }
        break;
    case 2:
        for (int y = 0; y < 128; y += 8) {
            for (int x = 0; x < 128; x += 8) {
                painter.fillRect(((x + y) / 8) % 2 == 0 ? QRect(x, y + 2, 8, 4) : QRect(x + 2, y, 4, 8), texture.ink);
            }
        }
        break;
    default:
        for (int i = 0; i < 128; i += 16) {
            painter.drawLine(0, i, 127, i);
            painter.drawLine(i, 0, i, 127);
        }
        break;
    }
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}

ShowModel::Background ShowBackground::current() const
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const ShowMasterView *view = m_host->masterView();
    const ShowModel::SlideMaster *master = nullptr;
    if (view->active()) {
        if (const ShowModel::SlideLayout *layout = deck->findLayout(view->currentLayoutId());
            layout != nullptr && layout->background.set) {
            return layout->background;
        }
        master = deck->findMaster(view->currentMasterId());
    } else if (const ShowModel::Slide *slide = m_host->currentSlide()) {
        if (slide->background.set) {
            return slide->background;
        }
        if (const ShowModel::SlideLayout *layout = deck->layoutFor(*slide); layout != nullptr && layout->background.set) {
            return layout->background;
        }
        master = deck->masterFor(*slide);
    }
    if (master != nullptr) {
        return ShowModel::Background{true, master->background, master->backgroundStyle};
    }
    return ShowModel::Background{true, QColor(Qt::white), {}};
}

QString ShowBackground::kind() const
{
    switch (current().style.kind) {
    case ShowModel::FillKind::Gradient:
        return QStringLiteral("gradient");
    case ShowModel::FillKind::Picture:
        return QStringLiteral("picture");
    case ShowModel::FillKind::Pattern:
        return QStringLiteral("pattern");
    case ShowModel::FillKind::Plain:
        break;
    }
    return QStringLiteral("solid");
}

QColor ShowBackground::color() const
{
    QColor c = current().color;
    c.setAlpha(255);
    return c;
}

int ShowBackground::transparency() const
{
    const ShowModel::Background b = current();
    qreal alpha = b.color.alphaF();
    if (b.style.kind == ShowModel::FillKind::Picture) {
        alpha = b.style.imageAlpha;
    } else if (b.style.kind == ShowModel::FillKind::Gradient && !b.style.stops.isEmpty()) {
        alpha = b.style.stops.first().color.alphaF();
    } else if (b.style.kind == ShowModel::FillKind::Pattern) {
        alpha = b.style.foreground.alphaF();
    }
    return qRound((1.0 - alpha) * 100.0);
}

QVariantList ShowBackground::stops() const
{
    QVariantList list;
    const QList<ShowModel::GradientStop> stops = current().style.stops;
    for (qsizetype i = 0; i < stops.size(); ++i) {
        QColor c = stops.at(i).color;
        c.setAlpha(255);
        list.append(QVariantMap{{QStringLiteral("id"), int(i)}, {QStringLiteral("position"), stops.at(i).position},
                                {QStringLiteral("color"), c}});
    }
    return list;
}

bool ShowBackground::own() const
{
    const ShowMasterView *view = m_host->masterView();
    if (view->active()) {
        const ShowModel::SlideLayout *layout = m_host->showDocument()->presentation()->findLayout(view->currentLayoutId());
        return layout == nullptr || layout->background.set;
    }
    const ShowModel::Slide *slide = m_host->currentSlide();
    return slide != nullptr && slide->background.set;
}

bool ShowBackground::hideGraphics() const
{
    const ShowMasterView *view = m_host->masterView();
    if (view->active()) {
        const ShowModel::SlideLayout *layout = m_host->showDocument()->presentation()->findLayout(view->currentLayoutId());
        return layout != nullptr && !layout->showMasterShapes;
    }
    const ShowModel::Slide *slide = m_host->currentSlide();
    return slide != nullptr && !slide->showMasterShapes;
}

QString ShowBackground::target() const
{
    const ShowMasterView *view = m_host->masterView();
    if (!view->active()) {
        return QStringLiteral("slide");
    }
    return view->currentLayoutId().isEmpty() ? QStringLiteral("master") : QStringLiteral("layout");
}

void ShowBackground::apply(const ShowModel::Background &background, const QString &label)
{
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    const ShowMasterView *view = m_host->masterView();
    if (view->active()) {
        for (ShowModel::SlideLayout &layout : after.layouts) {
            if (layout.id == view->currentLayoutId()) {
                layout.background = background;
            }
        }
        if (view->currentLayoutId().isEmpty()) {
            for (ShowModel::SlideMaster &master : after.masters) {
                if (master.id == view->currentMasterId() && background.set) {
                    master.background = background.color;
                    master.backgroundStyle = background.style;
                }
            }
        }
    } else if (const ShowModel::Slide *slide = m_host->currentSlide()) {
        after = ShowModel::withBackground(after, slide->id, background, false);
    }
    m_host->applyDeckChange(after, label);
}

void ShowBackground::setKind(const QString &kind)
{
    ShowModel::Background b = current();
    b.set = true;
    QColor base = b.color.isValid() ? b.color : QColor(Qt::white);
    base.setAlpha(255);
    if (kind == u"solid") {
        b.style = ShowModel::FillStyle();
        b.color = base;
    } else if (kind == u"gradient") {
        b.style = ShowModel::FillStyle();
        b.style.kind = ShowModel::FillKind::Gradient;
        b.style.stops = {{0.0, base.lighter(115)}, {1.0, base.darker(125)}};
        b.style.angle = 90;
        b.color = b.style.stops.first().color;
    } else if (kind == u"picture") {
        if (b.style.kind != ShowModel::FillKind::Picture) {
            setTexture(0); // something to see until a picture is chosen
            emit pictureRequested();
            return;
        }
    } else if (kind == u"pattern") {
        b.style = ShowModel::FillStyle();
        b.style.kind = ShowModel::FillKind::Pattern;
        b.style.pattern = QStringLiteral("pct50");
        b.style.foreground = QColor(0x40, 0x40, 0x40);
        b.style.background = base;
        b.color = b.style.foreground;
    }
    apply(b, tr("Background fill"));
}

void ShowBackground::setColor(const QColor &color)
{
    if (!color.isValid()) {
        return;
    }
    ShowModel::Background b = current();
    const qreal alpha = 1.0 - transparency() / 100.0;
    b.set = true;
    b.style = ShowModel::FillStyle();
    b.color = withAlpha(color, alpha);
    apply(b, tr("Background colour"));
}

void ShowBackground::setTransparency(int percent)
{
    ShowModel::Background b = current();
    const qreal alpha = 1.0 - qBound(0, percent, 100) / 100.0;
    b.set = true;
    switch (b.style.kind) {
    case ShowModel::FillKind::Plain:
        b.color = withAlpha(b.color, alpha);
        break;
    case ShowModel::FillKind::Gradient:
        for (ShowModel::GradientStop &stop : b.style.stops) {
            stop.color = withAlpha(stop.color, alpha);
        }
        break;
    case ShowModel::FillKind::Picture:
        b.style.imageAlpha = alpha;
        break;
    case ShowModel::FillKind::Pattern:
        b.style.foreground = withAlpha(b.style.foreground, alpha);
        b.style.background = withAlpha(b.style.background, alpha);
        break;
    }
    apply(b, tr("Background transparency"));
}

void ShowBackground::setGradient(const QVariantList &stops, qreal angle, bool radial)
{
    ShowModel::Background b = current();
    const qreal alpha = 1.0 - transparency() / 100.0;
    b.set = true;
    b.style = ShowModel::FillStyle();
    b.style.kind = ShowModel::FillKind::Gradient;
    for (const QVariant &value : stops) {
        const QVariantMap stop = value.toMap();
        b.style.stops.append({qBound(0.0, stop.value(QStringLiteral("position")).toDouble(), 1.0),
                              withAlpha(stop.value(QStringLiteral("color")).value<QColor>(), alpha)});
    }
    std::sort(b.style.stops.begin(), b.style.stops.end(),
              [](const ShowModel::GradientStop &x, const ShowModel::GradientStop &y) { return x.position < y.position; });
    if (b.style.stops.size() < 2) {
        return;
    }
    b.style.angle = angle;
    b.style.path = radial ? QStringLiteral("circle") : QString();
    b.color = b.style.stops.first().color;
    apply(b, tr("Background gradient"));
}

void ShowBackground::setPictureFile(const QUrl &url)
{
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!file.open(QIODevice::ReadOnly)) {
        m_host->showStatusMessage(tr("Cannot read %1").arg(file.fileName()), 8000);
        return;
    }
    const QByteArray bytes = file.readAll();
    QBuffer buffer;
    buffer.setData(bytes);
    QImageReader reader(&buffer);
    if (!reader.canRead()) {
        m_host->showStatusMessage(tr("%1 is not a picture").arg(file.fileName()), 8000);
        return;
    }
    ShowModel::Background b;
    b.set = true;
    b.color = QColor(200, 200, 200);
    b.style.kind = ShowModel::FillKind::Picture;
    b.style.image = bytes;
    const QByteArray format = reader.format();
    b.style.imageMime = QStringLiteral("image/") + QString::fromLatin1(format == "jpg" ? QByteArray("jpeg") : format);
    b.style.imageAlpha = 1.0 - transparency() / 100.0;
    apply(b, tr("Background picture"));
}

void ShowBackground::setTexture(int index)
{
    ShowModel::Background b;
    b.set = true;
    b.color = kTextures[qBound(0, index, int(std::size(kTextures)) - 1)].base;
    b.style.kind = ShowModel::FillKind::Picture;
    b.style.image = textureImage(index);
    b.style.imageMime = QStringLiteral("image/png");
    b.style.tile = true;
    apply(b, tr("Background texture"));
}

void ShowBackground::setTile(bool tile)
{
    ShowModel::Background b = current();
    if (b.style.kind != ShowModel::FillKind::Picture || b.style.tile == tile) {
        return;
    }
    b.set = true;
    b.style.tile = tile;
    apply(b, tr("Background picture"));
}

void ShowBackground::setPattern(const QString &preset, const QColor &foreground, const QColor &background)
{
    ShowModel::Background b;
    b.set = true;
    b.style.kind = ShowModel::FillKind::Pattern;
    b.style.pattern = preset.isEmpty() ? QStringLiteral("pct50") : preset;
    b.style.foreground = foreground.isValid() ? foreground : QColor(0x40, 0x40, 0x40);
    b.style.background = background.isValid() ? background : QColor(Qt::white);
    b.color = b.style.foreground;
    apply(b, tr("Background pattern"));
}

void ShowBackground::setHideGraphics(bool hide)
{
    ShowModel::Presentation after = *m_host->showDocument()->presentation();
    const ShowMasterView *view = m_host->masterView();
    if (view->active()) {
        for (ShowModel::SlideLayout &layout : after.layouts) {
            if (layout.id == view->currentLayoutId()) {
                layout.showMasterShapes = !hide;
            }
        }
    } else if (const ShowModel::Slide *slide = m_host->currentSlide()) {
        after.findSlide(slide->id)->showMasterShapes = !hide;
    }
    m_host->applyDeckChange(after, tr("Hide background graphics"));
}

void ShowBackground::applyToAll()
{
    const ShowModel::Presentation *deck = m_host->showDocument()->presentation();
    const ShowModel::Slide *slide = m_host->currentSlide();
    const QString slideId = slide != nullptr ? slide->id : QString();
    m_host->applyDeckChange(ShowModel::withBackground(*deck, slideId, current(), true), tr("Apply background to all"));
}

void ShowBackground::reset()
{
    if (m_host->masterView()->active() && m_host->masterView()->currentLayoutId().isEmpty()) {
        return; // a master always has a background
    }
    apply(ShowModel::Background(), tr("Reset background"));
}

} // namespace QindaOffice::Apps::Show
