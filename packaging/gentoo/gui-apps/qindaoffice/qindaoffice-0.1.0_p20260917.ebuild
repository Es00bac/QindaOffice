# Copyright 2026 QindaQt contributors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

inherit cmake xdg

DESCRIPTION="QindaQt Office - word processor, spreadsheet, presentations and notes for the QindaQt desktop"
HOMEPAGE="https://github.com/Es00bac/QindaQt"
SRC_URI="${P}.tar.xz"
S="${WORKDIR}/${P}"

LICENSE="GPL-3+"
SLOT="0"
KEYWORDS="~amd64"
IUSE="+charts +libreoffice"

# The QindaOffice repository is not published yet, so this is a local,
# immutable snapshot rather than a download. The operator produces the
# archive from the exact reviewed commit with the tree's own script:
#
#     tools/make-dist.sh 0.1.0_p20260917 /var/cache/distfiles
#
# That script refuses to run on a dirty tree and vendors the QXlsx
# submodule INTO the tarball, which is what makes the build work under
# FEATURES=network-sandbox: src_unpack fetches nothing. The archive also
# carries a .dist-commit file naming the commit it was cut from.
RESTRICT="fetch"

# gui-wm/qindaqt-desktop owns the QindaQt global-menu static archives and
# headers office_shell links (no CMake package configs are installed for
# them, hence the find_library use in CMakeLists.txt). Qt Charts is optional
# (QQO_WITH_CHARTS) and only needed by the M10 chart features. Qt Svg is
# not: the suite ships its icons as scalable SVG only, and the top-level
# find_package lists Svg as REQUIRED.
RDEPEND="
	>=gui-wm/qindaqt-desktop-0.1.0_pre20260907-r1
	>=dev-qt/qtbase-6.11:6=[dbus,gui,network,widgets]
	>=dev-qt/qtsvg-6.11:6=
	>=kde-frameworks/karchive-6.0:6=
	>=kde-frameworks/sonnet-6.0:6=
	dev-libs/qtkeychain
	app-text/poppler[qt6]
	libreoffice? ( app-office/libreoffice )
	charts? ( dev-qt/qtcharts:6 )
"
DEPEND="${RDEPEND}"
BDEPEND="
	virtual/pkgconfig
"

pkg_nofetch() {
	einfo "QindaOffice is not published to a remote yet. Produce the archive"
	einfo "from the reviewed commit in a QindaOffice checkout:"
	einfo ""
	einfo "    tools/make-dist.sh ${PV} \"${DISTDIR}\""
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
