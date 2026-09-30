# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 QindaQt contributors
# Distributed under the terms of the GNU General Public License v3 or later

EAPI=8

inherit cmake optfeature xdg

DESCRIPTION="QindaQt Office: documents, sheets, slides, notes, mail, books, databases, plans"
HOMEPAGE="https://github.com/Es00bac/QindaQt"
SRC_URI="${PF}.tar.xz"
S="${WORKDIR}/${PF}"

LICENSE="GPL-3+"
SLOT="0"
KEYWORDS="~amd64"
IUSE="+charts +libreoffice"

# The QindaOffice repository is not published yet, so this is a local,
# immutable snapshot rather than a download. The operator produces the
# archive from the exact reviewed commit with the tree's own script:
#
#     tools/make-dist.sh 0.1.0_p20260925 /var/cache/distfiles
#
# That script refuses to run on a dirty tree and vendors the QXlsx
# submodule INTO the tarball, which is what makes the build work under
# FEATURES=network-sandbox: src_unpack fetches nothing. The archive also
# carries a .dist-commit file naming the commit it was cut from.
#
# The archive is named after PF, not P: a re-cut on the same day is a new
# revision with DIFFERENT contents, and ${P}.tar.xz would hand two of them
# the same distfile name and the same Manifest line.
RESTRICT="fetch"

# gui-wm/qindaqt-desktop owns the QindaQt global-menu static archives, the
# design-token and theme libraries and their headers, which office_quick
# links (no CMake package configs are installed for them, hence the
# find_library use in CMakeLists.txt). dev-libs/qindatk is the toolkit the
# whole suite's UI is built from (M31, D-031) and dev-qt/qtdeclarative the
# QtQuick it needs.
#
# qtbase[widgets] stays: nothing in the suite is a QWidget any more, but
# QtPrintSupport links QtWidgets and the suite prints through QPrinter.
#
# USE=charts drives QQO_WITH_CHARTS, QindaCalc's chart feature. The
# charts are drawn by the suite's own ChartItem (D-036), so the flag no
# longer pulls dev-qt/qtcharts. Qt Svg is required: the suite ships its
# icons as scalable SVG only.
#
# 0.1.0_p20260925 (M40-M51) adds QindaMail, QindaBooks, QindaBase, QindaDiagram
# and QindaPlan and moves every app to the QindaTK ribbon:
#   - dev-libs/qindatk-0.1.0-r7: Ribbon v2, the mail/calendar providers
#     (POP3, IMAP folders, CalDAV discovery, iCalendar feeds), the calendar
#     grid and the icons the new apps use;
#   - qtbase[sql,sqlite]: QindaBooks keeps its company file in SQLite
#     through QtSql; the QSQLITE driver is loaded at RUN time, so a Qt
#     without it builds, links and then fails on the first open;
#   - dev-db/sqlite: QindaBase talks to SQLite directly and needs the
#     session extension and column metadata; Gentoo's sqlite always builds
#     both (--enable-session, SQLITE_ENABLE_COLUMN_METADATA), and configure
#     fails with a clear message on a SQLite without them;
#   - qtbase[ssl]: mail, calendar, Mercury and Stripe talk TLS only (D-043,
#     D-048); dev-libs/qtkeychain holds every token and password (D-052),
#     through libsecret or KWallet;
#   - qtbase[concurrent]: QindaCalc loads workbooks on a worker thread.
# 0.1.0_p20260926 (QindaOffice d0a0b2c, D-053): recovered documents no
# longer re-journal under a fresh key (QindaWrite reopened every discarded
# untitled document, more each launch); QindaNote types dark ink on paper
# on dark themes and takes touch — one finger inks, two fingers zoom and
# pan — which needs dev-libs/qindatk-0.1.0-r8 (D-304).
# -r1 (QindaOffice ce86fcc): a restored window drops any journal key it
# took while being built, and clean documents never journal — QindaWrite
# still re-saved every recovered document under a second key, so Discard
# never made it stay gone.
# -r2 (QindaOffice bd25fca, D-054): the global menu export holds changes
# while its window is inactive — the panel closed every Office menu the
# instant it opened, because opening it flipped Paste/Undo and republished.
# -r3 (QindaOffice 0400b81): dock panel toggles no longer bounce between the
# View action and the dock (a QML binding loop in Note, Write, Calc, Show).
# -r4 (QindaOffice 0fbb89d, D-055): unsaved documents from an earlier run are
# offered in ONE Recovered Documents dialog (instead of one window each);
# discarding — or just closing the dialog — makes them stay gone.
# 0.1.0_p20260927 (QindaOffice 980d85d, M52, D-057): QindaNote live notes —
# speech from the microphone, the desktop's output or both becomes notes a
# model keeps revising on the page. New dependencies:
#   - dev-qt/qtwebsockets: ElevenLabs realtime speech-to-text (office_speech);
#   - media-video/pipewire: capture runs pw-record (the sink monitor for
#     desktop audio, which QtMultimedia cannot open);
#   - installs /usr/libexec/qindaoffice/qindaoffice-whisper, the local
#     Whisper helper; faster-whisper and its models are optional (below).
# -r1 (QindaOffice b72c0ec, M53, D-058): the assistant edits documents in
# Write, Calc, Show and Note itself (one undo step, Undo change), pasted
# replies in QindaNote render as markdown, and live notes get a Detail
# slider (condensed by default). No new dependencies.
# -r2 (QindaOffice 428a49d, M54, D-059): live notes open a session setup
# first — kind of session (lecture, meeting, interview, tutorial, talk),
# what to focus on and leave out, which sections to keep, and custom
# instructions. No new dependencies.
# -r3 (QindaOffice e938dc7, M55, D-060): "Who said what" — speaker-labelled
# live notes through ElevenLabs batch diarization; Laya's yes/no answers
# are read correctly. No new dependencies.
# -r4 (QindaOffice 14d3f3c, M56, D-061): live notes round two — the "this
# matters" flag, prep material and series memory, the kept recording with
# replay (dev-qt/qtmultimedia[qml]), the Session Review panel, action items
# handed to QindaMail as tasks, study pages, export, split by topic, silence
# handling, screen capture (kde-plasma/spectacle, optional), usage meter.
# 0.1.0_p20260929 (M58, D-064): live Calc agent state, native session grants,
# CLI/MCP edits, revision checks, receipts and named undo. QindaTK r13 owns
# the agent SDK and Python-standard-library qinda-agent client.
# -r1 (M57/M59, D-063/D-065): organised live notes, technical/planning/
# troubleshooting structures, source-bound reviewed follow-ups, transcript
# answers and the live Note CLI/MCP provider. No new dependencies.
RDEPEND="
	>=gui-wm/qindaqt-desktop-0.1.0_pre20260907-r1
	>=dev-libs/qindatk-0.1.0-r13
	>=dev-qt/qtbase-6.11:6=[concurrent,dbus,gui,network,sql,sqlite,ssl,widgets]
	>=dev-qt/qtdeclarative-6.11:6=
	>=dev-qt/qtsvg-6.11:6=
	>=dev-qt/qtwebsockets-6.11:6=
	>=dev-qt/qtmultimedia-6.11:6=[qml]
	media-video/pipewire
	>=kde-frameworks/karchive-6.0:6=
	>=kde-frameworks/sonnet-6.0:6=
	dev-libs/qtkeychain:=
	>=dev-db/sqlite-3.50:3
	app-text/poppler[qt6]
	libreoffice? ( app-office/libreoffice )
"
DEPEND="${RDEPEND}"
BDEPEND="
	virtual/pkgconfig
"

pkg_nofetch() {
	einfo "QindaOffice is not published to a remote yet. Produce the archive"
	einfo "from the reviewed commit in a QindaOffice checkout:"
	einfo ""
	einfo "    tools/make-dist.sh ${PVR} \"${DISTDIR}\""
	einfo ""
	einfo "then re-run emerge. The script vendors third_party/QXlsx into the"
	einfo "tarball so the build needs no network."
}

src_configure() {
	local mycmakeargs=(
		-DBUILD_TESTING=OFF
		-DQQO_WITH_CHARTS=$(usex charts ON OFF)
	)
	cmake_src_configure
}

pkg_postinst() {
	xdg_pkg_postinst
	optfeature "Access .mdb/.accdb import in QindaBase" app-office/mdbtools
	optfeature "local Whisper speech-to-text for QindaNote live notes" \
		"dev-python/faster-whisper app-accessibility/faster-whisper-models"
	optfeature "reusing Gabbee's ElevenLabs key (Secret Service lookup)" app-crypt/libsecret
	optfeature "screen capture for live notes (slides to image-reading models)" kde-plasma/spectacle
}
