// SPDX-License-Identifier: GPL-3.0-or-later
// The order a show plays and the custom-show helpers (show_settings.h).

#include "show_settings.h"

#include "presentation.h"

namespace QindaOffice::Show {

int customShowIndex(const Presentation &deck, quint32 number)
{
    for (qsizetype i = 0; i < deck.customShows.size(); ++i) {
        if (deck.customShows.at(i).number == number) {
            return int(i);
        }
    }
    return -1;
}

quint32 nextCustomShowNumber(const Presentation &deck)
{
    quint32 next = 0;
    for (const CustomShow &show : deck.customShows) {
        next = qMax(next, show.number + 1);
    }
    return next;
}

QList<int> showPlaylist(const Presentation &deck, int custom)
{
    const ShowSettings &settings = deck.showSettings;
    if (custom < 0 && settings.range == ShowRange::Custom) {
        custom = customShowIndex(deck, settings.customShow);
    }
    QList<int> rows;
    if (custom >= 0 && custom < deck.customShows.size()) {
        for (const QString &id : deck.customShows.at(custom).slides) {
            for (qsizetype i = 0; i < deck.slides.size(); ++i) {
                if (deck.slides.at(i).id == id && !deck.slides.at(i).hidden) {
                    rows.append(int(i));
                    break;
                }
            }
        }
        return rows;
    }
    int first = 0;
    int last = int(deck.slides.size()) - 1;
    if (settings.range == ShowRange::Slides) {
        first = qMax(0, settings.from - 1);
        last = qMin(last, qMax(first, settings.to - 1));
    }
    for (int i = first; i <= last; ++i) {
        if (!deck.slides.at(i).hidden) {
            rows.append(i);
        }
    }
    return rows;
}

QString formatTiming(qint64 ms)
{
    const qint64 seconds = qMax<qint64>(0, (ms + 500) / 1000);
    const auto two = [](qint64 n) { return QStringLiteral("%1").arg(n, 2, 10, QLatin1Char('0')); };
    if (seconds >= 3600) {
        return QStringLiteral("%1:%2:%3").arg(two(seconds / 3600), two(seconds % 3600 / 60), two(seconds % 60));
    }
    return QStringLiteral("%1:%2").arg(two(seconds / 60), two(seconds % 60));
}

} // namespace QindaOffice::Show
