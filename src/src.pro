TARGET = dvbcut
SOURCES = avframe.cpp buffer.cpp differenceimageprovider.cpp dvbcut.cpp exception.cpp gettext.cpp imageprovider.cpp index.cpp logoutput.cpp main.cpp mpgfile.cpp playaudio.cpp progressstatusbar.cpp psfile.cpp pts.cpp settings.cpp streamdata.cpp tsfile.cpp
HEADERS = avframe.h buffer.h busyindicator.h defines.h differenceimageprovider.h dvbcut.h exception.h gettext.h imageprovider.h index.h logoutput.h mpgfile.h muxer.h playaudio.h port.h progressstatusbar.h psfile.h pts.h settings.h stream.h streamdata.h streamhandle.h tsfile.h types.h ../ffmpeg.src/libavformat/avformat.h
FORMS = dvbcutbase.ui
RESOURCES = dvbcutbase.qrc
CONFIG += link_pkgconfig
QMAKE_CXXFLAGS += -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -D_FILE_OFFSET_BITS=64
DESTDIR = ../bin
QT += xml

system(pkg-config --exists libavcodec) {
#system(pkg-config --exists foo) {
	message(Building with external FFMPEG)
	PKGCONFIG += libavformat libavcodec

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
