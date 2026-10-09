// SPDX-License-Identifier: GPL-3.0-or-later
// Input masks: parsing Access's mask syntax, shaping typed text, showing a
// stored value through its mask, and the wizard's presets. See
// base_input_mask.h for the contract.
#include "base_input_mask.h"

#include <QVariantMap>

namespace QindaOffice::Base {

namespace {

using Element = InputMask::Element;

bool asciiDigit(QChar ch)
{
    return ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
}

bool slotClass(QChar ch)
{
    return QStringLiteral("09#L?Aa&C").contains(ch);
}

bool requiredClass(QChar ch)
{
    return ch == QLatin1Char('0') || ch == QLatin1Char('L') || ch == QLatin1Char('A') || ch == QLatin1Char('&');
}

// AGENT-NOTE: a typed space is a separator, never slot content (except
// for & and C): "503 555 1234" must fill (999) 000-0000.
bool accepts(QChar cls, QChar ch)
{
    switch (cls.unicode()) {
    case '0':
    case '9': return asciiDigit(ch);
    case '#': return asciiDigit(ch) || ch == QLatin1Char('+') || ch == QLatin1Char('-');
    case 'L':
    case '?': return ch.isLetter();
    case 'A':
    case 'a': return ch.isLetterOrNumber();
    case '&':
    case 'C': return !ch.isNull();
    default: return false;
    }
}

QString expects(QChar cls)
{
    switch (cls.unicode()) {
    case '0':
    case '9':
    case '#': return QStringLiteral("a digit");
    case 'L':
    case '?': return QStringLiteral("a letter");
    case 'A':
    case 'a': return QStringLiteral("a letter or a digit");
    default: return QStringLiteral("a character");
    }
}

QChar cased(QChar ch, int letterCase)
{
    return letterCase > 0 ? ch.toUpper() : letterCase < 0 ? ch.toLower() : ch;
}

// The literal characters right after element `index`, up to the next slot.
QString followingLiterals(const InputMask &mask, int index)
{
    QString out;
    for (int k = index + 1; k < mask.elements.size(); ++k) {
        if (mask.elements.at(k).kind == Element::Kind::Slot) {
            break;
        }
        out += mask.elements.at(k).ch;
    }
    return out;
}

// How many leftmost optional slots a right-filled mask leaves empty for
// `typed` characters.
int rightFillGap(const InputMask &mask, int typed)
{
    if (!mask.rightFill) {
        return 0;
    }
    int optional = 0;
    for (const Element &e : mask.elements) {
        optional += e.kind == Element::Kind::Slot && !e.required ? 1 : 0;
    }
    return qMax(0, qMin(optional, mask.slotCount() - typed));
}

// Splits the mask text into its ';' sections ("…" and \x protect a ';').
QStringList sections(const QString &text)
{
    QStringList out{QString()};
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\\') && !quoted && i + 1 < text.size()) {
            out.last() += ch;
            out.last() += text.at(++i);
            continue;
        }
        if (ch == QLatin1Char('"')) {
            quoted = !quoted;
        }
        if (ch == QLatin1Char(';') && !quoted && out.size() < 3) {
            out.append(QString());
            continue;
        }
        out.last() += ch;
    }
    return out;
}

} // namespace

int InputMask::slotCount() const
{
    int n = 0;
    for (const Element &e : elements) {
        n += e.kind == Element::Kind::Slot ? 1 : 0;
    }
    return n;
}

std::optional<InputMask> InputMask::parse(const QString &text, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error != nullptr) {
            *error = why;
        }
        return std::nullopt;
    };
    InputMask mask;
    mask.text = text.trimmed();
    if (mask.text.isEmpty()) {
        return mask;
    }
    if (mask.text.compare(QLatin1String("Password"), Qt::CaseInsensitive) == 0) {
        mask.password = true;
        return mask;
    }
    const QStringList parts = sections(mask.text);
    const QString body = parts.value(0);
    int letterCase = 0;
    for (int i = 0; i < body.size(); ++i) {
        const QChar ch = body.at(i);
        if (ch == QLatin1Char('\\')) {
            if (i + 1 >= body.size()) {
                return fail(QStringLiteral("The input mask ends with \\; put the character to show after it."));
            }
            mask.elements.append({Element::Kind::Literal, body.at(++i), false, 0});
        } else if (ch == QLatin1Char('"')) {
            const int close = int(body.indexOf(QLatin1Char('"'), i + 1));
            if (close < 0) {
                return fail(QStringLiteral("The input mask has a \"text\" that is not closed."));
            }
            for (int k = i + 1; k < close; ++k) {
                mask.elements.append({Element::Kind::Literal, body.at(k), false, 0});
            }
            i = close;
        } else if (ch == QLatin1Char('>')) {
            letterCase = 1;
        } else if (ch == QLatin1Char('<')) {
            if (i + 1 < body.size() && body.at(i + 1) == QLatin1Char('>')) {
                letterCase = 0;
                ++i;
            } else {
                letterCase = -1;
            }
        } else if (ch == QLatin1Char('!')) {
            mask.rightFill = true;
        } else if (slotClass(ch)) {
            mask.elements.append({Element::Kind::Slot, ch, requiredClass(ch), letterCase});
        } else {
            mask.elements.append({Element::Kind::Literal, ch, false, 0});
        }
    }
    if (mask.slotCount() == 0) {
        return fail(QStringLiteral("An input mask needs at least one place for a character: 0, 9, #, L, ?, A, a, & "
                                   "or C."));
    }
    mask.storeLiterals = parts.value(1).trimmed() == QLatin1String("0");
    QString holder = parts.value(2);
    holder.remove(QLatin1Char('"'));
    if (!holder.isEmpty()) {
        mask.placeholder = holder.at(0);
    }
    return mask;
}

MaskResult applyInputMask(const InputMask &mask, const QString &input)
{
    MaskResult result;
    const QString in = input.trimmed();
    if (mask.isEmpty() || mask.password || in.isEmpty()) {
        result.text = mask.password ? input : in;
        return result;
    }
    int typedCount = 0;
    for (const QChar ch : in) {
        typedCount += ch.isLetterOrNumber() ? 1 : 0;
    }
    // "!": with fewer typed characters than slots, the leftmost optional
    // slots stay empty (a phone number typed without its area code).
    int skipOptional = rightFillGap(mask, typedCount);
    QString shaped;    // with literals
    QString typedOnly; // the characters that filled slots
    int j = 0;
    int filled = 0;
    int lastFilledEnd = 0;
    const int n = int(in.size());
    for (int index = 0; index < mask.elements.size(); ++index) {
        const Element &e = mask.elements.at(index);
        if (e.kind == Element::Kind::Literal) {
            if (j < n && in.at(j) == e.ch) {
                ++j; // the literal was typed too
            }
            shaped += e.ch;
            continue;
        }
        if (!e.required && skipOptional > 0) {
            --skipOptional;
            shaped += QLatin1Char(' ');
            continue;
        }
        // Separators the slot cannot hold are skipped — unless an optional
        // slot is followed by that very literal ("3/14" in 99/99): then the
        // slot stays empty and the literal matches next.
        const QString next = followingLiterals(mask, int(index));
        while (j < n && !accepts(e.ch, in.at(j)) && !in.at(j).isLetterOrNumber()
               && (e.required || !next.contains(in.at(j)))) {
            ++j;
        }
        if (j < n && accepts(e.ch, in.at(j))) {
            const QChar ch = cased(in.at(j), e.letterCase);
            shaped += ch;
            typedOnly += ch;
            ++j;
            ++filled;
            lastFilledEnd = int(shaped.size());
            continue;
        }
        if (!e.required) {
            continue; // an empty optional slot
        }
        result.ok = false;
        result.error = j < n ? QStringLiteral("has \"%1\" where %2 is expected; it must look like %3")
                                   .arg(in.at(j))
                                   .arg(expects(e.ch), maskHint(mask))
                             : QStringLiteral("is too short; it must look like %1").arg(maskHint(mask));
        return result;
    }
    while (j < n && !in.at(j).isLetterOrNumber()) {
        ++j;
    }
    if (j < n) {
        result.ok = false;
        result.error = QStringLiteral("is too long; it must look like %1").arg(maskHint(mask));
        return result;
    }
    if (filled == 0) {
        return result; // nothing typed
    }
    if (filled < mask.slotCount()) {
        shaped = shaped.left(lastFilledEnd); // no trailing empty slots or their literals
    }
    result.text = mask.storeLiterals ? shaped : typedOnly;
    return result;
}

QString maskedDisplay(const InputMask &mask, const QString &stored)
{
    if (mask.password) {
        return QString(stored.size(), QLatin1Char('*'));
    }
    if (mask.isEmpty() || stored.isEmpty() || mask.storeLiterals) {
        return stored;
    }
    // Typed characters only: put them back into the slots, left to right
    // (right-filled masks align them to the right).
    int skipOptional = rightFillGap(mask, int(stored.size()));
    QString out;
    int j = 0;
    int lastFilledEnd = 0;
    for (const Element &e : mask.elements) {
        if (e.kind == Element::Kind::Literal) {
            out += e.ch;
            continue;
        }
        if (!e.required && skipOptional > 0) {
            --skipOptional;
            out += QLatin1Char(' ');
            continue;
        }
        if (j < stored.size() && accepts(e.ch, stored.at(j))) {
            out += stored.at(j++);
            lastFilledEnd = int(out.size());
        } else if (e.required) {
            return stored; // does not fit: show it as it is
        }
    }
    if (j < stored.size()) {
        return stored;
    }
    return out.left(lastFilledEnd);
}

QString maskHint(const InputMask &mask)
{
    if (mask.password) {
        return QStringLiteral("********");
    }
    QString out;
    for (const Element &e : mask.elements) {
        out += e.kind == Element::Kind::Literal ? e.ch : mask.placeholder;
    }
    return out;
}

QVariantList inputMaskPresets()
{
    // AGENT-NOTE: Access's Input Mask Wizard (US list) without the time
    // masks — QindaBase has no time-only type.
    const auto preset = [](const char *id, const char *label, const char *mask, const char *sample) {
        return QVariantMap{{QStringLiteral("id"), QString::fromLatin1(id)},
                           {QStringLiteral("label"), QString::fromUtf8(label)},
                           {QStringLiteral("mask"), QString::fromUtf8(mask)},
                           {QStringLiteral("sample"), QString::fromUtf8(sample)}};
    };
    return {preset("phone", "Phone number", "!(999) 000-0000;;_", "(503) 555-0142"),
            preset("ssn", "Social Security number", "000-00-0000;;_", "531-86-7180"),
            preset("zip", "ZIP code", "00000-9999;;_", "97201-1234"),
            preset("postalCanada", "Canadian postal code", ">L0L 0L0;;_", "K1A 0B1"),
            preset("extension", "Extension", "!99999;;_", "63215"),
            preset("shortDate", "Short date", "99/99/0000;0;_", "3/14/2026"),
            preset("password", "Password", "Password", "********")};
}

} // namespace QindaOffice::Base
