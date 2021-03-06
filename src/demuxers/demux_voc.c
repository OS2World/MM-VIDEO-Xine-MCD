/*
 * Copyright (C) 2001-2003 the xine project
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
 * Creative Voice File Demuxer by Mike Melanson (melanson@pcisys.net)
 * Note that this demuxer does not yet support very many things that can
 * possibly be seen in a VOC file. It only plays the first block in a file.
 * It will only play that block if it is PCM data. More variations will be
 * supported as they are encountered.
 *
 * $Id: demux_voc.c,v 1.32 2003/08/25 21:51:39 f1rmb Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "xine_internal.h"
#include "xineutils.h"
#include "demux.h"
#include "buffer.h"
#include "bswap.h"
#include "group_audio.h"

#define PCM_BLOCK_ALIGN 1024
#define VOC_HEADER_SIZE 0x1A
#define VOC_SIGNATURE "Creative Voice File\x1A"
#define BLOCK_PREAMBLE_SIZE 4

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  unsigned int         audio_type;
  unsigned int         audio_sample_rate;
  unsigned int         audio_bits;
  unsigned int         audio_channels;

  off_t                data_start;
  off_t                data_size;
  unsigned int         running_time;

  int                  seek_flag;  /* this is set when a seek just occurred */
} demux_voc_t;

typedef struct {
  demux_class_t     demux_class;
} demux_voc_class_t;

/* returns 1 if the VOC file was opened successfully, 0 otherwise */
static int open_voc_file(demux_voc_t *this) {
  unsigned char header[VOC_HEADER_SIZE];
  unsigned char preamble[BLOCK_PREAMBLE_SIZE];
  off_t first_block_offset;
  signed char sample_rate_divisor;

  if (xine_demux_read_header(this->input, header, VOC_HEADER_SIZE) != VOC_HEADER_SIZE)
    return 0;

  /* check the signature */
  if (strncmp(header, VOC_SIGNATURE, strlen(VOC_SIGNATURE)) != 0)
    return 0;

  /* file is qualified */
  first_block_offset = LE_16(&header[0x14]);
  this->input->seek(this->input, first_block_offset, SEEK_SET);

  /* load the block preamble */
  if (this->input->read(this->input, preamble, BLOCK_PREAMBLE_SIZE) != 
    BLOCK_PREAMBLE_SIZE)
    return 0;

  /* so far, this demuxer only cares about type 1 blocks */
  if (preamble[0] != 1) {
    xine_log(this->stream->xine, XINE_LOG_MSG,
      _("unknown VOC block type (0x%02X); please report to xine developers\n"),
      preamble[0]);
    return 0;
  }

  /* assemble 24-bit, little endian length */
  this->data_size = preamble[1] | (preamble[2] << 8) | (preamble[3] << 16);

  /* get the next 2 bytes (re-use preamble bytes) */
  if (this->input->read(this->input, preamble, 2) != 2)
    return 0;

  /* this app only knows how to deal with format 0 data (raw PCM) */
  if (preamble[1] != 0) {
    xine_log(this->stream->xine, XINE_LOG_MSG,
      _("unknown VOC compression type (0x%02X); please report to xine developers\n"),
      preamble[1]);
    return 0;
  }

  this->audio_type = BUF_AUDIO_LPCM_BE;
  sample_rate_divisor = preamble[0];
  this->audio_sample_rate = 256 - (1000000 / sample_rate_divisor);
  this->data_start = this->input->get_current_pos(this->input);
  this->audio_bits = 8;
  this->audio_channels = 1;
  this->running_time = this->data_size / this->audio_sample_rate;

  return 1;
}

static int demux_voc_send_chunk(demux_plugin_t *this_gen) {
  demux_voc_t *this = (demux_voc_t *) this_gen;

  buf_element_t *buf = NULL;
  unsigned int remaining_sample_bytes;
  off_t current_file_pos;
  int64_t current_pts;

  /* just load data chunks from wherever the stream happens to be
   * pointing; issue a DEMUX_FINISHED status if EOF is reached */
  remaining_sample_bytes = PCM_BLOCK_ALIGN;
  current_file_pos = 
    this->input->get_current_pos(this->input) - this->data_start;

  current_pts = current_file_pos;
  current_pts *= 90000;
  current_pts /= this->audio_sample_rate;

  if (this->seek_flag) {
    xine_demux_control_newpts(this->stream, current_pts, 0);
    this->seek_flag = 0;
  }

  while (remaining_sample_bytes) {
    /* abort if no audio fifo */
    if(!this->audio_fifo) {
      this->status = DEMUX_FINISHED;
      break;
    }

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->extra_info->input_pos = current_file_pos;
    buf->extra_info->input_length = this->data_size;
    buf->extra_info->input_time = current_pts / 90;
    buf->pts = current_pts;

    if (remaining_sample_bytes > buf->max_size)
      buf->size = buf->max_size;
    else
      buf->size = remaining_sample_bytes;
    remaining_sample_bytes -= buf->size;

    if (this->input->read(this->input, buf->content, buf->size) !=
      buf->size) {
      buf->free_buffer(buf);
      this->status = DEMUX_FINISHED;
      break;
    }

    if (!remaining_sample_bytes)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    this->audio_fifo->put (this->audio_fifo, buf);
  }

  return this->status;
}

static void demux_voc_send_headers(demux_plugin_t *this_gen) {
  demux_voc_t *this = (demux_voc_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->audio_channels;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    this->audio_sample_rate;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
    this->audio_bits;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo && this->audio_type) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->audio_sample_rate;
    buf->decoder_info[2] = this->audio_bits;
    buf->decoder_info[3] = this->audio_channels;
    buf->size = 0;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_voc_seek (demux_plugin_t *this_gen, off_t start_pos, int start_time) {
  demux_voc_t *this = (demux_voc_t *) this_gen;

  this->seek_flag = 1;
  this->status = DEMUX_OK;
  xine_demux_flush_engine (this->stream);

  /* if input is non-seekable, do not proceed with the rest of this
   * seek function */
  if (!INPUT_IS_SEEKABLE(this->input))
    return this->status;

  /* check the boundary offsets */
  if (start_pos < 0)
    this->input->seek(this->input, this->data_start, SEEK_SET);
  else if (start_pos >= this->data_size) {
    this->status = DEMUX_FINISHED;
    return this->status;
  } else {
    /* This function must seek along the block alignment. The start_pos
     * is in reference to the start of the data. Divide the start_pos by
     * the block alignment integer-wise, and multiply the quotient by the
     * block alignment to get the new aligned offset. Add the data start
     * offset and seek to the new position. */
    start_pos /= PCM_BLOCK_ALIGN;
    start_pos *= PCM_BLOCK_ALIGN;
    start_pos += this->data_start;

    this->input->seek(this->input, start_pos, SEEK_SET);
  }

  return this->status;
}

static void demux_voc_dispose (demux_plugin_t *this_gen) {
  demux_voc_t *this = (demux_voc_t *) this_gen;

  free(this);
}

static int demux_voc_get_status (demux_plugin_t *this_gen) {
  demux_voc_t *this = (demux_voc_t *) this_gen;

  return this->status;
}

/* return the approximate length in miliseconds */
static int demux_voc_get_stream_length (demux_plugin_t *this_gen) {
  demux_voc_t *this = (demux_voc_t *) this_gen;

  return this->running_time * 1000;
}

static uint32_t demux_voc_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_voc_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_voc_t    *this;

  this         = xine_xmalloc (sizeof (demux_voc_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_voc_send_headers;
  this->demux_plugin.send_chunk        = demux_voc_send_chunk;
  this->demux_plugin.seek              = demux_voc_seek;
  this->demux_plugin.dispose           = demux_voc_dispose;
  this->demux_plugin.get_status        = demux_voc_get_status;
  this->demux_plugin.get_stream_length = demux_voc_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_voc_get_capabilities;
  this->demux_plugin.get_optional_data = demux_voc_get_optional_data;
  this->demux_plugin.demux_class       = class_gen;

  this->status = DEMUX_FINISHED;

  switch (stream->content_detection_method) {

  case METHOD_BY_EXTENSION: {
    char *extensions, *mrl;

    mrl = input->get_mrl (input);
    extensions = class_gen->get_extensions (class_gen);

    if (!xine_demux_check_extension (mrl, extensions)) {
      free (this);
      return NULL;
    }
  }
  /* falling through is intended */

  case METHOD_BY_CONTENT:
  case METHOD_EXPLICIT:

    if (!open_voc_file(this)) {
      free (this);
      return NULL;
    }

  break;

  default:
    free (this);
    return NULL;
  }

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "VOC file demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "VOC";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "voc";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return NULL;
}

static void class_dispose (demux_class_t *this_gen) {
  demux_voc_class_t *this = (demux_voc_class_t *) this_gen;

  free (this);
}

void *demux_voc_init_plugin (xine_t *xine, void *data) {
  demux_voc_class_t     *this;

  this = xine_xmalloc (sizeof (demux_voc_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

  return this;
}
