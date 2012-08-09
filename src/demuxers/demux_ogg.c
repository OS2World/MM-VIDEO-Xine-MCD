/*
 * Copyright (C) 2000-2003 the xine project
 * 
 * This file is part of xine, a free video player.
 * 
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 */

/*
 * $Id: demux_ogg.c,v 1.109 2003/10/06 15:46:20 mroi Exp $
 *
 * demultiplexer for ogg streams
 *
 */
/* 2003.02.09 (dilb) update of the handling for audio/video infos for strongarm cpus. */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#ifdef HAVE_SPEEX
#include <speex.h>
#include <speex_header.h>
#include <speex_stereo.h>
#include <speex_callbacks.h>
#endif

#ifdef HAVE_THEORA
#include <theora/theora.h>
#endif

/********** logging **********/
#define LOG_MODULE "demux_ogg"
/* #define LOG_VERBOSE */
/* #define LOG */

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"

#define CHUNKSIZE                8500
#define PACKET_TYPE_HEADER       0x01
#define PACKET_TYPE_COMMENT      0x03
#define PACKET_TYPE_CODEBOOK     0x05
#define PACKET_TYPE_BITS	 0x07
#define PACKET_LEN_BITS01        0xc0
#define PACKET_LEN_BITS2         0x02
#define PACKET_IS_SYNCPOINT      0x08

#define MAX_STREAMS              16

#define PTS_AUDIO                0
#define PTS_VIDEO                1

#define WRAP_THRESHOLD           900000

#define SUB_BUFSIZE 1024

typedef struct chapter_entry_s {
  int64_t           start_pts;
  char              *name;
} chapter_entry_t;

typedef struct chapter_info_s {
  int                current_chapter;
  int                max_chapter;
  chapter_entry_t   *entries;
} chapter_info_t;

typedef struct demux_ogg_s {
  demux_plugin_t        demux_plugin;

  xine_stream_t        *stream;
  fifo_buffer_t        *audio_fifo;
  fifo_buffer_t        *video_fifo;
  input_plugin_t       *input;
  int                   status;

#ifdef HAVE_THEORA
  theora_info           t_info;
  theora_comment        t_comment;
#endif

  int                   frame_duration;

  ogg_sync_state        oy;
  ogg_page              og;

  ogg_stream_state      oss[MAX_STREAMS];
  uint32_t              buf_types[MAX_STREAMS];
  int                   preview_buffers[MAX_STREAMS];
  int64_t               header_granulepos[MAX_STREAMS];
  int64_t               factor[MAX_STREAMS];
  int64_t               quotient[MAX_STREAMS];
  int                   resync[MAX_STREAMS];
  char                  *language[MAX_STREAMS];

  int64_t               start_pts;

  int                   num_streams;

  int                   num_audio_streams;
  int                   num_video_streams;
  int                   num_spu_streams;

  off_t                 avg_bitrate;

  int64_t               last_pts[2];
  int                   send_newpts;
  int                   buf_flag_seek;
  int                   keyframe_needed;
  int                   ignore_keyframes;
  int                   time_length;

  char                 *title;
  chapter_info_t       *chapter_info;
} demux_ogg_t ;

typedef struct {
  demux_class_t     demux_class;
} demux_ogg_class_t;


#ifdef HAVE_THEORA
static int intlog(int num) {
  int ret=0;

  while(num>0){
    num=num/2;
    ret=ret+1;
  }
  return(ret);
}
#endif

static int get_stream (demux_ogg_t *this, int serno) {
  /*finds the stream_num, which belongs to a ogg serno*/
  int i;

  for (i = 0; i<this->num_streams; i++) {
    if (this->oss[i].serialno == serno) {
      return i;
    }
  }
  return -1;
}

static int64_t get_pts (demux_ogg_t *this, int stream_num , int64_t granulepos ) {
  /*calculates an pts from an granulepos*/
  if (granulepos<0) {
    if ( this->header_granulepos[stream_num]>=0 ) {
      /*return the smallest valid pts*/
      return 1;
    } else
      return 0;

#ifdef HAVE_THEORA
  } else  if (this->buf_types[stream_num]==BUF_VIDEO_THEORA) {
    int64_t iframe,pframe;
    int keyframe_granule_shift;
    keyframe_granule_shift=intlog(this->t_info.keyframe_frequency_force-1);
    iframe=granulepos>>keyframe_granule_shift;
    pframe=granulepos-(iframe<<keyframe_granule_shift);
    return 1+((iframe + pframe)*this->frame_duration);
#endif

  } else if (this->quotient[stream_num])
    return 1+(granulepos*this->factor[stream_num]/this->quotient[stream_num]);
  else
    return 0;
}

static int read_ogg_packet (demux_ogg_t *this) {
  char *buffer;
  long bytes;
  while (ogg_sync_pageout(&this->oy,&this->og)!=1) {
    buffer = ogg_sync_buffer(&this->oy, CHUNKSIZE);
    bytes  = this->input->read(this->input, buffer, CHUNKSIZE);
    ogg_sync_wrote(&this->oy, bytes);
    if (bytes < CHUNKSIZE/2) {
      return 0;
    }
  }
  return 1;
}

static void get_stream_length (demux_ogg_t *this) {
  /*determine the streamlenght and set this->time_length accordingly.
    ATTENTION:current_pos and oggbuffers will be destroyed by this function,
    there will be no way to continue playback uninterrupted.

    You have to seek afterwards, because after get_stream_length, the
    current_position is at the end of the file */

  int filelength;
  int done=0;
  int stream_num;

  this->time_length=-1;

  if (this->input->get_capabilities(this->input) & INPUT_CAP_SEEKABLE) {
    filelength=this->input->get_length(this->input);
    
    if (filelength!=-1) {
      if (filelength>70000) {
	this->demux_plugin.seek((demux_plugin_t *)this, (off_t) filelength-65536 ,0);
      }
      done=0;
      while (!done) {
	if (!read_ogg_packet (this)) {
	  if (this->time_length) {
	    this->stream->stream_info[XINE_STREAM_INFO_BITRATE]
	      = ((int64_t) 8000*filelength)/this->time_length;
	    /*this is a fine place to compute avg_bitrate*/
	    this->avg_bitrate= 8000*filelength/this->time_length;
	  }
	  return;
	}
	stream_num=get_stream(this, ogg_page_serialno (&this->og) );
	if (stream_num!=-1) {
	  if (this->time_length < (get_pts(this, stream_num, ogg_page_granulepos(&this->og) / 90)))
	    this->time_length = get_pts(this, stream_num, ogg_page_granulepos(&this->og)) / 90;
	}
      }
    }  
  }
}

#ifdef HAVE_THEORA
static void send_ogg_packet (demux_ogg_t *this,
			     fifo_buffer_t *fifo,
			     ogg_packet *op,
			     int64_t pts,
			     uint32_t decoder_flags,
			     int stream_num) {
  /*this little function is used to send an entire ogg-packet through
    xine buffers to the appropiate decoder, where recieve_ogg_packet should be called to collect the
  buffers and reassemble them to an ogg packet*/

  buf_element_t *buf;

  int done=0,todo=op->bytes;
  int op_size = sizeof(ogg_packet);

  /* nasty hack to pack op as well as (vorbis/theora) content
     in one xine buffer */

  buf = fifo->buffer_pool_alloc (fifo);
  memcpy (buf->content, op, op_size);

  if ( buf->max_size > op_size + todo ) {
    memcpy (buf->content + op_size , op->packet, todo);
    done=todo;
    buf->decoder_flags = BUF_FLAG_FRAME_START | BUF_FLAG_FRAME_END | decoder_flags;
    buf->size = op_size + done;
  } else {
    memcpy (buf->content + op_size , op->packet, buf->max_size - op_size );
    done=done+ buf->max_size - op_size;
    buf->decoder_flags = BUF_FLAG_FRAME_START | decoder_flags;
    buf->size = buf->max_size;
  }

  buf->pts  = pts;
  buf->extra_info->input_pos  = this->input->get_current_pos (this->input);
  buf->extra_info->input_time = buf->pts / 90 ;
  buf->type       = this->buf_types[stream_num] ;

  this->video_fifo->put (this->video_fifo, buf);

  while (done<todo) {
    buf = fifo->buffer_pool_alloc (fifo);
    if (done+buf->max_size < todo) {
      memcpy (buf->content, op->packet+done, buf->max_size);
      buf->size = buf->max_size;
      done=done+buf->max_size;
      buf->decoder_flags = decoder_flags;
    } else {
      memcpy (buf->content, op->packet+done, todo-done);
      buf->size = todo-done;
      done=todo;
      buf->decoder_flags = BUF_FLAG_FRAME_END | decoder_flags;
    }

    buf->pts = pts;
    buf->extra_info->input_pos  = this->input->get_current_pos (this->input);
    buf->extra_info->input_time = buf->pts / 90 ;
    buf->type       = this->buf_types[stream_num] ;

    fifo->put (fifo, buf);
  }
}
#endif

/* redefine abs as macro to handle 64-bit diffs.
   i guess llabs may not be available everywhere */
#define abs(x) ( ((x)<0) ? -(x) : (x) )

static void check_newpts (demux_ogg_t *this, int64_t pts, int video, int preview) {
  int64_t diff;

  lprintf ("new pts %lld found in stream\n",pts);

  diff = pts - this->last_pts[video];

  if (!preview && (pts>=0) &&
      (this->send_newpts || (this->last_pts[video] && abs(diff)>WRAP_THRESHOLD) ) ) {

    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
             "diff=%lld (pts=%lld, last_pts=%lld)\n", diff, pts, this->last_pts[video]);

    if (this->buf_flag_seek) {
      xine_demux_control_newpts(this->stream, pts, BUF_FLAG_SEEK);
      this->buf_flag_seek = 0;
    } else {
      xine_demux_control_newpts(this->stream, pts, 0);
    }
    this->send_newpts = 0;
    this->last_pts[1-video] = 0;
  }

  if (!preview && (pts>=0) )
    this->last_pts[video] = pts;

  /* use pts for bitrate measurement */

  /*compute avg_bitrate if time_length isn't set*/
  if ((pts>180000) && !(this->time_length)) {
    this->avg_bitrate = this->input->get_current_pos (this->input) * 8 * 90000/ pts;

    if (this->avg_bitrate<1)
      this->avg_bitrate = 1;

  }
}

/*
 * utility function to read a LANGUAGE= line from the user_comments,
 * to label audio and spu streams
 */
static void read_language_comment (demux_ogg_t * this, ogg_packet *op, int stream_num) {
  char           **ptr;
  char           *comment;
  vorbis_comment vc;
  vorbis_info    vi;

  vorbis_comment_init(&vc);
  vorbis_info_init(&vi);

  /* this is necessary to make libvorbis accept this vorbis_info*/
  vi.rate=1;

  if ( vorbis_synthesis_headerin(&vi, &vc, op) >= 0) {
    ptr=vc.user_comments;
    while(*ptr) {
      comment=*ptr;
      if ( !strncasecmp ("LANGUAGE=", comment, 9) ) {
        this->language[stream_num]=strdup (comment + strlen ("LANGUAGE=") );
      }
      ++ptr;
    }
  }
  vorbis_comment_clear(&vc);
  vorbis_info_clear(&vi);
}

/*
 * utility function to pack one ogg_packet into a xine
 * buffer, fill out all needed fields
 * and send it to the right fifo
 */

static void send_ogg_buf (demux_ogg_t *this,
			  ogg_packet  *op,
			  int          stream_num,
			  uint32_t     decoder_flags) {

  int hdrlen;

  hdrlen = (*op->packet & PACKET_LEN_BITS01) >> 6;
  hdrlen |= (*op->packet & PACKET_LEN_BITS2) << 1;

  if ( this->audio_fifo
       && (this->buf_types[stream_num] & 0xFF000000) == BUF_AUDIO_BASE) {
    buf_element_t *buf;

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);

    if (op->packet[0] == PACKET_TYPE_COMMENT ) {
      read_language_comment(this, op, stream_num);
    }

    if ((this->buf_types[stream_num] & 0xFFFF0000) == BUF_AUDIO_VORBIS) {
      int op_size = sizeof(ogg_packet);
      ogg_packet *og_ghost;
      op_size += (4 - (op_size % 4));

      /* nasty hack to pack op as well as (vorbis) content
	 in one xine buffer */
      memcpy (buf->content + op_size, op->packet, op->bytes);
      memcpy (buf->content, op, op_size);
      og_ghost = (ogg_packet *) buf->content;
      og_ghost->packet = buf->content + op_size;
      
      buf->size   = op->bytes;
    } else if ((this->buf_types[stream_num] & 0xFFFF0000) == BUF_AUDIO_SPEEX) {
      memcpy (buf->content, op->packet, op->bytes);
      buf->size   = op->bytes;      
    } else {
      memcpy (buf->content, op->packet+1+hdrlen, op->bytes-1-hdrlen);
      buf->size   = op->bytes-1-hdrlen;
    }
    lprintf ("audio buf_size %d\n", buf->size);

    if ((op->granulepos!=-1) || (this->header_granulepos[stream_num]!=-1)) {
      buf->pts = get_pts(this, stream_num, op->granulepos );
      check_newpts( this, buf->pts, PTS_AUDIO, decoder_flags );
    } else
      buf->pts = 0;

    lprintf ("audiostream %d op-gpos %lld hdr-gpos %lld pts %lld \n",
             stream_num,
             op->granulepos,
             this->header_granulepos[stream_num],
             buf->pts);

    buf->extra_info->input_pos     = this->input->get_current_pos (this->input);
    buf->extra_info->input_time    = buf->pts / 90;
    buf->type          = this->buf_types[stream_num] ;
    buf->decoder_flags = decoder_flags;

    this->audio_fifo->put (this->audio_fifo, buf);

#ifdef HAVE_THEORA
  } else if ((this->buf_types[stream_num] & 0xFFFF0000) == BUF_VIDEO_THEORA) {

    int64_t pts;
    theora_info t_info;
    theora_comment t_comment;

    theora_info_init (&t_info);
    theora_comment_init (&t_comment);

    /*Lets see if this is an Header*/
    if ((theora_decode_header(&t_info, &t_comment, op))>=0) {
      decoder_flags=decoder_flags|BUF_FLAG_HEADER;
      lprintf ("found an header\n");
    }

    if ((op->granulepos!=-1) || (this->header_granulepos[stream_num]!=-1)) {
      pts = get_pts(this, stream_num, op->granulepos );
      check_newpts( this, pts, PTS_VIDEO, decoder_flags );
    } else
      pts = 0;

    lprintf ("theorastream %d op-gpos %lld hdr-gpos %lld pts %lld \n",
             stream_num,
             op->granulepos,
             this->header_granulepos[stream_num],
             pts);

    send_ogg_packet (this, this->video_fifo, op, pts, decoder_flags, stream_num);

    theora_comment_clear (&t_comment);
    theora_info_clear (&t_info);
#endif

  } else if ((this->buf_types[stream_num] & 0xFF000000) == BUF_VIDEO_BASE) {

    buf_element_t *buf;
    int todo, done;

    lprintf ("video buffer, type=%08x\n", this->buf_types[stream_num]);

    if (op->packet[0] == PACKET_TYPE_COMMENT ) {
      char           **ptr;
      char           *comment;
      vorbis_comment vc;
      vorbis_info    vi;

      vorbis_comment_init(&vc);
      vorbis_info_init(&vi);

      /* this is necessary to make libvorbis accept this vorbis_info*/
      vi.rate=1;

      if ( vorbis_synthesis_headerin(&vi, &vc, op) >= 0) {
        char *chapter_time = 0;
        char *chapter_name = 0;
        int   chapter_no = 0;
        ptr=vc.user_comments;
        while(*ptr) {
          comment=*ptr;
          if ( !strncasecmp ("TITLE=", comment,6) ) {
            this->title=strdup (comment + strlen ("TITLE=") );
            this->stream->meta_info[XINE_META_INFO_TITLE] = strdup (this->title);
          }
          if ( !chapter_time && strlen(comment) == 22 &&
              !strncasecmp ("CHAPTER" , comment, 7) &&
              isdigit(*(comment+7)) && isdigit(*(comment+8)) &&
              (*(comment+9) == '=')) {

            chapter_time = strdup(comment+10);
            chapter_no   = strtol(comment+7, NULL, 10);
          }
          if ( !chapter_name && !strncasecmp("CHAPTER", comment, 7) &&
               isdigit(*(comment+7)) && isdigit(*(comment+8)) &&
               !strncasecmp ("NAME=", comment+9, 5)) {

            if (strtol(comment+7,NULL,10) == chapter_no) {
              chapter_name = strdup(comment+14);
            }
          }
          if (chapter_time && chapter_name && chapter_no){
            int hour, min, sec, msec;

            lprintf("create chapter entry: no=%d name=%s time=%s\n", chapter_no, chapter_name, chapter_time);
            hour= strtol(chapter_time, NULL, 10);
            min = strtol(chapter_time+3, NULL, 10);
            sec = strtol(chapter_time+6, NULL, 10);
            msec = strtol(chapter_time+9, NULL, 10);
            lprintf("time: %d %d %d %d\n", hour, min,sec,msec);

            if (!this->chapter_info) {
              this->chapter_info = (chapter_info_t *)xine_xmalloc(sizeof(chapter_info_t));
              this->chapter_info->current_chapter = -1;
            }
            this->chapter_info->max_chapter = chapter_no;
            this->chapter_info->entries = realloc( this->chapter_info->entries, chapter_no*sizeof(chapter_entry_t));
            this->chapter_info->entries[chapter_no-1].name = chapter_name;
            this->chapter_info->entries[chapter_no-1].start_pts = (msec + (1000.0 * sec) + (60000.0 * min) + (3600000.0 * hour))*90;

            free (chapter_time);
            chapter_no = 0;
            chapter_time = chapter_name = 0;
          }
	  ++ptr;
	}
      }
      vorbis_comment_clear(&vc);
      vorbis_info_clear(&vi);
    }

    todo = op->bytes;
    done = 1+hdrlen;
    while (done<todo) {

      buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

      if ( (todo-done)>(buf->max_size-1)) {
	buf->size  = buf->max_size-1;
	buf->decoder_flags = decoder_flags;
      } else {
	buf->size = todo-done;
	buf->decoder_flags = BUF_FLAG_FRAME_END | decoder_flags;
      }

      /*
	lprintf ("done %d todo %d doing %d\n", done, todo, buf->size);
      */
      memcpy (buf->content, op->packet+done, buf->size);

      if ((op->granulepos!=-1) || (this->header_granulepos[stream_num]!=-1)) {
	buf->pts = get_pts(this, stream_num, op->granulepos );
	check_newpts( this, buf->pts, PTS_VIDEO, decoder_flags );
      } else
	buf->pts = 0;

      buf->extra_info->input_pos  = this->input->get_current_pos (this->input);
      buf->extra_info->input_time = buf->pts / 90 ;
      buf->type       = this->buf_types[stream_num] ;

      done += buf->size;

      lprintf ("videostream %d op-gpos %lld hdr-gpos %lld pts %lld \n",
               stream_num,
               op->granulepos,
               this->header_granulepos[stream_num],
               buf->pts);

      this->video_fifo->put (this->video_fifo, buf);
    }

    if (this->chapter_info && op->granulepos != -1) {
      int chapter = 0;
      int64_t pts = get_pts(this, stream_num, op->granulepos );

      /*lprintf("CHP max=%d current=%d pts=%lld\n",
              this->chapter_info->max_chapter, this->chapter_info->current_chapter, pts);*/

      while (chapter < this->chapter_info->max_chapter &&
             this->chapter_info->entries[chapter].start_pts < pts) {
        chapter++;
      }
      chapter--;

      if (chapter != this->chapter_info->current_chapter){
        xine_event_t uevent;
        xine_ui_data_t data;
        int title_len;

        this->chapter_info->current_chapter = chapter;
        if (this->stream->meta_info[XINE_META_INFO_TITLE])
          free (this->stream->meta_info[XINE_META_INFO_TITLE]);
        if (chapter >= 0) {
          char t_title[256];

          sprintf(t_title, "%s / %s", this->title, this->chapter_info->entries[chapter].name);
          this->stream->meta_info[XINE_META_INFO_TITLE] = strdup(t_title);
        } else {
          this->stream->meta_info[XINE_META_INFO_TITLE] = strdup (this->title);
        }
        lprintf("new TITLE: %s\n", this->stream->meta_info[XINE_META_INFO_TITLE]);

        uevent.type = XINE_EVENT_UI_SET_TITLE;
        uevent.stream = this->stream;
        uevent.data = &data;
        uevent.data_length = sizeof(data);
        title_len = strlen(this->stream->meta_info[XINE_META_INFO_TITLE]) + 1;
        memcpy(data.str, this->stream->meta_info[XINE_META_INFO_TITLE], title_len);
        data.str_len = title_len;
        xine_event_send(this->stream, &uevent);
      }
    }
  } else if ((this->buf_types[stream_num] & 0xFF000000) == BUF_SPU_BASE) {

    buf_element_t *buf;
    int i;
    char *subtitle,*str;
    int lenbytes;
    int start,end;
    uint32_t *val;

    for (i = 0, lenbytes = 0; i < hdrlen; i++) {
      lenbytes = lenbytes << 8;
      lenbytes += *((unsigned char *) op->packet + hdrlen - i);
    }

    if (op->packet[0] == PACKET_TYPE_HEADER ) {
      lprintf ("Textstream-header-packet\n");
    } else if (op->packet[0] == PACKET_TYPE_COMMENT ) {
      lprintf ("Textstream-comment-packet\n");
      read_language_comment(this, op, stream_num);
    } else {
      subtitle = (char *)&op->packet[hdrlen + 1];

      if ((strlen(subtitle) > 1) || (*subtitle != ' ')) {

	start = op->granulepos;
	end = start+lenbytes;
	lprintf ("subtitlestream %d: %d -> %d :%s\n",stream_num,start,end,subtitle);
	buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);

	buf->type = this->buf_types[stream_num];
	buf->pts = 0;

	val = (uint32_t * )buf->content;

	*val++ = start;
	*val++ = end;
	str = (char *)val;

	memcpy (str, subtitle, 1+strlen(subtitle));

	this->video_fifo->put (this->video_fifo, buf);
      }
    }
  }
}

/*
 * interpret stream start packages, send headers
 */
static void demux_ogg_send_header (demux_ogg_t *this) {

  int          stream_num = -1;
  int          cur_serno;
  int          done = 0;
  ogg_packet   op;
  xine_event_t ui_event;

  lprintf ("detecting stream types...\n");

  this->ignore_keyframes = 0;

  while (!done) {
    if (!read_ogg_packet(this)) {
      this->status = DEMUX_FINISHED;
      return;
    }
    /* now we've got at least one new page */

    cur_serno = ogg_page_serialno (&this->og);

    if (ogg_page_bos(&this->og)) {
      lprintf ("beginning of stream\n");
      lprintf ("serial number %d\n", cur_serno);

      ogg_stream_init(&this->oss[this->num_streams], cur_serno);
      stream_num = this->num_streams;
      this->buf_types[stream_num] = 0;
      this->header_granulepos[stream_num] = -1;
      this->num_streams++;
    } else {
      stream_num = get_stream(this, cur_serno);
      if (stream_num == -1) {
	xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                 "help, stream with no beginning!\n");
	abort();
      }
    }

    ogg_stream_pagein(&this->oss[stream_num], &this->og);

    while (ogg_stream_packetout(&this->oss[stream_num], &op) == 1) {

      if (!this->buf_types[stream_num]) {
	/* detect buftype */
	if (!strncmp (&op.packet[1], "vorbis", 6)) {

	  vorbis_info       vi;
	  vorbis_comment    vc;

	  this->buf_types[stream_num] = BUF_AUDIO_VORBIS
	    +this->num_audio_streams++;

	  this->preview_buffers[stream_num] = 3;

	  vorbis_info_init(&vi);
	  vorbis_comment_init(&vc);
	  if (vorbis_synthesis_headerin(&vi, &vc, &op) >= 0) {

	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE]
	      = vi.bitrate_nominal;
	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE]
	      = vi.rate;

	    this->factor[stream_num] = 90000;
	    this->quotient[stream_num] = vi.rate;

	    if (vi.bitrate_nominal<1)
	      this->avg_bitrate += 100000; /* assume 100 kbit */
	    else
	      this->avg_bitrate += vi.bitrate_nominal;

	  } else {
	    this->factor[stream_num] = 900;
	    this->quotient[stream_num] = 441;

	    this->preview_buffers[stream_num] = 0;
	    xine_log (this->stream->xine, XINE_LOG_MSG,
		      _("ogg: vorbis audio track indicated but no vorbis stream header found.\n"));
	  }
	  vorbis_comment_clear(&vc);
	  vorbis_info_clear(&vi);
	} else if (!strncmp (&op.packet[0], "Speex", 5)) {

#ifdef HAVE_SPEEX
	  void * st;
	  SpeexMode * mode;
	  SpeexHeader * header;

	  this->buf_types[stream_num] = BUF_AUDIO_SPEEX
	    +this->num_audio_streams++;

	  this->preview_buffers[stream_num] = 1;

	  header = speex_packet_to_header (op.packet, op.bytes);

	  if (header) {
	    int bitrate;
	    mode = speex_mode_list[header->mode];

	    st = speex_decoder_init (mode);

	    speex_decoder_ctl (st, SPEEX_GET_BITRATE, &bitrate);

	    if (bitrate <= 1)
	      bitrate = 16000; /* assume 16 kbit */

	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE]
	      = bitrate;

	    this->factor[stream_num] = 90000;
	    this->quotient[stream_num] = header->rate;

	    this->avg_bitrate += bitrate;

	    lprintf ("detected Speex stream,\trate %d\tbitrate %d\n",
		     header->rate, bitrate);

	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE]
	      = header->rate;

	    this->preview_buffers[stream_num] += header->extra_headers;
	  }
#else
	  xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                   "Speex stream detected, unable to play\n");

	  this->buf_types[stream_num] = BUF_CONTROL_NOP;
#endif
	} else if (!strncmp (&op.packet[1], "video", 5)) {

	  buf_element_t    *buf;
	  xine_bmiheader    bih;
	  int               channel;

	  int16_t          locbits_per_sample;
	  uint32_t         locsubtype;
	  int32_t          locsize, locdefault_len, locbuffersize, locwidth, locheight;
	  int64_t          loctime_unit, locsamples_per_unit;

	  memcpy(&locsubtype, &op.packet[9], 4);
	  memcpy(&locsize, &op.packet[13], 4);
	  memcpy(&loctime_unit, &op.packet[17], 8);
	  memcpy(&locsamples_per_unit, &op.packet[25], 8);
	  memcpy(&locdefault_len, &op.packet[33], 4);
	  memcpy(&locbuffersize, &op.packet[37], 4);
	  memcpy(&locbits_per_sample, &op.packet[41], 2);
	  memcpy(&locwidth, &op.packet[45], 4);
	  memcpy(&locheight, &op.packet[49], 4);

          lprintf ("direct show filter created stream detected, hexdump:\n");
#ifdef LOG
	  xine_hexdump (op.packet, op.bytes);
#endif

	  channel = this->num_video_streams++;

	  this->buf_types[stream_num] = fourcc_to_buf_video (locsubtype);
	  if( !this->buf_types[stream_num] )
	    this->buf_types[stream_num] = BUF_VIDEO_UNKNOWN;
	  this->buf_types[stream_num] |= channel;
	  this->preview_buffers[stream_num] = 5; /* FIXME: don't know */

	  lprintf ("subtype          %.4s\n", &locsubtype);
	  lprintf ("time_unit        %lld\n", loctime_unit);
	  lprintf ("samples_per_unit %lld\n", locsamples_per_unit);
	  lprintf ("default_len      %d\n", locdefault_len);
	  lprintf ("buffersize       %d\n", locbuffersize);
	  lprintf ("bits_per_sample  %d\n", locbits_per_sample);
	  lprintf ("width            %d\n", locwidth);
	  lprintf ("height           %d\n", locheight);
	  lprintf ("buf_type         %08x\n",this->buf_types[stream_num]);

	  bih.biSize=sizeof(xine_bmiheader);
	  bih.biWidth = locwidth;
	  bih.biHeight= locheight;
	  bih.biPlanes= 0;
	  memcpy(&bih.biCompression, &locsubtype, 4);
	  bih.biBitCount= 0;
	  bih.biSizeImage=locwidth*locheight;
	  bih.biXPelsPerMeter=1;
	  bih.biYPelsPerMeter=1;
	  bih.biClrUsed=0;
	  bih.biClrImportant=0;

	  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
	  buf->decoder_flags = BUF_FLAG_HEADER;
	  this->frame_duration = loctime_unit * 9 / 1000;
	  this->factor[stream_num] = loctime_unit * 9;
	  this->quotient[stream_num] = 1000;
	  buf->decoder_info[1] = this->frame_duration;
	  memcpy (buf->content, &bih, sizeof (xine_bmiheader));
	  buf->size = sizeof (xine_bmiheader);
	  buf->type = this->buf_types[stream_num];

	  /*
	   * video metadata
	   */

	  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]
	    = locwidth;
	  this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT]
	    = locheight;
	  this->stream->stream_info[XINE_STREAM_INFO_FRAME_DURATION]
	    = this->frame_duration;

	  this->avg_bitrate += 500000; /* FIXME */

	  this->video_fifo->put (this->video_fifo, buf);

	} else if (!strncmp (&op.packet[1], "audio", 5)) {

	  if (this->audio_fifo) {
	    buf_element_t    *buf;
	    int               codec;
	    char              str[5];
	    int               channel;

	    int16_t          locbits_per_sample, locchannels, locblockalign;
	    uint32_t         locsubtype;
	    int32_t          locsize, locdefault_len, locbuffersize, locavgbytespersec;
	    int64_t          loctime_unit, locsamples_per_unit;

	    memcpy(&locsubtype, &op.packet[9], 4);
	    memcpy(&locsize, &op.packet[13], 4);
	    memcpy(&loctime_unit, &op.packet[17], 8);
	    memcpy(&locsamples_per_unit, &op.packet[25], 8);
	    memcpy(&locdefault_len, &op.packet[33], 4);
	    memcpy(&locbuffersize, &op.packet[37], 4);
	    memcpy(&locbits_per_sample, &op.packet[41], 2);
	    memcpy(&locchannels, &op.packet[45], 2);
	    memcpy(&locblockalign, &op.packet[47], 2);
	    memcpy(&locavgbytespersec, &op.packet[49], 4);

            lprintf ("direct show filter created audio stream detected, hexdump:\n");
#ifdef LOG
            xine_hexdump (op.packet, op.bytes);
#endif

	    memcpy(str, &locsubtype, 4);
	    str[4] = 0;
	    codec = strtoul(str, NULL, 16);
	      
	    channel= this->num_audio_streams++;
	      
	    switch (codec) {
	    case 0x01:
	      this->buf_types[stream_num] = BUF_AUDIO_LPCM_LE | channel;
	      break;
	    case 55:
	    case 0x55:
	      this->buf_types[stream_num] = BUF_AUDIO_MPEG | channel;
	      break;
	    case 0x2000:
	      this->buf_types[stream_num] = BUF_AUDIO_A52 | channel;
	      break;
	    default:
              xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                       "demux_ogg: unknown audio codec type 0x%x\n", codec);
	      this->buf_types[stream_num] = BUF_CONTROL_NOP;
	      break;
	    }
	      
	    lprintf ("subtype          0x%x\n", codec);
	    lprintf ("time_unit        %lld\n", loctime_unit);
	    lprintf ("samples_per_unit %lld\n", locsamples_per_unit);
	    lprintf ("default_len      %d\n", locdefault_len);
	    lprintf ("buffersize       %d\n", locbuffersize);
	    lprintf ("bits_per_sample  %d\n", locbits_per_sample);
	    lprintf ("channels         %d\n", locchannels);
	    lprintf ("blockalign       %d\n", locblockalign);
	    lprintf ("avgbytespersec   %d\n", locavgbytespersec);
	    lprintf ("buf_type         %08x\n",this->buf_types[stream_num]);

	    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
	    buf->type = this->buf_types[stream_num];
	    buf->decoder_flags = BUF_FLAG_HEADER;
	    buf->decoder_info[0] = 0;
	    buf->decoder_info[1] = locsamples_per_unit;
	    buf->decoder_info[2] = locbits_per_sample;
	    buf->decoder_info[3] = locchannels;
	    this->audio_fifo->put (this->audio_fifo, buf);

	    this->preview_buffers[stream_num] = 5; /* FIXME: don't know */
	    this->factor[stream_num] = 90000;
	    this->quotient[stream_num] = locsamples_per_unit;



	    this->avg_bitrate += locavgbytespersec*8;

	    /*
	     * audio metadata
	     */

	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS]
	      = locchannels;
	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS]
	      = locbits_per_sample;
	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE]
	      = locsamples_per_unit;
	    this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE]
	      = locavgbytespersec*8;

	  } else /* no audio_fifo there */
	    this->buf_types[stream_num] = BUF_CONTROL_NOP;

	} else if (op.bytes >= 142
		   && !strncmp (&op.packet[1], "Direct Show Samples embedded in Ogg", 35) ) {

          lprintf ("older Direct Show filter-generated stream header detected. Hexdump:\n");
#ifdef LOG
	  xine_hexdump (op.packet, op.bytes);
#endif
	  this->preview_buffers[stream_num] = 5; /* FIXME: don't know */

	  if ( (*(int32_t*)(op.packet+96)==0x05589f80) && (op.bytes>=184)) {

	    buf_element_t    *buf;
	    xine_bmiheader    bih;
	    int               channel;
	    uint32_t          fcc;

	    lprintf ("seems to be a video stream.\n");

	    channel = this->num_video_streams++;
	    fcc = *(uint32_t*)(op.packet+68);
	    lprintf ("fourcc %08x\n", fcc);

	    this->buf_types[stream_num] = fourcc_to_buf_video (fcc);
	    if( !this->buf_types[stream_num] )
	      this->buf_types[stream_num] = BUF_VIDEO_UNKNOWN;
	    this->buf_types[stream_num] |= channel;

	    bih.biSize          = sizeof(xine_bmiheader);
	    bih.biWidth         = *(int32_t*)(op.packet+176);
	    bih.biHeight        = *(int32_t*)(op.packet+180);
	    bih.biPlanes        = 0;
	    memcpy (&bih.biCompression, op.packet+68, 4);
	    bih.biBitCount      = *(int16_t*)(op.packet+182);
	    if (!bih.biBitCount)
	      bih.biBitCount = 24; /* FIXME ? */
	    bih.biSizeImage     = (bih.biBitCount>>3)*bih.biWidth*bih.biHeight;
	    bih.biXPelsPerMeter = 1;
	    bih.biYPelsPerMeter = 1;
	    bih.biClrUsed       = 0;
	    bih.biClrImportant  = 0;

	    buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
	    buf->decoder_flags = BUF_FLAG_HEADER;
	    this->frame_duration = (*(int64_t*)(op.packet+164)) * 9 / 1000;
	    this->factor[stream_num] = (*(int64_t*)(op.packet+164)) * 9;
	    this->quotient[stream_num] = 1000;

	    buf->decoder_info[1] = this->frame_duration;
	    memcpy (buf->content, &bih, sizeof (xine_bmiheader));
	    buf->size = sizeof (xine_bmiheader);
	    buf->type = this->buf_types[stream_num];
	    this->video_fifo->put (this->video_fifo, buf);

	    lprintf ("subtype          %.4s\n", &fcc);
	    lprintf ("buf_type         %08x\n", this->buf_types[stream_num]);
	    lprintf ("video size       %d x %d\n", bih.biWidth, bih.biHeight);
	    lprintf ("frame duration   %d\n", this->frame_duration);

	    /*
	     * video metadata
	     */

	    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]
	      = bih.biWidth;
	    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT]
	      = bih.biHeight;
	    this->stream->stream_info[XINE_STREAM_INFO_FRAME_DURATION]
	      = this->frame_duration;

	    this->avg_bitrate += 500000; /* FIXME */

	    this->ignore_keyframes = 1;

	  } else if (*(int32_t*)op.packet+96 == 0x05589F81) {

#if 0
	    /* FIXME: no test streams */

	    buf_element_t    *buf;
	    int               codec;
	    char              str[5];
	    int               channel;
	    int               extra_size;

	    extra_size         = *(int16_t*)(op.packet+140);
	    format             = *(int16_t*)(op.packet+124);
	    channels           = *(int16_t*)(op.packet+126);
	    samplerate         = *(int32_t*)(op.packet+128);
	    nAvgBytesPerSec    = *(int32_t*)(op.packet+132);
	    nBlockAlign        = *(int16_t*)(op.packet+136);
	    wBitsPerSample     = *(int16_t*)(op.packet+138);
	    samplesize         = (sh_a->wf->wBitsPerSample+7)/8;
	    cbSize             = extra_size;
	    if(extra_size > 0)
	      memcpy(wf+sizeof(WAVEFORMATEX),op.packet+142,extra_size);

#endif

	    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                     "FIXME, old audio format not handled\n");

	    this->buf_types[stream_num] = BUF_CONTROL_NOP;

	  } else {
	    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                     "old header detected but stream type is unknown\n");
	    this->buf_types[stream_num] = BUF_CONTROL_NOP;
	  }
	} else if (!strncmp (&op.packet[1], "text", 4)) {
	  int channel=0;
	  uint32_t *val;
	  buf_element_t *buf;

	  lprintf ("textstream detected.\n");
	  this->preview_buffers[stream_num] = 2;
	  channel= this->num_spu_streams++;
	  this->buf_types[stream_num] = BUF_SPU_OGM | channel;

	  /*send an empty spu to inform the video_decoder, that there is a stream*/
	  buf = this->video_fifo->buffer_pool_alloc (this->video_fifo);
	  buf->type = this->buf_types[stream_num];
	  buf->pts = 0;
	  val = (uint32_t * )buf->content;
	  *val++=0;
	  *val++=0;
	  *val++=0;
	  this->video_fifo->put (this->video_fifo, buf);

	} else if (!strncmp (&op.packet[1], "theora", 4)) {

#ifdef HAVE_THEORA
	  printf ("demux_ogg: Theorastreamsupport is highly alpha at the moment\n");

	  if (theora_decode_header(&this->t_info, &this->t_comment, &op)>=0) {

	    this->num_video_streams++;

	    this->factor[stream_num] = (int64_t) 90000 * (int64_t) this->t_info.fps_denominator;
	    this->quotient[stream_num] = this->t_info.fps_numerator;

	    this->frame_duration = ((int64_t) 90000*this->t_info.fps_denominator)/this->t_info.fps_numerator;

	    this->preview_buffers[stream_num]=3;
	    this->buf_types[stream_num] = BUF_VIDEO_THEORA;

	    this->stream->meta_info[XINE_META_INFO_VIDEOCODEC]
              = strdup ("theora");
	    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_WIDTH]
	      = this->t_info.frame_width;
	    this->stream->stream_info[XINE_STREAM_INFO_VIDEO_HEIGHT]
	      = this->t_info.frame_height;
	    this->stream->stream_info[XINE_STREAM_INFO_FRAME_DURATION]
	      = ((int64_t) 90000*this->t_info.fps_denominator)/this->t_info.fps_numerator;

	    /*currently aspect_nominator and -denumerator are 0?*/
	    if (this->t_info.aspect_denominator)
	      this->stream->stream_info[XINE_STREAM_INFO_VIDEO_RATIO]
		= ((int64_t) this->t_info.aspect_numerator*10000)/this->t_info.aspect_denominator;

#ifdef LOG
	    printf ("demux_ogg: decoded theora header \n");
	    printf ("           frameduration %d\n",this->frame_duration);
	    printf ("           w:%d h:%d \n",this->t_info.frame_width,this->t_info.frame_height);
	    printf ("           an:%d ad:%d \n",this->t_info.aspect_numerator,this->t_info.aspect_denominator);
#endif
	  } else {
	    /*Rejected stream*/
	    xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                     " A theora header was rejected by libtheora\n");
	    this->buf_types[stream_num] = BUF_CONTROL_NOP;
	    this->preview_buffers[stream_num] = 5; /* FIXME: don't know */
	  }
#else
	  this->buf_types[stream_num] = BUF_VIDEO_THEORA;
	  this->stream->meta_info[XINE_META_INFO_VIDEOCODEC]
              = strdup ("theora");
#endif

	} else {
          if(this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG){
            printf ("demux_ogg: unknown stream type (signature >%.8s<). hex dump of bos packet follows:\n", op.packet);
            xine_hexdump (op.packet, op.bytes);
          }
	  this->buf_types[stream_num] = BUF_CONTROL_NOP;
	}
      }

      /*
       * send preview buffer
       */

      lprintf ("sending preview buffer of stream type %08x\n",
               this->buf_types[stream_num]);

      send_ogg_buf (this, &op, stream_num, BUF_FLAG_PREVIEW);

      if (!ogg_page_bos(&this->og)) {

	int i;

	/* are we finished ? */
	this->preview_buffers[stream_num] --;
	  
	done = 1;

	for (i=0; i<this->num_streams; i++) {
	  if (this->preview_buffers[i]>0)
	    done = 0;

          lprintf ("%d preview buffers left to send from stream %d\n",
                   this->preview_buffers[i], i);
	}
      }
    }
  }

  ui_event.type = XINE_EVENT_UI_CHANNELS_CHANGED;
  ui_event.data_length = 0;
  xine_event_send(this->stream, &ui_event);

  /*get the streamlength*/
  get_stream_length (this);

}

static void demux_ogg_send_content (demux_ogg_t *this) {
  int stream_num;
  int cur_serno;
  
  ogg_packet op;
  
  lprintf ("send package...\n");

  if (!read_ogg_packet(this)) {
    this->status = DEMUX_FINISHED;
    lprintf ("EOF\n");
    return;
  }

  /* now we've got one new page */

  cur_serno = ogg_page_serialno (&this->og);
  stream_num=get_stream(this, cur_serno);
  if (stream_num < 0) {
    lprintf ("error: unknown stream, serialnumber %d\n", cur_serno);

    if (!ogg_page_bos(&this->og)) {
      lprintf ("help, stream with no beginning!\n");
    }

    lprintf ("adding late stream with serial number %d (all content will be discarded)\n",
	    cur_serno);

    ogg_stream_init(&this->oss[this->num_streams], cur_serno);
    stream_num = this->num_streams;
    this->buf_types[stream_num] = 0;
    this->header_granulepos[stream_num]=-1;
    this->num_streams++;
  }

  ogg_stream_pagein(&this->oss[stream_num], &this->og);

  if (ogg_page_bos(&this->og)) {
    lprintf ("beginning of stream\ndemux_ogg: serial number %d - discard\n",
	    ogg_page_serialno (&this->og));
    while (ogg_stream_packetout(&this->oss[stream_num], &op) == 1) ;
    return;
  }

  /*while keyframeseeking only process videostream*/
    if (!this->ignore_keyframes && this->keyframe_needed
      && ((this->buf_types[stream_num] & 0xFF000000) != BUF_VIDEO_BASE))
    return;

  while (ogg_stream_packetout(&this->oss[stream_num], &op) == 1) {
    /* printf("demux_ogg: packet: %.8s\n", op.packet); */
    /* printf("demux_ogg:   got a packet\n"); */

    if ((*op.packet & PACKET_TYPE_HEADER) && (this->buf_types[stream_num]!=BUF_VIDEO_THEORA) && (this->buf_types[stream_num]!=BUF_AUDIO_SPEEX)) {
      if (op.granulepos!=-1) {
	this->header_granulepos[stream_num]=op.granulepos;
	lprintf ("header with granulepos, remembering granulepos\n");
      } else {
	lprintf ("demux_ogg: header => discard\n");
      }
      continue;
    }

    /*discard granulepos-less packets and to early audiopackets*/
    if (this->resync[stream_num]) {
      if ((this->buf_types[stream_num] & 0xFF000000) == BUF_SPU_BASE) {
	/*never drop subtitles*/
	this->resync[stream_num]=0;
      } else if ((op.granulepos==-1) && (this->header_granulepos[stream_num]==-1)) {
	continue;
      } else {

	/*dump too early packets*/
	if ((get_pts(this,stream_num,op.granulepos)-this->start_pts) > -90000)
	  this->resync[stream_num]=0;
	else
	  continue;
      }
    }

    if (!this->ignore_keyframes && this->keyframe_needed) {
      lprintf ("keyframe needed... buf_type=%08x\n", this->buf_types[stream_num]);
      if (this->buf_types[stream_num] == BUF_VIDEO_THEORA) {
#ifdef HAVE_THEORA

	int keyframe_granule_shift;
	int64_t pframe=-1,iframe=-1;

	keyframe_granule_shift=intlog(this->t_info.keyframe_frequency_force-1);

	if(op.granulepos>=0){
	  iframe=op.granulepos>>keyframe_granule_shift;
	  pframe=op.granulepos-(iframe<<keyframe_granule_shift);
          xprintf (this->stream->xine, XINE_VERBOSITY_DEBUG,
                   "seeking keyframe i %lld p %lld\n",iframe,pframe);
	  if (pframe!=0)
	    continue;
	} else
	  continue;
	this->keyframe_needed = 0;
	this->start_pts=get_pts(this,stream_num,op.granulepos);
#endif
      } else if ((this->buf_types[stream_num] & 0xFF000000) == BUF_VIDEO_BASE) {

	/*calculate the current pts*/
	if (op.granulepos!=-1) {
	  this->start_pts=get_pts(this, stream_num, op.granulepos);
	} else if (this->start_pts!=-1)
	  this->start_pts=this->start_pts+this->frame_duration;

	/*seek the keyframe*/
	if ((*op.packet == PACKET_IS_SYNCPOINT) && (this->start_pts!=-1))
	  this->keyframe_needed = 0;
	else
	  continue;

      } else if ((this->buf_types[stream_num] & 0xFF000000) == BUF_VIDEO_BASE) continue;
    }
    send_ogg_buf (this, &op, stream_num, 0);

    /*delete used header_granulepos*/
    if (op.granulepos==-1)
      this->header_granulepos[stream_num]=-1;

  }
}

static int demux_ogg_send_chunk (demux_plugin_t *this_gen) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  demux_ogg_send_content (this);

  return this->status;
}

static void demux_ogg_dispose (demux_plugin_t *this_gen) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  int i;

  for (i=0; i<this->num_streams; i++) {
    ogg_stream_clear(&this->oss[i]);

    if (this->language[i]) {
      free (this->language[i]);
      this->language[i]=0;
    }
  }

  ogg_sync_clear(&this->oy);

#ifdef HAVE_THEORA
  theora_comment_clear (&this->t_comment);
  theora_info_clear (&this->t_info);
#endif

  if (this->chapter_info){
    free (this->chapter_info->entries);
    free (this->chapter_info);
  }
  if (this->title){
    free (this->title);
  }

  free (this);
}

static int demux_ogg_get_status (demux_plugin_t *this_gen) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  return this->status;
}

static void demux_ogg_send_headers (demux_plugin_t *this_gen) {
  demux_ogg_t *this = (demux_ogg_t *) this_gen;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /*
   * send start buffers
   */

  this->last_pts[0]   = 0;
  this->last_pts[1]   = 0;

  /*
   * initialize ogg engine
   */
  ogg_sync_init(&this->oy);

  this->num_streams       = 0;
  this->num_audio_streams = 0;
  this->num_video_streams = 0;
  this->num_spu_streams   = 0;
  this->avg_bitrate       = 1;

  this->input->seek (this->input, 0, SEEK_SET);

  if (this->status == DEMUX_OK) {
    xine_demux_control_start(this->stream);
    /* send header */
    demux_ogg_send_header (this);

    lprintf ("headers sent, avg bitrate is %lld\n", this->avg_bitrate);
  }

  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = this->num_video_streams>0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = this->num_audio_streams>0;
  this->stream->stream_info[XINE_STREAM_INFO_MAX_SPU_CHANNEL] = this->num_spu_streams;
}

static int demux_ogg_seek (demux_plugin_t *this_gen,
			   off_t start_pos, int start_time) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen;
  int i;
  start_time /= 1000;
  /*
   * seek to start position
   */

  if (INPUT_IS_SEEKABLE(this->input)) {

    this->keyframe_needed = (this->num_video_streams>0);

    if ( (!start_pos) && (start_time)) {
      if (this->time_length!=-1) {
	/*do the seek via time*/
	int current_time=-1;
	off_t current_pos;
	current_pos=this->input->get_current_pos(this->input);

	/*try to find out the current time*/
	if (this->last_pts[PTS_VIDEO]) {
	  current_time=this->last_pts[PTS_VIDEO]/90000;
	} else if (this->last_pts[PTS_AUDIO]) {
	  current_time=this->last_pts[PTS_AUDIO]/90000;
	}

	/*fixme, the file could grow, do something
	 about this->time_length using get_lenght to verify, that the stream
	hasn` changed its length, otherwise no seek to "new" data is possible*/

	lprintf ("seek to time %d called\n",start_time);
	lprintf ("current time is %d\n",current_time); 

	if (current_time > start_time) {
	  /*seek between beginning and current_pos*/

	  /*fixme - sometimes we seek backwards and during
	    keyframeseeking, we undo the seek*/

	  start_pos = start_time * current_pos
	  / current_time ;
	} else {
	  /*seek between current_pos and end*/
	  start_pos = current_pos +
	    ((start_time - current_time) *
	     ( this->input->get_length(this->input) - current_pos ) /
	     ( (this->time_length / 1000) - current_time)
	    );
	}

	lprintf ("current_pos is%lld\n",current_pos);
	lprintf ("new_pos is %lld\n",start_pos); 

      } else {
	/*seek using avg_bitrate*/
	start_pos = start_time * this->avg_bitrate/8;
      }

      lprintf ("seeking to %d seconds => %lld bytes\n",
	      start_time, start_pos);

    }

    ogg_sync_reset(&this->oy);

    for (i=0; i<this->num_streams; i++) {
      this->header_granulepos[i]=-1;
    }

    /*some strange streams have no syncpoint flag set at the beginning*/	 
    if (start_pos == 0)	 
      this->keyframe_needed = 0;	 

    lprintf ("seek to %lld called\n",start_pos);

    this->input->seek (this->input, start_pos, SEEK_SET);

  }

  /* fixme - this would be a nice position to do the following tasks
     1. adjust an ogg videostream to a keyframe
     2. compare the keyframe_pts with start_time. if the difference is to
        high (e.g. larger than max keyframe_intervall, do a new seek or 
	continue reading
     3. adjust the audiostreams in such a way, that the
        difference is not to high.

     In short words, do all the cleanups necessary to continue playback
     without further actions
  */
  
  this->send_newpts     = 1;

  if( !this->stream->demux_thread_running ) {
    
    this->status            = DEMUX_OK;
    this->buf_flag_seek     = 0;

  } else {
    if (start_pos!=0) {
      this->buf_flag_seek = 1;
      /*each stream has to continue with a packet that has an
       granulepos*/
      for (i=0; i<this->num_streams; i++) {
	this->resync[i]=1;
      }

      this->start_pts=-1;
    }

    xine_demux_flush_engine(this->stream);
  }
  
  return this->status;
}

static int demux_ogg_get_stream_length (demux_plugin_t *this_gen) {

  demux_ogg_t *this = (demux_ogg_t *) this_gen; 

  if (this->time_length==-1){
    if (this->avg_bitrate) {
      return (int)((int64_t)1000 * this->input->get_length (this->input) * 8 /
		   this->avg_bitrate);
    } else {
      return 0;
    }
  } else {
    return this->time_length;
  }
}

static uint32_t demux_ogg_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_SPULANG | DEMUX_CAP_AUDIOLANG;
}

static int demux_ogg_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  
  demux_ogg_t *this = (demux_ogg_t *) this_gen; 

  char *str=(char *) data;
  int channel = *((int *)data);
  int stream_num;

  switch (data_type) {
  case DEMUX_OPTIONAL_DATA_SPULANG:
    if (channel==-1) {
      strcpy( str, "none");
      return DEMUX_OPTIONAL_SUCCESS;
    } else if ((channel>=0) && (channel<this->num_streams)) {
       for (stream_num=0; stream_num<this->num_streams; stream_num++) {
	if (this->buf_types[stream_num]==BUF_SPU_OGM+channel) {
	  if (this->language[stream_num]) {
	    sprintf(str, "%s", this->language[stream_num]);
	    return DEMUX_OPTIONAL_SUCCESS;
	  } else {
	    sprintf(str, "channel %d",channel);
	    return DEMUX_OPTIONAL_SUCCESS;
	  }
	}
      }
      return DEMUX_OPTIONAL_UNSUPPORTED;
    }
    return DEMUX_OPTIONAL_UNSUPPORTED;
  case DEMUX_OPTIONAL_DATA_AUDIOLANG:
    if (channel==-1) {
      strcpy( str, "none");
      return DEMUX_OPTIONAL_SUCCESS;
    } else if ((channel>=0) && (channel<this->num_streams)) {
      for (stream_num=0; stream_num<this->num_streams; stream_num++) {
	if ((this->buf_types[stream_num]&0xFF00001F)==BUF_AUDIO_BASE+channel) {
	  if (this->language[stream_num]) {
	    sprintf(str, "%s", this->language[stream_num]);
	    return DEMUX_OPTIONAL_SUCCESS;
	  } else {
	    sprintf(str, "channel %d",channel);
	    return DEMUX_OPTIONAL_SUCCESS;
	  }
	}
      }
      return DEMUX_OPTIONAL_UNSUPPORTED;
    }
    return DEMUX_OPTIONAL_UNSUPPORTED;
  default:
    return DEMUX_OPTIONAL_UNSUPPORTED;
  }
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, 
				    xine_stream_t *stream, 
				    input_plugin_t *input) {

  demux_ogg_t *this;

  switch (stream->content_detection_method) {

  case METHOD_BY_CONTENT: {
    uint8_t buf[4];

    if (xine_demux_read_header(input, buf, 4) != 4)
      return NULL;

    if ((buf[0] != 'O')
        || (buf[1] != 'g')
        || (buf[2] != 'g')
        || (buf[3] != 'S'))
      return NULL;
  }
  break;

  case METHOD_BY_EXTENSION: {
    char *extensions, *mrl;

    mrl = input->get_mrl (input);
    extensions = class_gen->get_extensions (class_gen);

    if (!xine_demux_check_extension (mrl, extensions)) {
      return NULL;
    }
  }
  break;

  case METHOD_EXPLICIT:
  break;

  default:
    return NULL;
  }

  /*
   * if we reach this point, the input has been accepted.
   */

  this         = xine_xmalloc (sizeof (demux_ogg_t));
  memset (this, 0, sizeof(demux_ogg_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_ogg_send_headers;
  this->demux_plugin.send_chunk        = demux_ogg_send_chunk;
  this->demux_plugin.seek              = demux_ogg_seek;
  this->demux_plugin.dispose           = demux_ogg_dispose;
  this->demux_plugin.get_status        = demux_ogg_get_status;
  this->demux_plugin.get_stream_length = demux_ogg_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_ogg_get_capabilities;
  this->demux_plugin.get_optional_data = demux_ogg_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;
  
  this->status = DEMUX_FINISHED;

#ifdef HAVE_THEORA
  theora_info_init (&this->t_info);
  theora_comment_init (&this->t_comment);
#endif  

  this->chapter_info = 0;
  this->title = 0;

  return &this->demux_plugin;
}

/*
 * ogg demuxer class
 */

static char *get_description (demux_class_t *this_gen) {
  return "OGG demux plugin";
}
 
static char *get_identifier (demux_class_t *this_gen) {
  return "OGG";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "ogg ogm spx";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return "audio/x-ogg: ogg: OggVorbis Audio;"
         "audio/x-speex: ogg: Speex Audio;"
         "application/x-ogg: ogg: OggVorbis Audio;";
}

static void class_dispose (demux_class_t *this_gen) {
  demux_ogg_class_t *this = (demux_ogg_class_t *) this_gen;

  free (this);
}

static void *init_class (xine_t *xine, void *data) {
  demux_ogg_class_t     *this;

  this = xine_xmalloc (sizeof (demux_ogg_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

  return this;
}

/*
 * exported plugin catalog entry
 */

plugin_info_t xine_plugin_info[] = {
  /* type, API, "name", version, special_info, init_function */  
  { PLUGIN_DEMUX, 22, "ogg", XINE_VERSION_CODE, NULL, init_class },
  { PLUGIN_NONE, 0, "", 0, NULL, NULL }
};
