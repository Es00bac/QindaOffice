# Copyright 2026 QindaQt contributors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

# git-r3 pulls the pinned release commit from the local development checkout.
# AGENT-NOTE: once the repository is hosted, replace this with a standard
# SRC_URI tarball (the CMake layout stays identical); bump the version and
# commit together and regenerate the Manifest.
inherit cmake xdg git-r3

DESCRIPTION="QindaQt Office - word processor, spreadsheet, presentations and notes for the QindaQt desktop"
HOMEPAGE="https://github.com/Es00bac/QindaQt"
EGIT_REPO_URI="file:///home/cabewse/work_SPaC3/QindaOffice"
# v0.1.0 (pinned on release; update with the release commit)
EGIT_COMMIT=""

LICENSE="GPL-3+"
SLOT="0"
KEYWORDS="~amd64"
IUSE="+libreoffice"

# gui-wm/qindaqt-desktop owns the QindaQt global-menu static archives and
# headers office_shell links (no CMake package configs are installed for
# them, hence the find_library use in CMakeLists.txt). Qt Charts is optional
# (QQO_WITH_CHARTS) and only needed by the M10 chart features.
RDEPEND="
	>=gui-wm/qindaqt-desktop-0.1.0_pre20260907-r1
	>=dev-qt/qtbase-6.11:6=[dbus,gui,wayland,widgets]
	>=kde-frameworks/karchive-6.0:6=
	>=kde-frameworks/sonnet-6.0:6=
	dev-libs/qtkeychain[qt6]
	x11-libs/poppler[qt6]
	libreoffice? ( app-office/libreoffice )
"
DEPEND="${RDEPEND}"
BDEPEND="
	virtual/pkgconfig
"

src_configure() {
	local mycmakeargs=(
		-DBUILD_TESTING=OFF
	)
	cmake_src_configure
}
