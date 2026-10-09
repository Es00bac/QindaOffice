// SPDX-License-Identifier: GPL-3.0-or-later
// Color categories: presets, values, IMAP keywords (mail_categories.h).
#include "mail_categories.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <limits>

namespace QindaOffice::Mail::Categories {

using namespace Qt::StringLiterals;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaOffice::Mail::Categories", text);
}

// RFC 3501 ATOM-CHAR minus what this mapping always encodes: "_" stands for
// a space and "&" opens a modified UTF-7 run.
bool plainAtomChar(QChar c)
{
    const ushort u = c.unicode();
    if (u < 0x21 || u > 0x7e) return false;
    return !QStringView(u"(){%*\"\\]_&").contains(c);
}

QByteArray utf7Run(const QString &run)
{
    QByteArray utf16;
    utf16.reserve(run.size() * 2);
    for (const QChar c : run) {
        utf16.append(char(c.unicode() >> 8));
        utf16.append(char(c.unicode() & 0xff));
    }
    QByteArray base64 = utf16.toBase64(QByteArray::OmitTrailingEquals);
    base64.replace('/', ',');
    return '&' + base64 + '-';
}

// Modified UTF-7 runs back to text. AGENT-NOTE: QindaTK's
// decodeMailboxName refuses a run that encodes printable ASCII (RFC 3501
// forbids that in mailbox names), but a keyword run here may hold "(",
// "_" or "$" — so the keyword side decodes its own runs.
QString decodeRuns(const QString &wire, bool *ok)
{
    QString out;
    *ok = true;
    for (qsizetype i = 0; i < wire.size(); ++i) {
        if (wire.at(i) != u'&') {
            out.append(wire.at(i));
            continue;
        }
        const qsizetype end = wire.indexOf(u'-', i + 1);
        if (end < 0) {
            *ok = false;
            return wire;
        }
        if (end == i + 1) {
            out.append(u'&');
        } else {
            QByteArray base64 = wire.mid(i + 1, end - i - 1).toLatin1();
            base64.replace(',', '/');
            const QByteArray utf16 = QByteArray::fromBase64(base64);
            if (utf16.size() % 2 != 0) {
                *ok = false;
                return wire;
            }
            for (qsizetype j = 0; j + 1 < utf16.size(); j += 2)
                out.append(QChar(ushort((uchar(utf16.at(j)) << 8) | uchar(utf16.at(j + 1)))));
        }
        i = end;
    }
    return out;
}

bool parseHex(const QString &color, int *r, int *g, int *b)
{
    static const QRegularExpression hex(u"^#([0-9a-fA-F]{6})$"_s);
    const QRegularExpressionMatch m = hex.match(color.trimmed());
    if (!m.hasMatch()) return false;
    const uint rgb = m.captured(1).toUInt(nullptr, 16);
    *r = int((rgb >> 16) & 0xff);
    *g = int((rgb >> 8) & 0xff);
    *b = int(rgb & 0xff);
    return true;
}

} // namespace

const QList<Preset> &presets()
{
    // AGENT-NOTE: the order IS the wire value (Graph categoryColor
    // preset<N>, EWS CategoryList color=N); the hex values approximate
    // Outlook's swatches and are user data, not chrome (AGENTS.md rule 3).
    static const QList<Preset> all{
        {"Red", "#d13438"},       {"Orange", "#f7630c"},     {"Brown", "#8e562e"},       {"Yellow", "#fce100"},
        {"Green", "#13a10e"},     {"Teal", "#00b7c3"},       {"Olive", "#8cbd18"},       {"Blue", "#0078d4"},
        {"Purple", "#8764b8"},    {"Cranberry", "#c50f1f"},  {"Steel", "#69797e"},       {"DarkSteel", "#3b4a54"},
        {"Gray", "#a19f9d"},      {"DarkGray", "#605e5c"},   {"Black", "#1b1a19"},       {"DarkRed", "#750b1c"},
        {"DarkOrange", "#8a3707"}, {"DarkBrown", "#4d291c"}, {"DarkYellow", "#c19c00"},  {"DarkGreen", "#0b6a0b"},
        {"DarkTeal", "#006666"},  {"DarkOlive", "#5a6e1a"},  {"DarkBlue", "#003966"},    {"DarkPurple", "#401b6c"},
        {"DarkCranberry", "#6e0811"}};
    return all;
}

QString colorForPreset(int preset)
{
    return preset >= 0 && preset < presets().size() ? QString::fromLatin1(presets().at(preset).color) : QString();
}

int presetForColor(const QString &color)
{
    int r = 0, g = 0, b = 0;
    if (!parseHex(color, &r, &g, &b)) return -1;
    int best = -1;
    int bestDistance = std::numeric_limits<int>::max();
    for (int i = 0; i < presets().size(); ++i) {
        int pr = 0, pg = 0, pb = 0;
        parseHex(QString::fromLatin1(presets().at(i).color), &pr, &pg, &pb);
        const int distance = (r - pr) * (r - pr) + (g - pg) * (g - pg) + (b - pb) * (b - pb);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

QList<Category> defaults()
{
    const auto make = [](const char *name, int preset) {
        Category c;
        c.name = QCoreApplication::translate("QindaOffice::Mail::Categories", name);
        c.preset = preset;
        c.color = colorForPreset(preset);
        return c;
    };
    return {make(QT_TRANSLATE_NOOP("QindaOffice::Mail::Categories", "Red category"), 0),
            make(QT_TRANSLATE_NOOP("QindaOffice::Mail::Categories", "Orange category"), 1),
            make(QT_TRANSLATE_NOOP("QindaOffice::Mail::Categories", "Yellow category"), 3),
            make(QT_TRANSLATE_NOOP("QindaOffice::Mail::Categories", "Green category"), 4),
            make(QT_TRANSLATE_NOOP("QindaOffice::Mail::Categories", "Blue category"), 7),
            make(QT_TRANSLATE_NOOP("QindaOffice::Mail::Categories", "Purple category"), 8)};
}

Category fromVariantMap(const QVariantMap &map)
{
    Category c;
    c.name = map.value(u"name"_s).toString().trimmed();
    c.preset = map.value(u"preset"_s, -1).toInt();
    if (c.preset < -1 || c.preset >= presets().size()) c.preset = -1;
    c.color = map.value(u"color"_s).toString().trimmed().toLower();
    int r = 0, g = 0, b = 0;
    if (!parseHex(c.color, &r, &g, &b)) c.color = colorForPreset(c.preset);
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.key() != "name"_L1 && it.key() != "color"_L1 && it.key() != "preset"_L1) c.extra.insert(it.key(), it.value());
    }
    return c;
}

QVariantMap toVariantMap(const Category &category)
{
    QVariantMap map = category.extra;
    map.insert(u"name"_s, category.name);
    map.insert(u"color"_s, category.color);
    map.insert(u"preset"_s, category.preset);
    return map;
}

QList<Category> listFromVariant(const QVariant &list)
{
    QList<Category> out;
    for (const QVariant &value : list.toList()) {
        const Category c = fromVariantMap(value.toMap());
        if (!c.name.isEmpty() && indexOf(out, c.name) < 0 && out.size() < kMaxCategories) out.append(c);
    }
    return out;
}

QVariantList listToVariant(const QList<Category> &categories)
{
    QVariantList out;
    for (const Category &category : categories) out.append(toVariantMap(category));
    return out;
}

int indexOf(const QList<Category> &list, const QString &name)
{
    const QString wanted = name.trimmed();
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).name.compare(wanted, Qt::CaseInsensitive) == 0) return i;
    }
    return -1;
}

QStringList normalized(const QStringList &names)
{
    QStringList out;
    for (const QString &name : names) {
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty()) continue;
        bool seen = false;
        for (const QString &have : std::as_const(out)) seen = seen || have.compare(trimmed, Qt::CaseInsensitive) == 0;
        if (!seen) out.append(trimmed.left(kMaxNameLength));
    }
    return out;
}

QStringList validate(const Category &category, const QList<Category> &others)
{
    QStringList problems;
    if (category.name.trimmed().isEmpty()) problems.append(u"name: "_s + tr("give the category a name"));
    else if (category.name.size() > kMaxNameLength) problems.append(u"name: "_s + tr("at most 255 characters"));
    else if (category.name.contains(u',') || category.name.contains(u';'))
        problems.append(u"name: "_s + tr("commas and semicolons separate categories; leave them out"));
    else if (indexOf(others, category.name) >= 0) problems.append(u"name: "_s + tr("a category with this name exists"));
    int r = 0, g = 0, b = 0;
    if (!category.color.isEmpty() && !parseHex(category.color, &r, &g, &b))
        problems.append(u"color: "_s + tr("not a #rrggbb colour"));
    return problems;
}

QString keywordFor(const QString &name)
{
    const QString trimmed = name.trimmed();
    QByteArray out;
    QString run;
    const auto flush = [&] {
        if (!run.isEmpty()) out += utf7Run(run);
        run.clear();
    };
    for (qsizetype i = 0; i < trimmed.size(); ++i) {
        const QChar c = trimmed.at(i);
        const bool leadingSpecial = i == 0 && (c == u'$' || c == u'\\');
        if (c == u' ') {
            flush();
            out += '_';
        } else if (c == u'&' && run.isEmpty()) {
            out += "&-";
        } else if (plainAtomChar(c) && !leadingSpecial) {
            flush();
            out += char(c.unicode());
        } else {
            run.append(c);
        }
    }
    flush();
    return QString::fromLatin1(out);
}

bool isCategoryKeyword(const QString &keyword)
{
    if (keyword.isEmpty() || keyword.startsWith(u'$') || keyword.startsWith(u'\\')) return false;
    for (const QChar c : keyword) {
        if (!(plainAtomChar(c) || c == u'_' || c == u'&')) return false;
    }
    return true;
}

QString nameForKeyword(const QString &keyword, const QList<Category> &known)
{
    if (!isCategoryKeyword(keyword)) return {};
    for (const Category &category : known) {
        if (keywordFor(category.name).compare(keyword, Qt::CaseInsensitive) == 0) return category.name;
    }
    QString wire = keyword;
    wire.replace(u'_', u' ');
    bool ok = false;
    const QString name = decodeRuns(wire, &ok).trimmed();
    return ok ? name : wire.trimmed();
}

} // namespace QindaOffice::Mail::Categories
