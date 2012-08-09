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
 * network buffering control
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>

/********** logging **********/
#define LOG_MODULE "net_buf_ctrl"
#define LOG_VERBOSE
/*
#define LOG
*/

#include "net_buf_ctrl.h"

#define DEFAULT_LOW_WATER_MARK     1
#define DEFAULT_HIGH_WATER_MARK 5000 /* in 1/1000 s */

#define FULL_FIFO_MARK             5 /* buffers free */

#define WRAP_THRESHOLD       5*90000 /* from the asf demuxer */

#define FIFO_PUT                   0
#define FIFO_GET                   1

struct nbc_s {

  xine_stream_t   *stream;

  int              buffering;
  int              enabled;

  int              progress;
  fifo_buffer_t   *video_fifo;
  fifo_buffer_t   *audio_fifo;
  int              video_fifo_fill;
  int              audio_fifo_fill;
  int              video_fifo_free;
  int              audio_fifo_free;
  int64_t          video_fifo_length; /* in ms */
  int64_t          audio_fifo_length; /* in ms */

  int64_t          low_water_mark;
  int64_t          high_water_mark;
  /* bitrate */
  int64_t          video_last_pts;
  int64_t          audio_last_pts;
  int64_t          video_first_pts;
  int64_t          audio_first_pts;
  int64_t          video_fifo_size;
  int64_t          audio_fifo_size;
  int64_t          video_br;
  int64_t          audio_br;

  int              video_in_disc;
  int              audio_in_disc;

  pthread_mutex_t  mutex;
};

static void report_progress (xine_stream_t *stream, int p) {

  xine_event_t             event;
  xine_progress_data_t     prg;

  prg.description = _("Buffering...");
  prg.percent = (p>100)?100:p;

  event.type = XINE_EVENT_PROGRESS;
  event.data = &prg;
  event.data_length = sizeof (xine_progress_data_t);

  xine_event_send (stream, &event);
}

static void nbc_set_speed_pause (xine_stream_t *stream) {
  lprintf("\nnet_buf_ctrl: nbc_put_cb: set_speed_pause\n");
  stream->xine->clock->set_speed (stream->xine->clock, XINE_SPEED_PAUSE);
  stream->xine->clock->set_option (stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 0);
  if (stream->audio_out)
    stream->audio_out->set_property(stream->audio_out,AO_PROP_PAUSED,2);
}

static void nbc_set_speed_normal (xine_stream_t *stream) {
  lprintf("\nnet_buf_ctrl: nbc_put_cb: set_speed_normal\n");
  stream->xine->clock->set_speed (stream->xine->clock, XINE_SPEED_NORMAL);
  stream->xine->clock->set_option (stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 1);
  if (stream->audio_out)
    stream->audio_out->set_property(stream->audio_out,AO_PROP_PAUSED,0);
}

void nbc_check_buffers (nbc_t *this) {
  /* Deprecated */
}

static void display_stats (nbc_t *this) {
  char *buffering[2] = {"   ", "buf"};
  char *enabled[2]   = {"off", "on "};

  if (this->stream->xine->verbosity >= 2) {
    printf("net_buf_ctrl: vid %3d%% %4.1fs %4lldkbps %1d, "\
           "aud %3d%% %4.1fs %4lldkbps %1d, %s %s\r",
           this->video_fifo_fill,
           (float)(this->video_fifo_length / 1000),
           this->video_br / 1000,
           this->video_in_disc,
           this->audio_fifo_fill,
           (float)(this->audio_fifo_length / 1000),
           this->audio_br / 1000,
           this->audio_in_disc,
           buffering[this->buffering],
           enabled[this->enabled]
          );
    fflush(stdout);
  }
}

/*  Try to compute the length of the fifo in 1/1000 s
 *  2 methods :
 *    if the bitrate is known
 *      use the size of the fifo
 *    else
 *      use the the first and the last pts of the fifo
 */
static void nbc_compute_fifo_length(nbc_t *this,
                                    fifo_buffer_t *fifo,
                                    buf_element_t *buf,
                                    int action) {
  int fifo_free, fifo_fill;
  int64_t video_br, audio_br;
  int has_video, has_audio;

  has_video = this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO];
  has_audio = this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO];
  video_br  = this->stream->stream_info[XINE_STREAM_INFO_VIDEO_BITRATE];
  audio_br  = this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITRATE];

  fifo_free = fifo->buffer_pool_num_free;
  fifo_fill = fifo->fifo_size;

  if (fifo == this->video_fifo) {
    this->video_fifo_free = fifo_free;
    this->video_fifo_fill = (100 * fifo_fill) / (fifo_fill + fifo_free - 1);
    this->video_fifo_size = fifo->fifo_data_size;
    if (video_br) {
      this->video_br = video_br;
      this->video_fifo_length = (8000 * this->video_fifo_size) / this->video_br;
    } else {
      if (buf->pts && (this->video_in_disc == 0)) {
        if (action == FIFO_PUT) {
          this->video_last_pts = buf->pts;
          if (this->video_first_pts == 0) {
            this->video_first_pts = buf->pts;
          }
        } else {
          /* GET */
          this->video_first_pts = buf->pts;
        }
        this->video_fifo_length = (this->video_last_pts - this->video_first_pts) / 90;
        if (this->video_fifo_length)
          this->video_br = 8000 * (this->video_fifo_size / this->video_fifo_length);
        else
          this->video_br = 0;
      } else {
        if (this->video_br)
          this->video_fifo_length = (8000 * this->video_fifo_size) / this->video_br;
      }
    }
  } else {
    this->audio_fifo_free = fifo_free;
    this->audio_fifo_fill = (100 * fifo_fill) / (fifo_fill + fifo_free - 1);
    this->audio_fifo_size = fifo->fifo_data_size;
    if (audio_br) {
      this->audio_br = audio_br;
      this->audio_fifo_length = (8000 * this->audio_fifo_size) / this->audio_br;
    } else {
      if (buf->pts && (this->audio_in_disc == 0)) {
        if (action == FIFO_PUT) {
          this->audio_last_pts = buf->pts;
          if (!this->audio_first_pts) {
            this->audio_first_pts = buf->pts;
          }
        } else {
          /* GET */
          this->audio_first_pts = buf->pts;
        }
        this->audio_fifo_length = (this->audio_last_pts - this->audio_first_pts) / 90;
        if (this->audio_fifo_length)
          this->audio_br = 8000 * (this->audio_fifo_size / this->audio_fifo_length);
        else
          this->audio_br = 0;
      } else {
        if (this->audio_br)
          this->audio_fifo_length = (8000 * this->audio_fifo_size) / this->audio_br;
      }
    }
  }
}

/* Alloc callback */
static void nbc_alloc_cb (fifo_buffer_t *fifo, void *this_gen) {
  nbc_t *this = (nbc_t*)this_gen;

  if (this->buffering) {

    /* restart playing if one fifo is full (to avoid deadlock) */
    if (fifo->buffer_pool_num_free <= 1) {
      this->progress = 100;
      report_progress (this->stream, 100);
      this->buffering = 0;

      if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
        printf("\nnet_buf_ctrl: nbc_alloc_cb: stops buffering\n");

      nbc_set_speed_normal(this->stream);
    }
  }
}

/* Put callback
 * the fifo mutex is locked */
static void nbc_put_cb (fifo_buffer_t *fifo, 
                        buf_element_t *buf, void *this_gen) {
  nbc_t *this = (nbc_t*)this_gen;
  int64_t progress = 0;
  int64_t video_p = 0;
  int64_t audio_p = 0;
  int has_video, has_audio;

  pthread_mutex_lock(&this->mutex);

  if ((buf->type & BUF_MAJOR_MASK) != BUF_CONTROL_BASE) {

    /* do nothing if we are at the end of the stream */
    if (!this->enabled) {
      /* a new stream starts */
      if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
        printf("\nnet_buf_ctrl: nbc_put_cb: starts buffering\n");
      this->enabled           = 1;
      this->buffering         = 1;
      this->video_first_pts   = 0;
      this->video_last_pts    = 0;
      this->audio_first_pts   = 0;
      this->audio_last_pts    = 0;
      this->video_fifo_length = 0;
      this->audio_fifo_length = 0;
      nbc_set_speed_pause(this->stream);
      this->progress = 0;
      report_progress (this->stream, progress);
    }

    nbc_compute_fifo_length(this, fifo, buf, FIFO_PUT);

    if (this->buffering) {

      has_video = this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO];
      has_audio = this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO];
      /* restart playing if high_water_mark is reached by all fifos
       * do not restart if has_video and has_audio are false to avoid
       * a yoyo effect at the beginning of the stream when these values
       * are not yet known.
       * 
       * be sure that the next buffer_pool_alloc() call will not deadlock,
       * we need at least 2 buffers (see buffer.c)
       */
      if ((((!has_video) || (this->video_fifo_length > this->high_water_mark)) &&
           ((!has_audio) || (this->audio_fifo_length > this->high_water_mark)) &&
           (has_video || has_audio))) {

        this->progress = 100;
        report_progress (this->stream, 100);
        this->buffering = 0;

        if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
          printf("\nnet_buf_ctrl: nbc_put_cb: stops buffering\n");

        nbc_set_speed_normal(this->stream);

      } else {
        /*  compute the buffering progress
         *    50%: video
         *    50%: audio */
        video_p = ((this->video_fifo_length * 50) / this->high_water_mark);
        if (video_p > 50) video_p = 50;
        audio_p = ((this->audio_fifo_length * 50) / this->high_water_mark);
        if (audio_p > 50) audio_p = 50;

        if ((has_video) && (has_audio)) {
          progress = video_p + audio_p;
        } else if (has_video) {
          progress = 2 * video_p;
        } else {
          progress = 2 * audio_p;
        }

        /* if the progress can't be computed using the fifo length,
           use the number of buffers */
        if (!progress) {
          video_p = this->video_fifo_fill;
          audio_p = this->audio_fifo_fill;
          progress = (video_p > audio_p) ? video_p : audio_p;
        }

        if (progress > this->progress) {
          report_progress (this->stream, progress);
          this->progress = progress;
        }
      }
    }
  } else {

    switch (buf->type) {
      case BUF_CONTROL_NOP:
      case BUF_CONTROL_END:

        /* end of stream :
         *   - disable the nbc
         *   - unpause the engine if buffering
         */
        if ((buf->decoder_flags & BUF_FLAG_END_USER) ||
            (buf->decoder_flags & BUF_FLAG_END_STREAM)) {
          this->enabled = 0;
          if (this->buffering) {
            this->buffering = 0;
            this->progress = 100;
            report_progress (this->stream, this->progress);

            if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
              printf("\nnet_buf_ctrl: nbc_put_cb: stops buffering\n");

            nbc_set_speed_normal(this->stream);
          }
        }
        break;

      case BUF_CONTROL_NEWPTS:
        /* discontinuity management */
        if (fifo == this->video_fifo) {
          this->video_in_disc++;
          lprintf("\nnet_buf_ctrl: nbc_put_cb video disc %d\n", this->video_in_disc);
        } else {
          this->audio_in_disc++;
          lprintf("\nnet_buf_ctrl: nbc_put_cb audio disc %d\n", this->audio_in_disc);
        }
        break;
    }

    if (fifo == this->video_fifo) {
      this->video_fifo_free = fifo->buffer_pool_num_free;
      this->video_fifo_size = fifo->fifo_data_size;
    } else {
      this->audio_fifo_free = fifo->buffer_pool_num_free;
      this->audio_fifo_size = fifo->fifo_data_size;
    }
  }


  display_stats(this);
  pthread_mutex_unlock(&this->mutex);
}

/* Get callback
 * the fifo mutex is locked */
static void nbc_get_cb (fifo_buffer_t *fifo,
			buf_element_t *buf, void *this_gen) {
  nbc_t *this = (nbc_t*)this_gen;
  pthread_mutex_lock(&this->mutex);

  if ((buf->type & BUF_MAJOR_MASK) != BUF_CONTROL_BASE) {

    nbc_compute_fifo_length(this, fifo, buf, FIFO_GET);

    if (this->enabled) {

      if (!this->buffering) {
        /* start buffering if one fifo is empty
         */
        int has_video = this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO];
        int has_audio = this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO];
        if (fifo->fifo_size == 0 && 
            (((fifo == this->video_fifo) && has_video) ||
             ((fifo == this->audio_fifo) && has_audio))) {
          int other_fifo_free;

          if (fifo == this->video_fifo) {
            other_fifo_free = this->audio_fifo_free;
          } else {
            other_fifo_free = this->video_fifo_free;
          }

          /* Don't pause if the other fifo is full because the next
             put() will restart the engine */
          if (other_fifo_free > FULL_FIFO_MARK) {
            this->buffering = 1;
            this->progress  = 0;
            report_progress (this->stream, 0);

            if (this->stream->xine->verbosity >= XINE_VERBOSITY_DEBUG)
              printf("\nnet_buf_ctrl: nbc_get_cb: starts buffering, vid: %d, aud: %d\n",
                     this->video_fifo_fill, this->audio_fifo_fill);
            nbc_set_speed_pause(this->stream);
          }
        }
      } else {
        nbc_set_speed_pause(this->stream);
      }
    }
  } else {
    /* discontinuity management */
    if (buf->type == BUF_CONTROL_NEWPTS) {
      if (fifo == this->video_fifo) {
        this->video_in_disc--;
        lprintf("\nnet_buf_ctrl: nbc_get_cb video disc %d\n", this->video_in_disc);
      } else {
        this->audio_in_disc--;
        lprintf("\nnet_buf_ctrl: nbc_get_cb audio disc %d\n", this->audio_in_disc);
      }
    }

    if (fifo == this->video_fifo) {
      this->video_fifo_free = fifo->buffer_pool_num_free;
      this->video_fifo_size = fifo->fifo_data_size;
    } else {
      this->audio_fifo_free = fifo->buffer_pool_num_free;
      this->audio_fifo_size = fifo->fifo_data_size;
    }
  }
  display_stats(this);
  pthread_mutex_unlock(&this->mutex);
}

nbc_t *nbc_init (xine_stream_t *stream) {

  nbc_t *this = (nbc_t *) malloc (sizeof (nbc_t));
  fifo_buffer_t *video_fifo = stream->video_fifo;
  fifo_buffer_t *audio_fifo = stream->audio_fifo;

  lprintf("net_buf_ctrl: nbc_init\n");
  pthread_mutex_init (&this->mutex, NULL);

  this->stream              = stream;
  this->buffering           = 0;
  this->enabled             = 0;
  this->low_water_mark      = DEFAULT_LOW_WATER_MARK;
  this->high_water_mark     = DEFAULT_HIGH_WATER_MARK;
  this->progress            = 0;
  this->video_fifo          = video_fifo;
  this->audio_fifo          = audio_fifo;
  this->video_fifo_fill     = 0;
  this->audio_fifo_fill     = 0;
  this->video_fifo_free     = 0;
  this->audio_fifo_free     = 0;
  this->video_fifo_length   = 0;
  this->audio_fifo_length   = 0;
  this->video_last_pts      = 0;
  this->audio_last_pts      = 0;
  this->video_first_pts     = 0;
  this->audio_first_pts     = 0;
  this->video_fifo_size     = 0;
  this->audio_fifo_size     = 0;
  this->video_br            = 0;
  this->audio_br            = 0;
  this->video_in_disc       = 0;
  this->audio_in_disc       = 0;

  video_fifo->register_alloc_cb(video_fifo, nbc_alloc_cb, this);
  video_fifo->register_put_cb(video_fifo, nbc_put_cb, this);
  video_fifo->register_get_cb(video_fifo, nbc_get_cb, this);

  if (audio_fifo) {
    audio_fifo->register_alloc_cb(audio_fifo, nbc_alloc_cb, this);
    audio_fifo->register_put_cb(audio_fifo, nbc_put_cb, this);
    audio_fifo->register_get_cb(audio_fifo, nbc_get_cb, this);
  }

  return this;
}

void nbc_close (nbc_t *this) {
  fifo_buffer_t *video_fifo = this->stream->video_fifo;
  fifo_buffer_t *audio_fifo = this->stream->audio_fifo;

  lprintf("\nnet_buf_ctrl: nbc_close\n");

  video_fifo->unregister_alloc_cb(video_fifo, nbc_alloc_cb);
  video_fifo->unregister_put_cb(video_fifo, nbc_put_cb);
  video_fifo->unregister_get_cb(video_fifo, nbc_get_cb);

  if (audio_fifo) {
    audio_fifo->unregister_alloc_cb(audio_fifo, nbc_alloc_cb);
    audio_fifo->unregister_put_cb(audio_fifo, nbc_put_cb);
    audio_fifo->unregister_get_cb(audio_fifo, nbc_get_cb);
  }

  pthread_mutex_lock(&this->mutex);
  this->stream->xine->clock->set_option (this->stream->xine->clock, CLOCK_SCR_ADJUSTABLE, 1);

  if (this->buffering) {
    this->buffering = 0;
    nbc_set_speed_normal(this->stream);
  }

  pthread_mutex_unlock(&this->mutex);

  free (this);
  lprintf("\nnet_buf_ctrl: nbc_close: done\n");
}


void nbc_set_high_water_mark(nbc_t *this, int value) {
/*
  Deprecated
  this->high_water_mark = value;
*/
  printf("\nnet_buf_ctrl: this method is deprecated, please fix the input plugin\n");
}

void nbc_set_low_water_mark(nbc_t *this, int value) {
/*
  Deprecated
  this->low_water_mark = value;
*/
  printf("\nnet_buf_ctrl: this method is deprecated, please fix the input plugin\n");
}
