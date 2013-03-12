# Copyright 1999-2008 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: /var/cvsroot/gentoo-x86/media-video/dvbcut/Attic/dvbcut-0.5.4.ebuild,v 1.4 2008/08/06 23:23:17 aballier dead $

EAPI=2

inherit eutils qt4-r2 git-2 fdo-mime
IUSE=""

DESCRIPTION="frame-accurate editing of MPEG-2 video with MPEG and AC3 audio"
HOMEPAGE="http://dvbcut.sourceforge.net"
EGIT_REPO_URI="git://github.com/nextghost/${PN}.git
	https://github.com/nextghost/${PN}.git"

LICENSE="GPL-2"

SLOT="0"
KEYWORDS="~amd64 ~x86"

RDEPEND="dev-qt/qtcore
	dev-qt/qtgui
	media-libs/libao
	media-libs/libmad
	media-video/ffmpeg"

src_unpack() {
	git-2_src_unpack
}

src_prepare() {
	eqmake4 dvbcut.pro PREFIX=/usr
}

src_install() {
	emake INSTALL_ROOT="${D}" install || die "Install failed"
	dodoc ChangeLog CREDITS README
}

pkg_postinst() {
	fdo-mime_desktop_database_update
	fdo-mime_mime_database_update
}

pkg_postrm() {
	fdo-mime_desktop_database_update
	fdo-mime_mime_database_update
}
