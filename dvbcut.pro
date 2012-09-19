TEMPLATE = subdirs
SUBDIRS = src

unix {
	isEmpty(PREFIX) {
		PREFIX = /usr/local
	}

	isEmpty(MANPATH) {
		MANPATH = $$PREFIX/man
	}

	isEmpty(DATADIR) {
		DATADIR = "$$PREFIX/share/dvbcut"
	}

	system(echo "PREFIX = \\'$$PREFIX\\'" >.qmake.cache)
	system(echo "DATADIR = \\'$$DATADIR\\'" >>.qmake.cache)

	mime.files = dvbcut.xml
	mime.path = /usr/share/mime/packages

	icons.files = dvbcut.svg
	icons.path = "$$DATADIR/icons"

	manpage.files = dvbcut.1
	manpage.path = "$$MANPATH/man1"

	INSTALLS += icons mime manpage
}
