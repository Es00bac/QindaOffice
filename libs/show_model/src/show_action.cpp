// SPDX-License-Identifier: GPL-3.0-or-later
// Action settings: links, action buttons and built-in sounds (show_action.h).

#include "show_action.h"

#include <QCoreApplication>
#include <QUrlQuery>
#include <QtEndian>
#include <QtMath>

#include <cmath>
#include <cstring>

namespace QindaOffice::Show {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaShow", text);
}

const QString kShowJump = QStringLiteral("ppaction://hlinkshowjump");
const QString kCustomShow = QStringLiteral("ppaction://customshow");

struct Jump {
    ActionKind kind;
    const char *name;
};
constexpr Jump kJumps[] = {{ActionKind::NextSlide, "nextslide"},   {ActionKind::PreviousSlide, "previousslide"},
                           {ActionKind::FirstSlide, "firstslide"}, {ActionKind::LastSlide, "lastslide"},
                           {ActionKind::LastViewed, "lastslideviewed"}, {ActionKind::EndShow, "endshow"}};

// The query of a "ppaction://verb?a=b&c=d" link (QUrl does not parse the
// ppaction scheme's authority-less form the way the query needs).
QUrlQuery queryOf(const QString &link)
{
    const qsizetype at = link.indexOf(QLatin1Char('?'));
    return QUrlQuery(at >= 0 ? link.mid(at + 1) : QString());
}

bool isVerb(const QString &link, const QString &verb)
{
    return link == verb || link.startsWith(verb + QLatin1Char('?'));
}

} // namespace

QString webScheme(const QString &address)
{
    const qsizetype colon = address.indexOf(QLatin1Char(':'));
    // "C:\..." is a Windows path, not a scheme; schemes are 2+ letters.
    if (colon < 2) {
        return {};
    }
    const QString scheme = address.left(colon).toLower();
    for (const QChar c : scheme) {
        if (!(c.isLetterOrNumber() || c == u'+' || c == u'-' || c == u'.')) {
            return {};
        }
    }
    return scheme == u"file" ? QString() : scheme;
}

ActionTarget parseAction(const QString &link, const QString &address)
{
    ActionTarget target;
    if (link.isEmpty()) {
        return target;
    }
    if (link.startsWith(QLatin1Char('#'))) {
        target.kind = ActionKind::Slide;
        target.slideId = link.mid(1);
        return target;
    }
    if (!link.startsWith(QLatin1String("ppaction://"))) {
        target.kind = webScheme(link).isEmpty() ? ActionKind::File : ActionKind::Url;
        target.address = link;
        return target;
    }
    if (isVerb(link, kShowJump)) {
        const QString jump = queryOf(link).queryItemValue(QStringLiteral("jump")).toLower();
        for (const Jump &entry : kJumps) {
            if (jump == QLatin1String(entry.name)) {
                target.kind = entry.kind;
                return target;
            }
        }
        target.kind = ActionKind::Other;
        return target;
    }
    if (isVerb(link, kCustomShow)) {
        const QUrlQuery query = queryOf(link);
        bool ok = false;
        target.customShow = query.queryItemValue(QStringLiteral("id")).toUInt(&ok);
        target.kind = ok ? ActionKind::CustomShow : ActionKind::Other;
        target.returnAfter = query.queryItemValue(QStringLiteral("return")).compare(u"true", Qt::CaseInsensitive) == 0;
        return target;
    }
    if (isVerb(link, QStringLiteral("ppaction://program"))) {
        target.kind = ActionKind::Program;
        target.address = address;
    } else if (isVerb(link, QStringLiteral("ppaction://hlinkfile"))) {
        target.kind = ActionKind::File;
        target.address = address;
    } else if (isVerb(link, QStringLiteral("ppaction://hlinkpres"))) {
        target.kind = ActionKind::Presentation;
        target.address = address;
    } else if (isVerb(link, QStringLiteral("ppaction://macro"))) {
        target.kind = ActionKind::Macro;
        target.address = queryOf(link).queryItemValue(QStringLiteral("name"));
    } else if (isVerb(link, QStringLiteral("ppaction://media"))) {
        target.kind = ActionKind::Media;
    } else if (isVerb(link, QStringLiteral("ppaction://noaction"))) {
        target.kind = ActionKind::None;
    } else {
        target.kind = ActionKind::Other;
    }
    return target;
}

QString jumpLink(ActionKind kind)
{
    for (const Jump &entry : kJumps) {
        if (entry.kind == kind) {
            return kShowJump + QStringLiteral("?jump=") + QLatin1String(entry.name);
        }
    }
    return {};
}

QString customShowLink(quint32 number, bool returnAfter)
{
    return kCustomShow + QStringLiteral("?id=%1").arg(number) + (returnAfter ? QStringLiteral("&return=true") : QString());
}

QString slideLink(const QString &slideId)
{
    return QLatin1Char('#') + slideId;
}

QList<ActionButtonPreset> actionButtonPresets()
{
    // AGENT-NOTE: the defaults are PowerPoint's, except Document — PowerPoint
    // starts it on "Run program", which QindaShow never does; it starts
    // empty and Action Settings opens on it (as on every new button).
    return {
        {QStringLiteral("actionButtonBackPrevious"), tr("Back or Previous"), QStringLiteral("chevron-left"),
         jumpLink(ActionKind::PreviousSlide)},
        {QStringLiteral("actionButtonForwardNext"), tr("Forward or Next"), QStringLiteral("chevron-right"),
         jumpLink(ActionKind::NextSlide)},
        {QStringLiteral("actionButtonBeginning"), tr("Beginning"), QStringLiteral("skip-back"),
         jumpLink(ActionKind::FirstSlide)},
        {QStringLiteral("actionButtonEnd"), tr("End"), QStringLiteral("skip-forward"), jumpLink(ActionKind::LastSlide)},
        {QStringLiteral("actionButtonHome"), tr("Home"), QStringLiteral("house"), jumpLink(ActionKind::FirstSlide)},
        {QStringLiteral("actionButtonInformation"), tr("Information"), QStringLiteral("info"), QString()},
        {QStringLiteral("actionButtonReturn"), tr("Return"), QStringLiteral("corner-up-left"),
         jumpLink(ActionKind::LastViewed)},
        {QStringLiteral("actionButtonMovie"), tr("Video"), QStringLiteral("film"), QString()},
        {QStringLiteral("actionButtonDocument"), tr("Document"), QStringLiteral("file-text"), QString()},
        {QStringLiteral("actionButtonSound"), tr("Sound"), QStringLiteral("volume-2"), QString()},
        {QStringLiteral("actionButtonHelp"), tr("Help"), QStringLiteral("circle-question-mark"), QString()},
        {QStringLiteral("actionButtonBlank"), tr("Custom"), QStringLiteral("square"), QString()},
    };
}

// ---- sounds -------------------------------------------------------------------------

QStringList builtInSounds()
{
    return {QStringLiteral("Chime"), QStringLiteral("Click"), QStringLiteral("Beep")};
}

QByteArray builtInSound(const QString &name)
{
    // 16-bit mono PCM at 22.05 kHz: a decaying two-note chime, a 30 ms
    // click, a 200 ms 880 Hz beep.
    constexpr int kRate = 22050;
    QList<qint16> samples;
    const auto tone = [&samples](qreal frequency, int ms, qreal amplitude, qreal decay) {
        const int count = kRate * ms / 1000;
        for (int i = 0; i < count; ++i) {
            const qreal t = qreal(i) / kRate;
            const qreal envelope = std::exp(-decay * t) * qMin<qreal>(1.0, i / 64.0);
            samples.append(qint16(32767 * amplitude * envelope * std::sin(2 * M_PI * frequency * t)));
        }
    };
    if (name == u"Chime") {
        tone(1318.5, 220, 0.5, 6);
        tone(1046.5, 420, 0.5, 5);
    } else if (name == u"Click") {
        tone(2200, 30, 0.6, 120);
    } else if (name == u"Beep") {
        tone(880, 200, 0.4, 2);
    } else {
        return {};
    }
    const quint32 dataBytes = quint32(samples.size() * 2);
    QByteArray out(44 + int(dataBytes), '\0');
    char *p = out.data();
    memcpy(p, "RIFF", 4);
    qToLittleEndian<quint32>(36 + dataBytes, p + 4);
    memcpy(p + 8, "WAVEfmt ", 8);
    qToLittleEndian<quint32>(16, p + 16);
    qToLittleEndian<quint16>(1, p + 20);             // PCM
    qToLittleEndian<quint16>(1, p + 22);             // mono
    qToLittleEndian<quint32>(kRate, p + 24);         // sample rate
    qToLittleEndian<quint32>(kRate * 2, p + 28);     // byte rate
    qToLittleEndian<quint16>(2, p + 32);             // block align
    qToLittleEndian<quint16>(16, p + 34);            // bits
    memcpy(p + 36, "data", 4);
    qToLittleEndian<quint32>(dataBytes, p + 40);
    for (qsizetype i = 0; i < samples.size(); ++i) {
        qToLittleEndian<qint16>(samples.at(i), p + 44 + 2 * i);
    }
    return out;
}

bool isWave(const QByteArray &bytes)
{
    return bytes.size() > 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WAVE";
}

} // namespace QindaOffice::Show
