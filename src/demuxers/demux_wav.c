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
 * MS WAV File Demuxer by Mike Melanson (melanson@pcisys.net)
 * based on WAV specs that are available far and wide
 *
 * $Id: demux_wav.c,v 1.49 2003/10/12 00:06:00 tmattern Exp $
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

#define WAV_SIGNATURE_SIZE 16
/* this is the hex value for 'data' */
#define data_TAG 0x61746164
#define PCM_BLOCK_ALIGN 1024

#define PREFERED_BLOCK_SIZE 4096

typedef struct {
  demux_plugin_t       demux_plugin;

  xine_stream_t       *stream;
  fifo_buffer_t       *video_fifo;
  fifo_buffer_t       *audio_fifo;
  input_plugin_t      *input;
  int                  status;

  xine_waveformatex    *wave;
  int                  wave_size;
  unsigned int         audio_type;

  off_t                data_start;
  off_t                data_size;

  int                  seek_flag;  /* this is set when a seek just occurred */
} demux_wav_t;

typedef struct {
  demux_class_t     demux_class;
} demux_wav_class_t;

/* returns 1 if the WAV file was opened successfully, 0 otherwise */
static int open_wav_file(demux_wav_t *this) {
  uint8_t signature[WAV_SIGNATURE_SIZE];
  uint32_t chunk_tag;
  uint32_t chunk_size;
  uint8_t chunk_preamble[8];

  /* check the signature */
  if (xine_demux_read_header(this->input, signature, WAV_SIGNATURE_SIZE) != WAV_SIGNATURE_SIZE)
    return 0;

  if ((signature[0] != 'R') ||
      (signature[1] != 'I') ||
      (signature[2] != 'F') ||
      (signature[3] != 'F') ||
      (signature[8] != 'W') ||
      (signature[9] != 'A') ||
      (signature[10] != 'V') ||
      (signature[11] != 'E') ||
      (signature[12] != 'f') ||
      (signature[13] != 'm') ||
      (signature[14] != 't') ||
      (signature[15] != ' '))
    return 0;

  /* file is qualified; skip over the header bytes in the stream */
  this->input->seek(this->input, WAV_SIGNATURE_SIZE, SEEK_SET);

  /* go after the format structure */
  if (this->input->read(this->input,
    (unsigned char *)&this->wave_size, 4) != 4)
    return 0;
  this->wave_size = le2me_32(this->wave_size);
  this->wave = (xine_waveformatex *) malloc( this->wave_size );
    
  if (this->input->read(this->input, (void *)this->wave, this->wave_size) !=
    this->wave_size) {
    free (this->wave);
    return 0;
  }
  xine_waveformatex_le2me(this->wave);
  this->audio_type = formattag_to_buf_audio(this->wave->wFormatTag);
  if(!this->audio_type) {
    this->audio_type = BUF_AUDIO_UNKNOWN;
  }

  /* traverse through the chunks to find the 'data' chunk */
  this->data_start = this->data_size = 0;
  while (this->data_start == 0) {

    if (this->input->read(this->input, chunk_preamble, 8) != 8) {
      free (this->wave);
      return 0;
    }
    chunk_tag = LE_32(&chunk_preamble[0]);      
    chunk_size = LE_32(&chunk_preamble[4]);

    if (chunk_tag == data_TAG) {
      this->data_start = this->input->get_current_pos(this->input);
      this->data_size = chunk_size;
    } else {
      this->input->seek(this->input, chunk_size, SEEK_CUR);
    }
  }
  if (this->audio_type == BUF_AUDIO_LPCM_LE) {
    if (this->input->read(this->input, chunk_preamble, 6) != 6 ) {
      free (this->wave);
      return 0;
    }
    if (chunk_preamble[0] == 0x72 &&
        chunk_preamble[1] == 0xf8 &&
        chunk_preamble[2] == 0x1f &&
        chunk_preamble[3] == 0x4e ) {
      this->audio_type = BUF_AUDIO_DNET;
    }
    if (chunk_preamble[0] == 0xff &&
        chunk_preamble[1] == 0x1f &&
        chunk_preamble[2] == 0x00 &&
        chunk_preamble[3] == 0xe8 &&
        chunk_preamble[4] == 0xf1 &&    /* DTS standard document was wrong here! */
        chunk_preamble[5] == 0x07 ) {   /* DTS standard document was wrong here! */
      this->audio_type = BUF_AUDIO_DTS; /* DTS but in 14bit format. */
    }
#if 0
    printf("Preamble=%02x %02x %02x %02x %02x %02x\n",
        chunk_preamble[0],
        chunk_preamble[1],
        chunk_preamble[2],
        chunk_preamble[3],
        chunk_preamble[4],
        chunk_preamble[5]);
#endif

  }

  return 1;
}

static int demux_wav_send_chunk(demux_plugin_t *this_gen) {
  demux_wav_t *this = (demux_wav_t *) this_gen;

  buf_element_t *buf = NULL;
  unsigned int remaining_sample_bytes;
  off_t current_file_pos;
  int64_t current_pts;

  /* just load data chunks from wherever the stream happens to be
   * pointing; issue a DEMUX_FINISHED status if EOF is reached */
  if (this->wave->nBlockAlign < PREFERED_BLOCK_SIZE) {
    remaining_sample_bytes = PREFERED_BLOCK_SIZE / this->wave->nBlockAlign *
                             this->wave->nBlockAlign;
  } else{
    remaining_sample_bytes = this->wave->nBlockAlign;
  }
  current_file_pos =
    this->input->get_current_pos(this->input) - this->data_start;

  current_pts = current_file_pos;
  current_pts *= 90000;
  current_pts /= this->wave->nAvgBytesPerSec;

  if (this->seek_flag) {
    xine_demux_control_newpts(this->stream, current_pts, 0);
    this->seek_flag = 0;
  }

  while (remaining_sample_bytes) {
    if(!this->audio_fifo){
      this->status = DEMUX_FINISHED;
      break;
    }

    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
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

#if 0
    for(n=0;n<20;n++) {
      printf("%x ",buf->content[n]);
    }
    printf("\n");
#endif

    if (this->audio_type == BUF_AUDIO_LPCM_LE) {
      if (buf->content[0] == 0x72 &&
          buf->content[1] == 0xf8 &&
          buf->content[2] == 0x1f &&
          buf->content[3] == 0x4e ) {
        this->audio_type = BUF_AUDIO_DNET;
      }
    } else if (this->audio_type == BUF_AUDIO_DNET) {
      buf->pts=0; /* We don't need pts values for only audio streams. */
    }
#if 0
    for(n=0;n<20;n++) {
      printf("%x ",buf->content[n]);
    }
    printf("\n");
#endif

    if (!remaining_sample_bytes)
      buf->decoder_flags |= BUF_FLAG_FRAME_END;

    buf->type = this->audio_type;
    this->audio_fifo->put (this->audio_fifo, buf);
  }

  return this->status;
}

static void demux_wav_send_headers(demux_plugin_t *this_gen) {
  demux_wav_t *this = (demux_wav_t *) this_gen;
  buf_element_t *buf;

  this->video_fifo  = this->stream->video_fifo;
  this->audio_fifo  = this->stream->audio_fifo;

  this->status = DEMUX_OK;

  /* load stream information */
  this->stream->stream_info[XINE_STREAM_INFO_HAS_VIDEO] = 0;
  this->stream->stream_info[XINE_STREAM_INFO_HAS_AUDIO] = 1;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_CHANNELS] =
    this->wave->nChannels;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_SAMPLERATE] =
    this->wave->nSamplesPerSec;
  this->stream->stream_info[XINE_STREAM_INFO_AUDIO_BITS] =
    this->wave->wBitsPerSample;

  /* send start buffers */
  xine_demux_control_start(this->stream);

  /* send init info to decoders */
  if (this->audio_fifo && this->audio_type) {
    buf = this->audio_fifo->buffer_pool_alloc (this->audio_fifo);
    buf->type = this->audio_type;
    buf->decoder_flags = BUF_FLAG_HEADER;
    buf->decoder_info[0] = 0;
    buf->decoder_info[1] = this->wave->nSamplesPerSec;
    buf->decoder_info[2] = this->wave->wBitsPerSample;
    buf->decoder_info[3] = this->wave->nChannels;
    buf->content = (void *)this->wave;
    buf->size = this->wave_size;
    this->audio_fifo->put (this->audio_fifo, buf);
  }
}

static int demux_wav_seek (demux_plugin_t *this_gen,
                           off_t start_pos, int start_time) {

  demux_wav_t *this = (demux_wav_t *) this_gen;

  this->seek_flag = 1;
  this->status = DEMUX_OK;
  xine_demux_flush_engine (this->stream);

  /* if input is non-seekable, do not proceed with the rest of this
   * seek function */
  if (!INPUT_IS_SEEKABLE(this->input))
    return this->status;

  /* check the boundary offsets */
  if (start_pos <= 0)
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
    start_pos /= this->wave->nBlockAlign;
    start_pos *= this->wave->nBlockAlign;
    start_pos += this->data_start;

    this->input->seek(this->input, start_pos, SEEK_SET);
  }

  return this->status;
}

static void demux_wav_dispose (demux_plugin_t *this_gen) {
  demux_wav_t *this = (demux_wav_t *) this_gen;

  free(this->wave);
  free(this);
}

static int demux_wav_get_status (demux_plugin_t *this_gen) {
  demux_wav_t *this = (demux_wav_t *) this_gen;

  return this->status;
}

/* return the approximate length in miliseconds */
static int demux_wav_get_stream_length (demux_plugin_t *this_gen) {
  demux_wav_t *this = (demux_wav_t *) this_gen;

  return (int)((int64_t) this->data_size * 1000 / this->wave->nAvgBytesPerSec);
}

static uint32_t demux_wav_get_capabilities(demux_plugin_t *this_gen) {
  return DEMUX_CAP_NOCAP;
}

static int demux_wav_get_optional_data(demux_plugin_t *this_gen,
					void *data, int data_type) {
  return DEMUX_OPTIONAL_UNSUPPORTED;
}

static demux_plugin_t *open_plugin (demux_class_t *class_gen, xine_stream_t *stream,
                                    input_plugin_t *input) {

  demux_wav_t    *this;
  uint32_t	align;

  this         = xine_xmalloc (sizeof (demux_wav_t));
  this->stream = stream;
  this->input  = input;

  this->demux_plugin.send_headers      = demux_wav_send_headers;
  this->demux_plugin.send_chunk        = demux_wav_send_chunk;
  this->demux_plugin.seek              = demux_wav_seek;
  this->demux_plugin.dispose           = demux_wav_dispose;
  this->demux_plugin.get_status        = demux_wav_get_status;
  this->demux_plugin.get_stream_length = demux_wav_get_stream_length;
  this->demux_plugin.get_video_frame   = NULL;
  this->demux_plugin.got_video_frame_cb= NULL;
  this->demux_plugin.get_capabilities  = demux_wav_get_capabilities;
  this->demux_plugin.get_optional_data = demux_wav_get_optional_data;
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

    if (!open_wav_file(this)) {
      free (this);
      return NULL;
    }

  break;

  default:
    free (this);
    return NULL;
  }

  /* special block alignment hack so that the demuxer doesn't send
   * packets with individual PCM samples */
  if ((this->wave->nAvgBytesPerSec / this->wave->nBlockAlign) ==
      this->wave->nSamplesPerSec) {
    align = PCM_BLOCK_ALIGN / this->wave->nBlockAlign;
    align = align * this->wave->nBlockAlign;
    this->wave->nBlockAlign = align;
  }

  return &this->demux_plugin;
}

static char *get_description (demux_class_t *this_gen) {
  return "WAV file demux plugin";
}

static char *get_identifier (demux_class_t *this_gen) {
  return "WAV";
}

static char *get_extensions (demux_class_t *this_gen) {
  return "wav";
}

static char *get_mimetypes (demux_class_t *this_gen) {
  return "audio/x-wav: wav: WAV audio;"
         "audio/wav: wav: WAV audio;"
         "audio/x-pn-wav: wav: WAV audio;"
         "audio/x-pn-windows-acm: wav: WAV audio;";
}

static void class_dispose (demux_class_t *this_gen) {
  demux_wav_class_t *this = (demux_wav_class_t *) this_gen;

  free (this);
}

void *demux_wav_init_plugin (xine_t *xine, void *data) {
  demux_wav_class_t     *this;

  this = xine_xmalloc (sizeof (demux_wav_class_t));

  this->demux_class.open_plugin     = open_plugin;
  this->demux_class.get_description = get_description;
  this->demux_class.get_identifier  = get_identifier;
  this->demux_class.get_mimetypes   = get_mimetypes;
  this->demux_class.get_extensions  = get_extensions;
  this->demux_class.dispose         = class_dispose;

  return this;
}
