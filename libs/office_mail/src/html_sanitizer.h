// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The HTML mail sanitizer (M64, D-072): turns an untrusted text/html body
// into HTML that is safe for Qt's rich-text engine — QTextDocument::setHtml,
// a QML TextEdit with TextEdit.RichText, printing through QTextDocument — and
// keeps remote content (tracking pixels above all) out unless the user asked
// for it.
//
// Pure functions: no network, no file access, no shared state; callable from
// any thread. Cost is linear in the input, which is bounded (kMaxHtmlLength).
//
// How: html_tokenizer.h splits the input into tokens; the sanitizer
// (html_sanitizer*.cpp) applies an element and attribute allowlist, checks
// every URL and CSS declaration, and REBUILDS the document — lower-case
// names, every attribute value double-quoted and re-escaped, an open-element
// stack that only closes what it opened, its own <html><head><body> skeleton.
//
// AGENT-GUARD: output is regenerated from tokens, never copied: no input
// substring reaches `html` except as escaped text or as an attribute value or
// CSS declaration that passed its check. A "fast path" that splices source
// text through reopens every hole the tokenizer and regenerator close.
//
// AGENT-CONTRACT (this file ↔ the renderer: message_render.cpp, the QindaMail
// reading pane and print path). Image sources in `html` are only
//   * cidUrl(id)        for <img src="cid:…">: register the attachment whose
//                       Content-ID is `id` under QUrl(cidUrl(id));
//   * inlineImageUrl(i) for data:image/…;base64: register inlineImages[i]
//                       under QUrl(inlineImageUrl(i));
//   * the http(s) URLs of remoteImages, and only when allowRemoteImages.
// Register resources (QTextDocument::addResource, ImageResource) before
// setHtml: the document looks them up during layout and asks loadResource()
// for anything missing — QTextDocument's default reads local files for
// relative names, so the renderer should also refuse every name it did not
// register. A QML TextEdit can fetch http(s) images itself through the QML
// engine's network access; that is why they only appear when allowed.
// Inline payloads start with a PNG/JPEG/GIF/WebP/BMP signature; cid parts
// come from the message and are not checked here (load them as raster
// images, never as SVG).

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Mail {

// Bounds on untrusted input; each one keeps the sanitizer or Qt's layout
// affordable (see html_sanitizer.cpp and html_sanitizer_tree.cpp).
inline constexpr qsizetype kMaxHtmlLength = 16 * 1024 * 1024; // UTF-16 units; the rest is cut
inline constexpr int kMaxInlineImages = 256;   // data: images decoded per message
inline constexpr int kMaxRemoteImages = 256;   // distinct http(s) images; more are removed
inline constexpr int kMaxCidImages = 256;      // distinct cid: images; more are removed
inline constexpr int kMaxOpenElements = 256;   // nesting depth of the output
inline constexpr int kMaxTableColumns = 64;    // columns per table row (colspan included)
inline constexpr int kMaxTableCells = 10000;   // Σ rows × columns over all tables

struct HtmlSanitizeOptions {
    // The user chose "Show pictures" (or always shows them for this sender).
    bool allowRemoteImages = false;
    // Largest decoded data:image payload; larger ones are removed.
    int maxInlineImageBytes = 4 * 1024 * 1024;
    // Tables nested deeper than this are flattened into their enclosing cell.
    // Qt lays a cell's content out about twice per enclosing table (measured:
    // 224 KB of text costs 0.1 s at depth 0, 2 s at depth 6, 8 s at depth 8).
    int maxTableDepth = 6;
};

struct SanitizedHtml {
    // A complete "<html><head>…</head><body…>…</body></html>" document:
    // filtered <style> rules in the head, balanced elements in the body.
    QString html;
    // http(s) image URLs found in <img src> (protocol-relative ones as
    // https:), deduplicated in document order — listed whether or not they
    // were allowed. When allowed, `html` references exactly these URLs:
    // tracking pixels (2 × 2 px or less) are then dropped entirely, from
    // `html` and from this list.
    QStringList remoteImages;
    // An <img> was blocked, or a remote CSS url()/@import or a background=
    // attribute was dropped, because allowRemoteImages is false. (CSS and
    // background images stay dropped even when allowed.)
    bool remoteBlocked = false;
    // Content-IDs used by <img src="cid:…">: percent-decoded, without <>,
    // deduplicated in document order.
    QStringList cidReferences;
    // Decoded data:image/(png|jpeg|gif|webp|bmp);base64 payloads; `html`
    // refers to payload i as inlineImageUrl(i).
    QList<QByteArray> inlineImages;
    // The first <title>, whitespace-simplified (never part of `html`).
    QString title;
    // Elements removed outright, with their content where they had any:
    // script, iframe, object, form controls, svg, link/base, hidden
    // (display:none) elements, images with a forbidden or unusable source, …
    // Not counted: unwrapped tags (content kept), the html/head/body
    // skeleton, <title>, a plain <meta>, blocked remote images.
    int removedElements = 0;
    // Attributes dropped from kept elements: on* handlers, target, srcset,
    // background, links with a forbidden scheme, styles filtered to nothing…
    int removedAttributes = 0;
    // The input was longer than kMaxHtmlLength and only its start was used;
    // the reading pane should say so.
    bool truncated = false;
};

SanitizedHtml sanitizeHtml(const QString &html, const HtmlSanitizeOptions &options = {});

// The exact image source the sanitizer writes for <img src="cid:…"> with the
// (percent-decoded, bracket-free) Content-ID `contentId`: "cid:" plus the id
// percent-encoded except for unreserved characters and "@!$()*+,;=:/".
QString cidUrl(const QString &contentId);

// "qindamail-inline:<index>", the source written for inlineImages[index].
QString inlineImageUrl(int index);

} // namespace QindaOffice::Mail
