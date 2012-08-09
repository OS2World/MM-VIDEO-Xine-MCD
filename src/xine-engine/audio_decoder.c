/*
 * Copyright (C) 2000-2002 the xine project
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
 * $Id: audio_decoder.c,v 1.108 2003/07/27 12:47:23 hadess Exp $
 *
 *
 * functions that implement audio decoding
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __EMX__
#include <sched.h>
#endif
#include <unistd.h>

#include "xine_internal.h"
#include "xineutils.h"

/*
#define LOG
*/

static void *audio_decoder_loop (void *stream_gen) {

  buf_element_t   *buf;
  xine_stream_t   *stream = (xine_stream_t *) stream_gen;
  int              running = 1;
  int              prof_audio_decode = -1;
  uint32_t         buftype_unknown = 0;

  if (prof_audio_decode == -1)
    prof_audio_decode = xine_profiler_allocate_slot ("audio decoder/output");

  while (running) {

#ifdef LOG
    printf ("audio_loop: waiting for package...\n");  
#endif

    buf = stream->audio_fifo->get (stream->audio_fifo);

    
#ifdef LOG
    printf ("audio_loop: got package pts = %lld, type = %08x\n", 
	    buf->pts, buf->type); 
#endif    

    extra_info_merge( stream->audio_decoder_extra_info, buf->extra_info );
    stream->audio_decoder_extra_info->seek_count = stream->video_seek_count;
      
    /* check for a new port to use */
    if (stream->next_audio_port) {
      uint32_t bits, rate;
      int mode;
      
      /* noone is allowed to modify the next port from now on */
      pthread_mutex_lock(&stream->next_audio_port_lock);
      if (stream->audio_out->status(stream->audio_out, stream, &bits, &rate, &mode)) {
        /* register our stream at the new output port */
        stream->next_audio_port->open(stream->next_audio_port, stream, bits, rate, mode);
        stream->audio_out->close(stream->audio_out, stream);
      }
      stream->audio_out = stream->next_audio_port;
      stream->next_audio_port = NULL;
      pthread_mutex_unlock(&stream->next_audio_port_lock);
      pthread_cond_broadcast(&stream->next_audio_port_wired);
    }

    switch (buf->type) {
      
    case BUF_CONTROL_HEADERS_DONE:
      pthread_mutex_lock (&stream->counter_lock);
      stream->header_count_audio++;
      pthread_cond_broadcast (&stream->counter_changed);
      pthread_mutex_unlock (&stream->counter_lock);
      break;

    case BUF_CONTROL_START:

#ifdef LOG
      printf ("audio_decoder: start\n");
#endif

      if (stream->audio_decoder_plugin) {

#ifdef LOG
	printf ("audio_decoder: close old decoder\n");
#endif	

	free_audio_decoder (stream, stream->audio_decoder_plugin);
	stream->audio_decoder_plugin = NULL;
	stream->audio_track_map_entries = 0;
	stream->audio_type = 0;
      }
      
      stream->metronom->handle_audio_discontinuity (stream->metronom, DISC_STREAMSTART, 0);
      
      buftype_unknown = 0;
      break;
      
    case BUF_CONTROL_END:

      /* wait for video to reach this marker, if necessary */
      
      pthread_mutex_lock (&stream->counter_lock);

      stream->finished_count_audio++;

#ifdef LOG
      printf ("audio_decoder: reached end marker # %d\n", 
	      stream->finished_count_audio);
#endif

      pthread_cond_broadcast (&stream->counter_changed);

      while (stream->finished_count_video < stream->finished_count_audio) {
        struct timeval tv;
        struct timespec ts;
        gettimeofday(&tv, NULL);
        ts.tv_sec  = tv.tv_sec + 1;
        ts.tv_nsec = tv.tv_usec * 1000;
        /* use timedwait to workaround buggy pthread broadcast implementations */
        pthread_cond_timedwait (&stream->counter_changed, &stream->counter_lock, &ts);
      }
          
      pthread_mutex_unlock (&stream->counter_lock);

      stream->audio_channel_auto = -1;

      break;
      
    case BUF_CONTROL_QUIT:
      if (stream->audio_decoder_plugin) {
	free_audio_decoder (stream, stream->audio_decoder_plugin);
	stream->audio_decoder_plugin = NULL;
	stream->audio_track_map_entries = 0;
	stream->audio_type = 0;
      }
      running = 0;
      break;

    case BUF_CONTROL_NOP:
      break;

    case BUF_CONTROL_RESET_DECODER:
#ifdef LOG
      printf ("audio_decoder: reset\n");
#endif
      extra_info_reset( stream->audio_decoder_extra_info );
      if (stream->audio_decoder_plugin)
        stream->audio_decoder_plugin->reset (stream->audio_decoder_plugin);
      break;
          
    case BUF_CONTROL_DISCONTINUITY:
      if (stream->audio_decoder_plugin)
        stream->audio_decoder_plugin->discontinuity (stream->audio_decoder_plugin);
      stream->metronom->handle_audio_discontinuity (stream->metronom, DISC_RELATIVE, buf->disc_off);
      break;

    case BUF_CONTROL_NEWPTS:
      if (stream->audio_decoder_plugin)
        stream->audio_decoder_plugin->discontinuity (stream->audio_decoder_plugin);
      if (buf->decoder_flags && BUF_FLAG_SEEK) {
        stream->metronom->handle_audio_discontinuity (stream->metronom, DISC_STREAMSEEK, buf->disc_off);
      } else {
        stream->metronom->handle_audio_discontinuity (stream->metronom, DISC_ABSOLUTE, buf->disc_off);
      }
      break;

    case BUF_CONTROL_AUDIO_CHANNEL:
      {
	if (stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
	  printf ("audio_decoder: suggested switching to stream_id %02x\n",
		  buf->decoder_info[0]);
	stream->audio_channel_auto = buf->decoder_info[0] & 0xff;
      }
      break;

    default:

      if (stream->stream_info[XINE_STREAM_INFO_IGNORE_AUDIO])
        break;

      xine_profiler_start_count (prof_audio_decode);

      if ( (buf->type & 0xFF000000) == BUF_AUDIO_BASE ) {
	
	uint32_t audio_type = 0;
	int      i,j;

	/*
        printf("audio_decoder: buf_type=%08x auto=%08x user=%08x\n",
	       buf->type, 
	       stream->audio_channel_auto,
	       stream->audio_channel_user);
	       */

        /* update track map */
        
        i = 0;
        while ( (i<stream->audio_track_map_entries) && (stream->audio_track_map[i]<buf->type) ) 
          i++;
        
        if ( (i==stream->audio_track_map_entries) 
	     || (stream->audio_track_map[i] != buf->type) ) {
          
          j = stream->audio_track_map_entries;

          if (j >= 50)
            break;

          while (j>i) {
            stream->audio_track_map[j] = stream->audio_track_map[j-1];
            j--;
          }
          stream->audio_track_map[i] = buf->type;
          stream->audio_track_map_entries++;
        }

	/* find out which audio type to decode */

#ifdef LOG
	printf ("audio_decoder: audio_channel_user = %d, map[0]=%08x\n",
		stream->audio_channel_user,
		stream->audio_track_map[0]);
#endif

	if (stream->audio_channel_user > -2) {

	  if (stream->audio_channel_user == -1) {

	    /* auto */

#ifdef LOG
	    printf ("audio_decoder: audio_channel_auto = %d\n",
		    stream->audio_channel_auto);
#endif

	    if (stream->audio_channel_auto>=0) {
 
	      if ((buf->type & 0xFF) == stream->audio_channel_auto) {
		audio_type = buf->type;
	      } else
		audio_type = -1;

	    } else
	      audio_type = stream->audio_track_map[0];

	  } else {
	    if (stream->audio_channel_user <= stream->audio_track_map_entries)
	      audio_type = stream->audio_track_map[stream->audio_channel_user];
	    else
	      audio_type = -1;
	  }

	  /* now, decode stream buffer if it's the right audio type */
	  
	  if (buf->type == audio_type) {
	    
	    int streamtype = (buf->type>>16) & 0xFF;

	    /* close old decoder of audio type has changed */
        
            if( buf->type != buftype_unknown && 
                (stream->audio_decoder_streamtype != streamtype ||
                !stream->audio_decoder_plugin) ) {
              
              if (stream->audio_decoder_plugin) {
                free_audio_decoder (stream, stream->audio_decoder_plugin);
              }
              
              stream->audio_decoder_streamtype = streamtype;
              stream->audio_decoder_plugin = get_audio_decoder (stream, streamtype);
              
              stream->stream_info[XINE_STREAM_INFO_AUDIO_HANDLED] = 
                (stream->audio_decoder_plugin != NULL);
            }
	    
	    if (audio_type != stream->audio_type) {
	      
	      if (stream->audio_decoder_plugin) {
		xine_event_t event;

		stream->audio_type = audio_type;

		event.type         = XINE_EVENT_UI_CHANNELS_CHANGED;
		event.data_length  = 0;
		xine_event_send(stream, &event);
	      }
	    }
	    
	    /* finally - decode data */
	    
	    if (stream->audio_decoder_plugin) 
	      stream->audio_decoder_plugin->decode_data (stream->audio_decoder_plugin, buf);
       
	    if (buf->type != buftype_unknown && 
	        !stream->stream_info[XINE_STREAM_INFO_AUDIO_HANDLED]) {
	      xine_log (stream->xine, XINE_LOG_MSG, 
			"audio_decoder: no plugin available to handle '%s'\n",
		        buf_audio_name( buf->type ) );
              
              if( !stream->meta_info[XINE_META_INFO_AUDIOCODEC] )
                stream->meta_info[XINE_META_INFO_AUDIOCODEC] 
                  = strdup (buf_audio_name( buf->type ));
                
	      buftype_unknown = buf->type;

	      /* fatal error - dispose plugin */       
	      if (stream->audio_decoder_plugin) {
	        free_audio_decoder (stream, stream->audio_decoder_plugin);
	        stream->audio_decoder_plugin = NULL;
	      }
	    }
	  }
	} 
      } else if( buf->type != buftype_unknown ) {
	  xine_log (stream->xine, XINE_LOG_MSG, 
		    "audio_decoder: error, unknown buffer type: %08x\n",
		    buf->type );
	  buftype_unknown = buf->type;
      }

      xine_profiler_stop_count (prof_audio_decode);
    }
    
    buf->free_buffer (buf);
  }

  return NULL;
}

void audio_decoder_init (xine_stream_t *stream) {

  pthread_attr_t       pth_attrs;
#ifndef __EMX__  
  struct sched_param   pth_params;
#endif
  int                  err;

  if (stream->audio_out == NULL) {
    stream->audio_fifo     = NULL;
    return;
  }
  
  /* The fifo size is based on dvd playback where buffers are filled
   * with 2k of data. With 230 buffers and a typical audio data rate
   * of 1.8 Mbit/s (four ac3 streams), the fifo can hold about 2 seconds
   * of audio, wich should be enough to compensate for drive delays.
   * We provide buffers of 8k size instead of 2k for demuxers sending
   * larger chunks.
   */
  stream->audio_fifo = fifo_buffer_new (230, 8192);
  stream->audio_channel_user = -1;
  stream->audio_channel_auto = -1;
  stream->audio_track_map_entries = 0;
  stream->audio_type = 0;

  /* future magic - coming soon
  stream->audio_temp = lrb_new (100, stream->audio_fifo);
  */

  pthread_attr_init(&pth_attrs);
#ifdef __EMX__
  pthread_attr_setprio(&pth_attrs,0);
#else
  pthread_attr_getschedparam(&pth_attrs, &pth_params);
  pth_params.sched_priority = sched_get_priority_min(SCHED_OTHER);
  pthread_attr_setschedparam(&pth_attrs, &pth_params);
#endif
  pthread_attr_setscope(&pth_attrs, PTHREAD_SCOPE_SYSTEM);
  
  if ((err = pthread_create (&stream->audio_thread,
			     &pth_attrs, audio_decoder_loop, stream)) != 0) {
    fprintf (stderr, "audio_decoder: can't create new thread (%s)\n",
	     strerror(err));
    abort();
  }
  
  pthread_attr_destroy(&pth_attrs);
}

void audio_decoder_shutdown (xine_stream_t *stream) {

  buf_element_t *buf;
  void          *p;

  if (stream->audio_fifo) {
    /* stream->audio_fifo->clear(stream->audio_fifo); */

    buf = stream->audio_fifo->buffer_pool_alloc (stream->audio_fifo);
    buf->type = BUF_CONTROL_QUIT;
    stream->audio_fifo->put (stream->audio_fifo, buf);
    
    pthread_join (stream->audio_thread, &p); 
    
    stream->audio_fifo->dispose (stream->audio_fifo);
    stream->audio_fifo = NULL;
  }

  /* wakeup any rewire operations */
  pthread_cond_broadcast(&stream->next_audio_port_wired);
}

int xine_get_audio_channel (xine_stream_t *stream) {

  return stream->audio_type & 0xFFFF; 
}

