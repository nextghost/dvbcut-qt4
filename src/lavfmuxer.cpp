/*  dvbcut
    Copyright (c) 2005 Sven Over <svenover@svenover.de>
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* $Id$ */

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/opt.h>
}
#include <cstring>
#include <utility>
#include <list>
#include "avframe.h"
#include "streamhandle.h"
#include "lavfmuxer.h"

#include <stdio.h>

#if LIBAVCODEC_VERSION_INT >= ((53<<16)+(0<<8)+0)
	#define guess_format av_guess_format
	#define av_alloc_format_context avformat_alloc_context
	#define CODEC_TYPE_AUDIO AVMEDIA_TYPE_AUDIO
#endif

lavfmuxer::lavfmuxer(const char *format, uint32_t audiostreammask, mpgfile &mpg, const char *filename)
    : muxer(), avfc(0), fileopened(false)
  {
  fmt = guess_format(format, NULL, NULL);
  if (!fmt) {
    return;
    }

#if LIBAVFORMAT_VERSION_INT >= ((53<<16)+(0<<8)+0)
	int ret = avformat_alloc_output_context2(&avfc, fmt, format, filename);

	if (ret < 0) {
		return;
	}

	avfc->max_delay = (int)(.7 * AV_TIME_BASE);
	av_opt_set_int(avfc, "preload", (int)(.5 * AV_TIME_BASE), AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(avfc, "muxrate", 10080000, AV_OPT_SEARCH_CHILDREN);
#else
  avfc=av_alloc_format_context();
  if (!avfc)
    return;

  avfc->preload= (int)(.5*AV_TIME_BASE);
  avfc->max_delay= (int)(.7*AV_TIME_BASE);
  avfc->mux_rate=10080000;

  avfc->oformat=fmt;
  strncpy(avfc->filename, filename, sizeof(avfc->filename));
#endif

  int id=0;

  st[VIDEOSTREAM].stream_index=id;
#if LIBAVFORMAT_VERSION_INT >= ((53<<16)+(0<<8)+0)
	AVStream *s = st[VIDEOSTREAM].avstr = avformat_new_stream(avfc, NULL);
	s->id = id++;
#else
  AVStream *s=st[VIDEOSTREAM].avstr=av_new_stream(avfc,id++);
#endif
  strpres[VIDEOSTREAM]=true;
  av_free(s->codec);
  mpg.setvideoencodingparameters();
  s->codec=mpg.getavcc(VIDEOSTREAM);
  s->codec->rc_buffer_size = 224*1024*8;
#if LIBAVFORMAT_VERSION_INT >= ((52<<16)+(21<<8)+0)
  s->sample_aspect_ratio = s->codec->sample_aspect_ratio;
#endif

  for (int i=0;i<mpg.getaudiostreams();++i)
    if (audiostreammask & (1u<<i)) {
      int astr=audiostream(i);
      st[astr].stream_index=id;
#if LIBAVFORMAT_VERSION_INT >= ((53<<16)+(0<<8)+0)
		s = st[astr].avstr = avformat_new_stream(avfc, NULL);
		s->id = id++;
#else
      s=st[astr].avstr=av_new_stream(avfc,id++);
#endif
      strpres[astr]=true;
      if (s->codec)
        av_free(s->codec);
#if LIBAVCODEC_VERSION_INT >= ((53<<16)+(0<<8)+0)
		s->codec = avcodec_alloc_context3(NULL);
#else
      s->codec = avcodec_alloc_context();
      avcodec_get_context_defaults(s->codec);
#endif
      s->codec->codec_type=CODEC_TYPE_AUDIO;
      s->codec->codec_id = (mpg.getstreamtype(astr)==streamtype::ac3audio) ?
	CODEC_ID_AC3 : CODEC_ID_MP2;
      s->codec->rc_buffer_size = 224*1024*8;

      // Must read some packets to get codec parameters
      streamhandle sh(mpg.getinitialoffset());
      streamdata *sd=sh.newstream(astr,mpg.getstreamtype(astr),mpg.istransportstream());

      while (sh.fileposition < mpg.getinitialoffset()+(4<<20)) {
	if (mpg.streamreader(sh)<=0)
	  break;

	if (sd->getitemlistsize() > 1) {
#if LIBAVCODEC_VERSION_INT >= ((53<<16)+(0<<8)+0)
	  if (!avcodec_open2(s->codec, avcodec_find_decoder(s->codec->codec_id), NULL)) {
#else
	  if (!avcodec_open(s->codec, avcodec_find_decoder(s->codec->codec_id))) {
#endif
	    int16_t samples[AVCODEC_MAX_AUDIO_FRAME_SIZE/sizeof(int16_t)];
	    int frame_size=sizeof(samples);
	    //fprintf(stderr, "** decode audio size=%d\n", sd->inbytes());
#if LIBAVCODEC_VERSION_INT >= ((53<<16)+(0<<8)+0)
		AVPacket packet;
		AVFrame frame;
		av_init_packet(&packet);
		packet.data = (uint8_t*)sd->getdata();
		packet.size = sd->inbytes();
		avcodec_decode_audio4(s->codec, &frame, &frame_size, &packet);
#elif LIBAVCODEC_VERSION_INT >= ((52<<16)+(0<<8)+0)
	    avcodec_decode_audio2
	      (s->codec,samples,&frame_size,
	       (uint8_t*) sd->getdata(),sd->inbytes());
#else
	    avcodec_decode_audio
	      (s->codec,samples,&frame_size,
	       (uint8_t*) sd->getdata(),sd->inbytes());
#endif
	    avcodec_close(s->codec);
	  }
	  break;
	}
      }
    }

#if LIBAVFORMAT_VERSION_INT >= ((53<<16)+(0<<8)+0)
	if (!(fmt->flags & AVFMT_NOFILE) && (avio_open(&avfc->pb, filename, AVIO_FLAG_WRITE) < 0)) {
		av_free(avfc);
		avfc = 0;
		return;
	}

	avfc->max_delay= (int)(.7*AV_TIME_BASE);
	av_opt_set_int(avfc, "preload", (int)(.5 * AV_TIME_BASE), AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(avfc, "muxrate", 10080000, AV_OPT_SEARCH_CHILDREN);

	av_dump_format(avfc, 0, filename, 1);
	avformat_write_header(avfc, NULL);
#else
  if ((av_set_parameters(avfc, NULL) < 0) || (!(fmt->flags & AVFMT_NOFILE)&&(url_fopen(&avfc->pb, filename, URL_WRONLY) < 0))) {
    av_free(avfc);
    avfc=0;
    return;
    }
  avfc->preload= (int)(.5*AV_TIME_BASE);
  avfc->max_delay= (int)(.7*AV_TIME_BASE);
  avfc->mux_rate=10080000;


  dump_format(avfc, 0, filename, 1);
  av_write_header(avfc);
#endif
  fileopened=true;
  }

lavfmuxer::~lavfmuxer()
  {
  if (avfc) {
    if (fileopened) {
      av_write_trailer(avfc);
      if (!(fmt->flags & AVFMT_NOFILE))
#if LIBAVFORMAT_VERSION_INT >= ((53<<16)+(0<<8)+0)
        avio_close(avfc->pb);
#elif LIBAVFORMAT_VERSION_INT >= ((52<<16)+(0<<8)+0)
        url_fclose(avfc->pb);
#else
        url_fclose(&avfc->pb);
#endif
      }

    av_free(avfc);
    }
  }

