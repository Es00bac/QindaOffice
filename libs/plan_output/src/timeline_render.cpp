// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeline_render.h"

#include "plan_page_output.h"
#include "plan_time.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace QindaOffice::Plan {

namespace {

constexpr qreal kPad = 10;
constexpr qreal kMonthStrip = 15;
constexpr qreal kLaneHeight = 20;
constexpr qreal kCalloutHeight = 27;
constexpr qreal kGap = 10;
constexpr qreal kDiamond = 6;

struct Bar {
    int index;
    int lane;
};

// Document colours for the phase bars: the accent's hue turned around the
// wheel at the accent's saturation and value (D-004: one accent family).
QColor phaseColour(const PlanPaperStyle &s, int i)
{
    static const int turns[] = {0, 165, 285, 40, 120, 320};
    const QColor a = s.accent.toHsv();
    const int hue = (a.hsvHue() + turns[i % 6]) % 360;
    // Value capped so white bar text keeps its contrast on every hue.
    return QColor::fromHsv(hue, std::min(210, a.hsvSaturation()), std::min(165, a.value()));
}

} // namespace

void paintTimeline(QPainter *painter, const QRectF &rect, const Project &project, const TimelineOptions &options)
{
    const PlanPaperStyle s;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->setClipRect(rect);
    if (options.paper) {
        painter->fillRect(rect, s.paper);
    }
    const QLocale &l = options.locale;
    const Minutes start = project.info.start;
    const Minutes finish = project.finish != kNoDate && project.finish > start ? project.finish : start + 1440;
    const bool narrow = rect.width() < 700;
    const QFont nameFont = paperFont(narrow ? 10 : 11, QFont::DemiBold);
    const QFont dateFont = paperFont(narrow ? 9 : 10);
    const QFont endLabel = paperFont(9);
    const QFont endDate = paperFont(narrow ? 10 : 11, QFont::Bold);
    const QFont monthFont = paperFont(9);
    const QFont barFont = paperFont(10, QFont::DemiBold);

    const QString dateFormat = narrow ? QStringLiteral("d MMM") : QStringLiteral("ddd d MMM");
    const QString startText = l.toString(dateOf(start), dateFormat);
    const QString finishText = l.toString(dateOf(finish), dateFormat);
    const qreal labelWidth = std::max({textWidth(endDate, startText), textWidth(endDate, finishText),
                                       textWidth(endLabel, QStringLiteral("Finish"))}) + 12;
    const qreal bandLeft = rect.left() + kPad + labelWidth;
    const qreal bandRight = rect.right() - kPad - labelWidth;
    const qreal span = qreal(finish - start);
    const auto xOf = [&](Minutes m) { return bandLeft + (bandRight - bandLeft) * qreal(m - start) / span; };

    // Phase bars: top-level summaries (else top-level tasks) in lanes.
    QList<int> phases;
    for (int i = 0; i < project.tasks.size(); ++i) {
        const Task &t = project.tasks.at(i);
        if (t.parentIndex < 0 && t.summary && t.effectiveActive && t.start != kNoDate) {
            phases.append(i);
        }
    }
    if (phases.isEmpty()) {
        for (int i = 0; i < project.tasks.size(); ++i) {
            const Task &t = project.tasks.at(i);
            if (t.parentIndex < 0 && !t.isMilestone() && t.effectiveActive && t.start != kNoDate) {
                phases.append(i);
            }
        }
    }
    QList<Bar> bars;
    QList<Minutes> laneEnds;
    for (int i : std::as_const(phases)) {
        const Task &t = project.tasks.at(i);
        int lane = 0;
        while (lane < laneEnds.size() && laneEnds.at(lane) > t.start) {
            ++lane;
        }
        lane = std::min(lane, 2);
        if (lane == laneEnds.size()) {
            laneEnds.append(t.finish);
        } else {
            laneEnds[lane] = std::max(laneEnds.at(lane), t.finish);
        }
        bars.append({i, lane});
    }
    const int lanes = std::max(1, int(laneEnds.size()));
    const qreal bandHeight = kMonthStrip + lanes * kLaneHeight + 6;
    const qreal free = std::max<qreal>(0, rect.height() - 2 * kPad - bandHeight);
    const qreal above = std::ceil(free / 2);
    const qreal bandTop = rect.top() + kPad + above;
    const qreal bandBottom = bandTop + bandHeight;
    const QRectF band(bandLeft, bandTop, bandRight - bandLeft, bandHeight);

    painter->setPen(QPen(s.rule, 1));
    painter->setBrush(s.zebra);
    painter->drawRoundedRect(band, 6, 6);

    // Month ticks and labels along the top of the band.
    QDate month(dateOf(start).year(), dateOf(start).month(), 1);
    for (int guard = 0; guard < 600 && toMinutes(month, 0) < finish; ++guard) {
        const QDate next = month.addMonths(1);
        const qreal x0 = std::max(bandLeft, xOf(toMinutes(month, 0)));
        const qreal x1 = std::min(bandRight, xOf(toMinutes(next, 0)));
        if (toMinutes(month, 0) > start) {
            painter->setPen(QPen(s.rule, 1));
            painter->drawLine(QPointF(x0, bandTop + 3), QPointF(x0, bandBottom - 3));
        }
        for (const QString &text : {l.toString(month, QStringLiteral("MMMM yyyy")), l.toString(month, QStringLiteral("MMM")),
                                    l.toString(month, QStringLiteral("MMM")).left(1)}) {
            if (textWidth(monthFont, text) + 8 <= x1 - x0) {
                drawTextAt(painter, x0 + 5, bandTop + 3, text, monthFont, s.muted);
                break;
            }
        }
        month = next;
    }

    // Phase bars.
    for (int k = 0; k < bars.size(); ++k) {
        const Task &t = project.tasks.at(bars.at(k).index);
        const qreal x0 = xOf(t.start);
        const qreal x1 = std::max(x0 + 3, xOf(t.finish));
        const QRectF r(x0, bandTop + kMonthStrip + bars.at(k).lane * kLaneHeight + 2, x1 - x0, kLaneHeight - 4);
        const QColor c = phaseColour(s, k);
        painter->setPen(Qt::NoPen);
        painter->setBrush(s.tint(c, 0.85));
        painter->drawRoundedRect(r, 3, 3);
        const QString name = elideText(barFont, t.name, r.width() - 10);
        if (name.size() > 1 && r.width() > 24) {
            drawTextAt(painter, r.left() + 5, r.top() + (r.height() - QFontMetricsF(barFont).height()) / 2, name, barFont,
                       s.paper);
        }
    }

    // Start and finish at the ends.
    const qreal mid = bandTop + bandHeight / 2;
    const qreal endH = QFontMetricsF(endLabel).height() + QFontMetricsF(endDate).height();
    drawTextAt(painter, bandLeft - 8, mid - endH / 2, QStringLiteral("Start"), endLabel, s.muted, true);
    drawTextAt(painter, bandLeft - 8, mid - endH / 2 + QFontMetricsF(endLabel).height(), startText, endDate, s.ink, true);
    drawTextAt(painter, bandRight + 8, mid - endH / 2, QStringLiteral("Finish"), endLabel, s.muted);
    drawTextAt(painter, bandRight + 8, mid - endH / 2 + QFontMetricsF(endLabel).height(), finishText, endDate, s.ink);

    // Today.
    if (options.today != kNoDate && options.today >= start && options.today <= finish) {
        const qreal x = xOf(options.today);
        // A small "Today" tag in the band's month strip (the one place no
        // milestone callout or phase bar ever goes), the line below it.
        const QFont tagFont = paperFont(8, QFont::DemiBold);
        const qreal tw = textWidth(tagFont, QStringLiteral("Today")) + 8;
        const qreal th = std::min<qreal>(kMonthStrip - 2, QFontMetricsF(tagFont).height() + 2);
        const QRectF tag(std::max(bandLeft, std::min(x - tw / 2, bandRight - tw)), bandTop + 1, tw, th);
        painter->setPen(QPen(s.critical, 1.5));
        painter->drawLine(QPointF(x, tag.bottom()), QPointF(x, bandBottom));
        painter->setPen(QPen(s.critical, 1));
        painter->setBrush(s.paper);
        painter->drawRoundedRect(tag, th / 2, th / 2);
        drawTextAt(painter, tag.left() + 4, tag.top() + 1, QStringLiteral("Today"), tagFont, s.critical);
    }

    // Milestones with callouts.
    QList<int> milestones;
    for (int i = 0; i < project.tasks.size(); ++i) {
        const Task &t = project.tasks.at(i);
        if (!t.summary && t.effectiveActive && t.isMilestone() && t.start != kNoDate) {
            milestones.append(i);
        }
    }
    std::stable_sort(milestones.begin(), milestones.end(),
                     [&](int a, int b) { return project.tasks.at(a).start < project.tasks.at(b).start; });
    const int aboveLanes = std::max(1, int(above / kCalloutHeight));
    const int belowLanes = std::max(1, int((free - above) / kCalloutHeight));
    struct Lane {
        bool up;
        int depth;
        qreal right;
    };
    QList<Lane> calloutLanes;
    for (int d = 0; d < std::max(aboveLanes, belowLanes); ++d) {
        if (d < aboveLanes) {
            calloutLanes.append({true, d, -1e9});
        }
        if (d < belowLanes) {
            calloutLanes.append({false, d, -1e9});
        }
    }
    const qreal maxLabel = std::max<qreal>(60, rect.width() / 4);
    int placed = 0;
    for (int i : std::as_const(milestones)) {
        const Task &t = project.tasks.at(i);
        const qreal x = xOf(t.start);
        QString name = elideText(nameFont, t.name, maxLabel);
        const QString date = l.toString(dateOf(t.start), dateFormat);
        qreal w = std::max(textWidth(nameFont, name), textWidth(dateFont, date)) + 8;
        // Where the label wants to start: at its diamond, pulled inside the
        // strip at either end. Lanes are tested against THAT position, so a
        // label pulled left at the right edge never lands on its neighbour.
        const qreal desired = std::max(rect.left() + kPad, std::min(x, rect.right() - kPad - w));
        // Alternate the side we try first (above for the 1st, 3rd, …
        // milestone, below for the 2nd, …) so the strip stays balanced.
        const bool preferUp = placed++ % 2 == 0;
        int chosen = -1;
        for (int depth = 0; chosen < 0 && depth < std::max(aboveLanes, belowLanes); ++depth) {
            for (const bool up : {preferUp, !preferUp}) {
                for (int k = 0; chosen < 0 && k < calloutLanes.size(); ++k) {
                    const Lane &lane = calloutLanes.at(k);
                    if (lane.up == up && lane.depth == depth && lane.right + kGap <= desired) {
                        chosen = k;
                    }
                }
            }
        }
        qreal left = desired;
        if (chosen < 0) {
            chosen = 0;
            for (int k = 1; k < calloutLanes.size(); ++k) {
                if (calloutLanes.at(k).right < calloutLanes.at(chosen).right) {
                    chosen = k;
                }
            }
            left = calloutLanes.at(chosen).right + kGap;
            // AGENT-GUARD: no free lane — shorten the label to the room
            // left before the edge rather than overlap a neighbour; with
            // too little room only the diamond is drawn.
            const qreal room = rect.right() - kPad - left;
            if (room < w) {
                name = elideText(nameFont, t.name, room - 8);
                w = room;
            }
        }
        Lane &lane = calloutLanes[chosen];
        const bool labelled = w >= 36;
        lane.right = labelled ? left + w : lane.right;
        const qreal top = lane.up ? bandTop - 4 - (lane.depth + 1) * kCalloutHeight
                                  : bandBottom + 4 + lane.depth * kCalloutHeight + 2;
        const qreal edge = lane.up ? bandTop : bandBottom;
        const qreal anchorY = lane.up ? top + kCalloutHeight - 2 : top;
        QPainterPath leader(QPointF(x, edge));
        leader.lineTo(x, lane.up ? anchorY + 2 : anchorY - 2);
        if (std::abs(left - x) > 0.5) {
            leader.lineTo(left, lane.up ? anchorY + 2 : anchorY - 2);
        }
        leader.lineTo(left, lane.up ? top + 2 : top + kCalloutHeight - 4);
        if (labelled) {
            painter->setPen(QPen(s.muted, 0.8));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(leader);
            drawTextAt(painter, left + 5, top, name, nameFont, s.ink);
            drawTextAt(painter, left + 5, top + QFontMetricsF(nameFont).height() - 1, date, dateFont, s.muted);
        }
        painter->setPen(QPen(s.paper, 1.2));
        painter->setBrush(t.percentComplete >= 100 ? s.good : s.ink);
        painter->drawPolygon(QPolygonF({QPointF(x, edge - kDiamond), QPointF(x + kDiamond, edge), QPointF(x, edge + kDiamond),
                                        QPointF(x - kDiamond, edge)}));
    }
    painter->restore();
}

QImage renderTimeline(const Project &project, const QSize &size, const TimelineOptions &options, qreal devicePixelRatio)
{
    QImage image(size * devicePixelRatio, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(options.paper ? PlanPaperStyle().paper : QColor(Qt::transparent));
    QPainter painter(&image);
    paintTimeline(&painter, QRectF(QPointF(0, 0), QSizeF(size)), project, options);
    painter.end();
    return image;
}

bool exportTimelinePng(const Project &project, const QString &path, const QSize &size, const TimelineOptions &options,
                       QString *error)
{
    const QImage image = renderTimeline(project, size, options, 2.0);
    if (!image.save(path, "PNG")) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write %1").arg(path);
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Plan
