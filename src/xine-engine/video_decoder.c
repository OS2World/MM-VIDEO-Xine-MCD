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
 *
 * $Id: video_decoder.c,v 1.136 2003/07/27 12:47:23 hadess Exp $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xine_internal.h"
#include "xineutils.h"
#ifndef __EMX__
#include <sched.h>
#endif

/*
#define LOG
*/

static void update_spu_decoder (xine_stream_t *stream, int type) {

  int streamtype = (type>>16) & 0xFF;
  
  if( stream->spu_decoder_streamtype != streamtype ||
      !stream->spu_decoder_plugin ) {
    
    if (stream->spu_decoder_plugin)
      free_spu_decoder (stream, stream->spu_decoder_plugin);
          
    stream->spu_decoder_streamtype = streamtype;
    stream->spu_decoder_plugin = get_spu_decoder (stream, streamtype);

  }
  return ;
}

static void *video_decoder_loop (void *stream_gen) {

  buf_element_t   *buf;
  xine_stream_t   *stream = (xine_stream_t *) stream_gen;
  int              running = 1;
  int              streamtype;
  int              prof_video_decode = -1;
  int              prof_spu_decode = -1;
  uint32_t         buftype_unknown = 0;
  
  if (prof_video_decode == -1)
    prof_video_decode = xine_profiler_allocate_slot ("video decoder");
  if (prof_spu_decode == -1)
    prof_spu_decode = xine_profiler_allocate_slot ("spu decoder");

  while (running) {

#ifdef LOG
    printf ("video_decoder: getting buffer...\n");  
#endif

    buf = stream->video_fifo->get (stream->video_fifo);
    extra_info_merge( stream->video_decoder_extra_info, buf->extra_info );
    stream->video_decoder_extra_info->seek_count = stream->video_seek_count;
    
#ifdef LOG
    printf ("video_decoder: got buffer 0x%08x\n", buf->type);      
#endif
    
    /* check for a new port to use */
    if (stream->next_video_port) {
      int64_t img_duration;
      int width, height;
      
      /* noone is allowed to modify the next port from now on */
      pthread_mutex_lock(&stream->next_video_port_lock);
      if (stream->video_out->status(stream->video_out, stream, &width, &height, &img_duration)) {
        /* register our stream at the new output port */
        stream->next_video_port->open(stream->next_video_port, stream);
        stream->video_out->close(stream->video_out, stream);
      }
      stream->video_out = stream->next_video_port;
      stream->next_video_port = NULL;
      pthread_mutex_unlock(&stream->next_video_port_lock);
      pthread_cond_broadcast(&stream->next_video_port_wired);
    }

    switch (buf->type & 0xffff0000) {
    case BUF_CONTROL_HEADERS_DONE:
      pthread_mutex_lock (&stream->counter_lock);
      stream->header_count_video++;
      pthread_cond_broadcast (&stream->counter_changed);
      pthread_mutex_unlock (&stream->counter_lock);
      break;

    case BUF_CONTROL_START:
      
      if (stream->video_decoder_plugin) {
	free_video_decoder (stream, stream->video_decoder_plugin);
	stream->video_decoder_plugin = NULL;
      }
      
      if (stream->spu_decoder_plugin) {
        free_spu_decoder (stream, stream->spu_decoder_plugin);
        stream->spu_decoder_plugin = NULL;
        stream->spu_track_map_entries = 0;
      }
      
      stream->metronom->handle_video_discontinuity (stream->metronom, 
						    DISC_STREAMSTART, 0);
      
      buftype_unknown = 0;
      break;

    case BUF_CONTROL_SPU_CHANNEL:
      {
	xine_event_t  ui_event;
	
	/* We use widescreen spu as the auto selection, because widescreen
	 * display is common. SPU decoders can choose differently if it suits
	 * them. */
	stream->spu_channel_auto = buf->decoder_info[0];
	stream->spu_channel_letterbox = buf->decoder_info[1];
	stream->spu_channel_pan_scan = buf->decoder_info[2];
	if (stream->spu_channel_user == -1)
	  stream->spu_channel = stream->spu_channel_auto;
	
	/* Inform UI of SPU channel changes */
	ui_event.type        = XINE_EVENT_UI_CHANNELS_CHANGED;
	ui_event.data_length = 0;

        xine_event_send (stream, &ui_event);
      }
      break;

    case BUF_CONTROL_END:

      /* wait for audio to reach this marker, if necessary */

      pthread_mutex_lock (&stream->counter_lock);

      stream->finished_count_video++;

#ifdef LOG
      printf ("video_decoder: reached end marker # %d\n", 
	      stream->finished_count_video);
#endif

      pthread_cond_broadcast (&stream->counter_changed);

      if (stream->audio_fifo) {

        while (stream->finished_count_video > stream->finished_count_audio) {
          struct timeval tv;
          struct timespec ts;
          gettimeofday(&tv, NULL);
          ts.tv_sec  = tv.tv_sec + 1;
          ts.tv_nsec = tv.tv_usec * 1000;
          /* use timedwait to workaround buggy pthread broadcast implementations */
          pthread_cond_timedwait (&stream->counter_changed, &stream->counter_lock, &ts);
        }
      }
          
      pthread_mutex_unlock (&stream->counter_lock);

      /* set engine status, send frontend notification event */
      xine_handle_stream_end (stream, buf->decoder_flags & BUF_FLAG_END_STREAM);

      /* Wake up xine_play if it's waiting for a frame */
      pthread_mutex_lock (&stream->first_frame_lock);
      if (stream->first_frame_flag) {
        stream->first_frame_flag = 0;
        pthread_cond_broadcast(&stream->first_frame_reached);
      }
      pthread_mutex_unlock (&stream->first_frame_lock);

      break;

    case BUF_CONTROL_QUIT:
      if (stream->video_decoder_plugin) {
	free_video_decoder (stream, stream->video_decoder_plugin);
	stream->video_decoder_plugin = NULL;
      }
      if (stream->spu_decoder_plugin) {
        free_spu_decoder (stream, stream->spu_decoder_plugin);
        stream->spu_decoder_plugin = NULL;
        stream->spu_track_map_entries = 0;
      }

      running = 0;
      break;

    case BUF_CONTROL_RESET_DECODER:
      extra_info_reset( stream->video_decoder_extra_info );
      stream->video_seek_count++;

      if (stream->video_decoder_plugin) {
        stream->video_decoder_plugin->reset (stream->video_decoder_plugin);
      }
      if (stream->spu_decoder_plugin) {
        stream->spu_decoder_plugin->reset (stream->spu_decoder_plugin);
      }
      break;

    case BUF_CONTROL_FLUSH_DECODER:
      if (stream->video_decoder_plugin) {
        stream->video_decoder_plugin->flush (stream->video_decoder_plugin);
      }
      break;
          
    case BUF_CONTROL_DISCONTINUITY:
#ifdef LOG
      printf ("video_decoder: discontinuity ahead\n");
#endif
      if (stream->video_decoder_plugin) {
        /* it might be a long time before we get back from a discontinuity, so we better flush
	 * the decoder before */
        stream->video_decoder_plugin->flush (stream->video_decoder_plugin);
        stream->video_decoder_plugin->discontinuity (stream->video_decoder_plugin);
      }
      
      stream->metronom->handle_video_discontinuity (stream->metronom, DISC_RELATIVE, buf->disc_off);

      break;
    
    case BUF_CONTROL_NEWPTS:
#ifdef LOG
      printf ("video_decoder: new pts %lld\n", buf->disc_off);
#endif
      if (stream->video_decoder_plugin) {
        /* it might be a long time before we get back from a discontinuity, so we better flush
	 * the decoder before */
        stream->video_decoder_plugin->flush (stream->video_decoder_plugin);
        stream->video_decoder_plugin->discontinuity (stream->video_decoder_plugin);
      }
      
      if (buf->decoder_flags & BUF_FLAG_SEEK) {
	stream->metronom->handle_video_discontinuity (stream->metronom, DISC_STREAMSEEK, buf->disc_off);
      } else {
	stream->metronom->handle_video_discontinuity (stream->metronom, DISC_ABSOLUTE, buf->disc_off);
      }
    
      break;
      
    case BUF_CONTROL_AUDIO_CHANNEL:
      {
	xine_event_t  ui_event;
	/* Inform UI of AUDIO channel changes */
	ui_event.type        = XINE_EVENT_UI_CHANNELS_CHANGED;
	ui_event.data_length = 0;
	xine_event_send (stream, &ui_event);
      }
      break;

    case BUF_CONTROL_NOP:
      break;
      
    default:

      if ( (buf->type & 0xFF000000) == BUF_VIDEO_BASE ) {

        if (stream->stream_info[XINE_STREAM_INFO_IGNORE_VIDEO])
          break;

        xine_profiler_start_count (prof_video_decode);
      
	/*
	  printf ("video_decoder: got package %d, decoder_info[0]:%d\n", 
	  buf, buf->decoder_info[0]);
	*/      
	
	streamtype = (buf->type>>16) & 0xFF;
 
        if( buf->type != buftype_unknown &&
            (stream->video_decoder_streamtype != streamtype ||
            !stream->video_decoder_plugin) ) {
          
          if (stream->video_decoder_plugin) {
            free_video_decoder (stream, stream->video_decoder_plugin);
          }
          
          stream->video_decoder_streamtype = streamtype;
          stream->video_decoder_plugin = get_video_decoder (stream, streamtype);
    
          stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED] = 
            (stream->video_decoder_plugin != NULL);
        }

        if (stream->video_decoder_plugin)
          stream->video_decoder_plugin->decode_data (stream->video_decoder_plugin, buf);  
 
        if (buf->type != buftype_unknown &&  
            !stream->stream_info[XINE_STREAM_INFO_VIDEO_HANDLED]) {
          xine_log (stream->xine, XINE_LOG_MSG, 
                    "video_decoder: no plugin available to handle '%s'\n",
                    buf_video_name( buf->type ) );
          
          if( !stream->meta_info[XINE_META_INFO_VIDEOCODEC] )
            stream->meta_info[XINE_META_INFO_VIDEOCODEC] 
              = strdup (buf_video_name( buf->type ));
          
          buftype_unknown = buf->type;
          
          /* fatal error - dispose plugin */
          if (stream->video_decoder_plugin) {
            free_video_decoder (stream, stream->video_decoder_plugin);
            stream->video_decoder_plugin = NULL;
          }
        }

        xine_profiler_stop_count (prof_video_decode);

      } else if ( (buf->type & 0xFF000000) == BUF_SPU_BASE ) {

        int      i,j;

        if (stream->stream_info[XINE_STREAM_INFO_IGNORE_SPU])
          break;

        xine_profiler_start_count (prof_spu_decode);

        update_spu_decoder(stream, buf->type);

        /* update track map */
        
        i = 0;
        while ( (i<stream->spu_track_map_entries) && (stream->spu_track_map[i]<buf->type) ) 
          i++;
        
        if ( (i==stream->spu_track_map_entries)
             || (stream->spu_track_map[i] != buf->type) ) {

          j = stream->spu_track_map_entries;

          if (j >= 50)
            break;

          while (j>i) {
            stream->spu_track_map[j] = stream->spu_track_map[j-1];
            j--;
          }
          stream->spu_track_map[i] = buf->type;
          stream->spu_track_map_entries++;
        }

        if (stream->spu_channel_user >= 0) {
          if (stream->spu_channel_user < stream->spu_track_map_entries)
            stream->spu_channel = (stream->spu_track_map[stream->spu_channel_user] & 0xFF);
          else
            stream->spu_channel = stream->spu_channel_auto;
        }

        if (stream->spu_decoder_plugin) {
          stream->spu_decoder_plugin->decode_data (stream->spu_decoder_plugin, buf);
        }

        xine_profiler_stop_count (prof_spu_decode);
        break;

      } else if (buf->type != buftype_unknown) {
	xine_log (stream->xine, XINE_LOG_MSG, 
		  "video_decoder: error, unknown buffer type: %08x\n",
		  buf->type );
	buftype_unknown = buf->type;
      }

      break;

    }

    buf->free_buffer (buf);
  }

  return NULL;
}

void video_decoder_init (xine_stream_t *stream) {
  
  pthread_attr_t       pth_attrs;
#ifndef __EMX__  
  struct sched_param   pth_params;
#endif  
  int		       err, num_buffers;

  /* The fifo size is based on dvd playback where buffers are filled
   * with 2k of data. With 500 buffers and a typical video data rate
   * of 8 Mbit/s, the fifo can hold about 1 second of video, wich
   * should be enough to compensate for drive delays.
   * We provide buffers of 8k size instead of 2k for demuxers sending
   * larger chunks.
   */

  num_buffers = stream->xine->config->register_num (stream->xine->config,
						    "video.num_buffers",
						    500,
						    "number of video buffers to allocate (higher values mean smoother playback but higher latency)",
						    NULL, 20,
						    NULL, NULL);


  stream->video_fifo = fifo_buffer_new (num_buffers, 8192);
  stream->spu_track_map_entries = 0;

  pthread_attr_init(&pth_attrs);
#ifdef __EMX__
  pthread_attr_setprio(&pth_attrs,0);
#else
  pthread_attr_getschedparam(&pth_attrs, &pth_params);
  pth_params.sched_priority = sched_get_priority_min(SCHED_OTHER);
  pthread_attr_setschedparam(&pth_attrs, &pth_params);
#endif
  pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);
  
  if ((err = pthread_create (&stream->video_thread,
			     &pth_attrs, video_decoder_loop, stream)) != 0) {
    fprintf (stderr, "video_decoder: can't create new thread (%s)\n",
	     strerror(err));
    abort();
  }
  
  pthread_attr_destroy(&pth_attrs);
}

void video_decoder_shutdown (xine_stream_t *stream) {

  buf_element_t *buf;
  void          *p;

#ifdef LOG
  printf ("video_decoder: shutdown...\n");
#endif

  /* stream->video_fifo->clear(stream->video_fifo); */

  buf = stream->video_fifo->buffer_pool_alloc (stream->video_fifo);
#ifdef LOG
  printf ("video_decoder: shutdown...2\n");
#endif
  buf->type = BUF_CONTROL_QUIT;
  stream->video_fifo->put (stream->video_fifo, buf);
#ifdef LOG
  printf ("video_decoder: shutdown...3\n");
#endif

  pthread_join (stream->video_thread, &p);
#ifdef LOG
  printf ("video_decoder: shutdown...4\n");
#endif

  /* wakeup any rewire operations */
  pthread_cond_broadcast(&stream->next_video_port_wired);
}

