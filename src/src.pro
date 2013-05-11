TARGET = dvbcut
SOURCES = avframe.cpp buffer.cpp differenceimageprovider.cpp dvbcut.cpp eventlistmodel.cpp exception.cpp exportdialog.cpp imageprovider.cpp index.cpp lavfmuxer.cpp logoutput.cpp main.cpp mpegmuxer.cpp mpgfile.cpp playaudio.cpp progressstatusbar.cpp progresswindow.cpp psfile.cpp pts.cpp settings.cpp streamdata.cpp tsfile.cpp
HEADERS = avframe.h buffer.h busyindicator.h defines.h differenceimageprovider.h dvbcut.h eventlistmodel.h exception.h exportdialog.h imageprovider.h index.h lavfmuxer.h logoutput.h mpegmuxer.h mpgfile.h muxer.h playaudio.h port.h progressstatusbar.h progresswindow.h psfile.h pts.h settings.h stream.h streamdata.h streamhandle.h tsfile.h types.h ../ffmpeg.src/libavformat/avformat.h
FORMS = dvbcutbase.ui exportdialogbase.ui mplayererrorbase.ui progresswindowbase.ui
RESOURCES = dvbcutbase.qrc
TRANSLATIONS = dvbcut_cs.ts dvbcut_de.ts dvbcut.ts
CONFIG += link_pkgconfig
QMAKE_CXXFLAGS += -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -D_FILE_OFFSET_BITS=64
DESTDIR = ../bin
QT += xml

system(pkg-config --exists libavcodec) {
	message(Building with external FFMPEG)
	PKGCONFIG += libavformat libavcodec libavutil

	system(pkg-config --exists libswscale) {
		message(Building with libswscale support)
		QMAKE_CXXFLAGS += -DHAVE_LIB_SWSCALE
		PKGCONFIG += libswscale
	}
} else {
	message(Building with internal FFMPEG)
	ffmpeg.target = ../ffmpeg/lib/libavcodec.a
	ffmpeg_confopts = --enable-gpl \
		--disable-decoders \
		--enable-memalign-hack \
		--disable-encoders \
		--disable-ffplay \
		--disable-ffserver \
		--disable-vhook \
		--disable-zlib \
		--disable-network \
		--disable-dv1394 \
		--disable-bktr \
		--disable-v4l \
		--disable-audio-beos \
		--disable-audio-oss \
		--enable-codec=mpeg2encoder \
		--enable-codec=mp2_encoder \
		--enable-codec=ac3_decoder \
		--enable-codec=ac3_encoder \
		--enable-a52 \
		--disable-mmx \
		--disable-debug
	ffmpeg.commands = cd ../ffmpeg.src; ./configure --prefix=`cd .. && pwd`/ffmpeg $$ffmpeg_confopts && make installlib

	INCLUDEPATH += ../ffmpeg.src ../ffmpeg.src/libavformat ../ffmpeg.src/libavcodec ../ffmpeg.src/libavutil
	LIBS += -L../ffmpeg/lib -lavformat -lavcodec -lavutil
	PRE_TARGETDEPS += $$ffmpeg.target
	QMAKE_CXXFLAGS += -DUSE_FFMPEG_INTERNAL
	QMAKE_EXTRA_TARGETS += ffmpeg
}

system(pkg-config --exists ao) {
	message(Building with libao support)
	QMAKE_CXXFLAGS += -DHAVE_LIB_AO
	PKGCONFIG += ao
}

system(pkg-config --exists mad) {
	message(Building with libmad support)
	QMAKE_CXXFLAGS += -DHAVE_LIB_MAD
	PKGCONFIG += mad
}


verinfo.target = version.h
verinfo.depends = $$SOURCES $$HEADERS
verinfo.commands = ../setversion.sh $$SOURCES $$HEADERS

QMAKE_EXTRA_TARGETS += verinfo

qtPrepareTool(LRELEASE, lrelease)

l10n.commands = $$LRELEASE ${QMAKE_FILE_IN} -qm ${QMAKE_FILE_OUT}
l10n.input = TRANSLATIONS
l10n.output = ${QMAKE_FILE_BASE}.qm
l10n.CONFIG += no_link target_predeps
l10n.variable_out = l10ninst.files
QMAKE_EXTRA_COMPILERS += l10n

unix {
	QMAKE_CXXFLAGS += -DDVBCUT_DATADIR=\"\\\"$$DATADIR\\\"\"

	DESKTOP_IN = ../dvbcut.desktop.in
	desktop.commands = sed -e \'s,@prefix@,$$PREFIX,\' ${QMAKE_FILE_IN} >${QMAKE_FILE_OUT}
	desktop.input = DESKTOP_IN
	desktop.output = dvbcut.desktop
	desktop.CONFIG += no_link target_predeps

	l10ninst.path = "$$DATADIR/translations"
	l10ninst.CONFIG += no_check_exist

	bininst.files = "$$DESTDIR/$$TARGET"
	bininst.path = "$$PREFIX/bin"
	bininst.CONFIG += no_check_exist

	desktopinst.files = dvbcut.desktop
	desktopinst.path = "/usr/share/applications"
	desktopinst.CONFIG += no_check_exist

        helpinst.files = dvbcut.html dvbcut_cs.html dvbcut_de.html
	helpinst.path = "$$DATADIR/help"

	QMAKE_EXTRA_COMPILERS += desktop
	INSTALLS += bininst l10ninst desktopinst helpinst
}
